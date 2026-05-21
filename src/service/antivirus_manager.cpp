#include "service/antivirus_manager.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace trayapp::service {
namespace {

class FileByteStream final : public IByteStream {
public:
    explicit FileByteStream(const std::wstring& path)
        : file_(path, std::ios::binary)
    {
        if (!file_) {
            return;
        }

        file_.seekg(0, std::ios::end);
        size_ = static_cast<std::uint64_t>(file_.tellg());
        file_.seekg(0, std::ios::beg);
    }

    [[nodiscard]] bool IsOpen() const
    {
        return file_.is_open();
    }

    bool Seek(std::uint64_t position) override
    {
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(position), std::ios::beg);
        return file_.good();
    }

    std::uint64_t Position() const override
    {
        return position_;
    }

    std::uint64_t Size() const override
    {
        return size_;
    }

    bool Read(unsigned char* buffer, std::size_t size, std::size_t& bytesRead) override
    {
        file_.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(size));
        bytesRead = static_cast<std::size_t>(file_.gcount());
        position_ = static_cast<std::uint64_t>(file_.tellg());
        if (file_.eof()) {
            file_.clear();
        }

        return bytesRead == size;
    }

private:
    mutable std::ifstream file_;
    std::uint64_t size_ = 0;
    std::uint64_t position_ = 0;
};

[[nodiscard]] std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size <= 1) {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size);
    return result;
}

void AppendLine(std::wstringstream& stream, const std::wstring& line)
{
    if (stream.tellp() > 0) {
        stream << L"\r\n";
    }

    stream << line;
}

void MergeReport(ScanReport& target, const ScanReport& source)
{
    target.malicious = target.malicious || source.malicious;
    target.scannedFiles += source.scannedFiles;
    target.infectedFiles += source.infectedFiles;
    if (!source.summary.empty()) {
        if (!target.summary.empty()) {
            target.summary += L"\r\n";
        }
        target.summary += source.summary;
    }
}

[[nodiscard]] unsigned long long FileTimeKey(const std::filesystem::directory_entry& entry)
{
    const auto time = entry.last_write_time().time_since_epoch();
    return static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(time).count());
}

} // namespace

AntivirusManager::AntivirusManager() = default;

AntivirusManager::~AntivirusManager()
{
    Stop();
}

void AntivirusManager::Start()
{
    stopping_ = false;
    schedulerThread_ = std::thread(&AntivirusManager::SchedulerLoop, this);
    monitorThread_ = std::thread(&AntivirusManager::MonitorLoop, this);
}

void AntivirusManager::Stop()
{
    stopping_ = true;
    wake_.notify_all();
    if (schedulerThread_.joinable()) {
        schedulerThread_.join();
    }
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
}

void AntivirusManager::LoadDatabases()
{
    std::lock_guard<std::mutex> lock(mutex_);
    engine_.LoadBuiltinDatabase();
}

AvDatabaseInfo AntivirusManager::DatabaseInfo() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.DatabaseInfo();
}

ScanReport AntivirusManager::ScanFile(const std::wstring& path) const
{
    FileByteStream stream(path);
    if (!stream.IsOpen()) {
        return ScanReport { false, 0, 0, L"Не удалось открыть файл: " + path };
    }

    ScanResult result;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result = engine_.Scan(stream, DetectObjectType(path));
    }

    ScanReport report;
    report.malicious = result.malicious;
    report.scannedFiles = 1;
    report.infectedFiles = result.malicious ? 1 : 0;
    if (result.malicious) {
        report.summary = L"Обнаружено: " + Utf8ToWide(result.recordName) + L" в " + path;
    } else {
        report.summary = L"Угроз не найдено: " + path;
    }

    return report;
}

ScanReport AntivirusManager::ScanDirectory(const std::wstring& path) const
{
    ScanReport report;
    std::wstringstream details;

    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        report.summary = L"Директория не найдена: " + path;
        return report;
    }

    const std::filesystem::recursive_directory_iterator end;
    std::filesystem::recursive_directory_iterator iterator(path, std::filesystem::directory_options::skip_permission_denied, error);
    while (!error && iterator != end) {
        const std::filesystem::directory_entry entry = *iterator;
        if (entry.is_regular_file(error)) {
            ScanReport fileReport = ScanFile(entry.path().wstring());
            report.scannedFiles += fileReport.scannedFiles;
            report.infectedFiles += fileReport.infectedFiles;
            report.malicious = report.malicious || fileReport.malicious;
            if (fileReport.malicious) {
                AppendLine(details, fileReport.summary);
            }
        }

        iterator.increment(error);
    }

    report.summary = details.str();
    if (report.summary.empty()) {
        report.summary = L"Сканирование завершено. Проверено файлов: " + std::to_wstring(report.scannedFiles) + L", угроз не найдено.";
    }

    return report;
}

ScanReport AntivirusManager::ScanFixedDrives() const
{
    wchar_t drives[512] = {};
    const DWORD length = GetLogicalDriveStringsW(ARRAYSIZE(drives), drives);
    ScanReport report;

    for (const wchar_t* drive = drives; drive < drives + length && *drive != L'\0'; drive += wcslen(drive) + 1) {
        if (GetDriveTypeW(drive) == DRIVE_FIXED) {
            MergeReport(report, ScanDirectory(drive));
        }
    }

    if (report.summary.empty()) {
        report.summary = L"Сканирование несъемных дисков завершено. Проверено файлов: " + std::to_wstring(report.scannedFiles) + L".";
    }

    return report;
}

void AntivirusManager::ConfigureSchedule(unsigned long intervalMinutes)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        scheduleIntervalMinutes_ = intervalMinutes;
    }
    wake_.notify_all();
}

ScanReport AntivirusManager::LastScheduledReport() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastScheduledReport_;
}

void AntivirusManager::AddMonitorDirectory(const std::wstring& path)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (std::find(monitorDirectories_.begin(), monitorDirectories_.end(), path) == monitorDirectories_.end()) {
            monitorDirectories_.push_back(path);
        }
    }
    wake_.notify_all();
}

ScanReport AntivirusManager::LastMonitorReport() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastMonitorReport_;
}

void AntivirusManager::SchedulerLoop()
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_) {
        if (scheduleIntervalMinutes_ == 0) {
            wake_.wait(lock);
            continue;
        }

        const unsigned long interval = scheduleIntervalMinutes_;
        wake_.wait_for(lock, std::chrono::minutes(interval));
        if (stopping_ || scheduleIntervalMinutes_ == 0) {
            continue;
        }

        lock.unlock();
        ScanReport report = ScanFixedDrives();
        lock.lock();
        lastScheduledReport_ = std::move(report);
    }
}

void AntivirusManager::MonitorLoop()
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_) {
        wake_.wait_for(lock, std::chrono::seconds(5));
        if (stopping_ || monitorDirectories_.empty()) {
            continue;
        }

        lock.unlock();
        ScanChangedMonitorFiles();
        lock.lock();
    }
}

void AntivirusManager::ScanChangedMonitorFiles()
{
    std::vector<std::wstring> directories;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        directories = monitorDirectories_;
    }

    ScanReport report;
    for (const std::wstring& directory : directories) {
        std::error_code error;
        std::filesystem::recursive_directory_iterator iterator(directory, std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::recursive_directory_iterator end;
        while (!error && iterator != end) {
            const std::filesystem::directory_entry entry = *iterator;
            if (entry.is_regular_file(error)) {
                const std::wstring path = entry.path().wstring();
                const unsigned long long stamp = FileTimeKey(entry);
                bool changed = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto [found, inserted] = monitorSnapshot_.emplace(path, stamp);
                    changed = inserted || found->second != stamp;
                    found->second = stamp;
                }

                if (changed) {
                    MergeReport(report, ScanFile(path));
                }
            }

            iterator.increment(error);
        }
    }

    if (report.scannedFiles > 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastMonitorReport_ = std::move(report);
    }
}

} // namespace trayapp::service
