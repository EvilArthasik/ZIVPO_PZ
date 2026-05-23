#include "service/antivirus_manager.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

namespace trayapp::service {
namespace {

constexpr wchar_t kServerHost[] = L"localhost";
constexpr INTERNET_PORT kServerPort = 8080;
constexpr wchar_t kUserAgent[] = L"TraySampleService/1.0";
constexpr auto kDatabaseUpdateInterval = std::chrono::hours(6);

class WinHttpHandle {
public:
    explicit WinHttpHandle(HINTERNET handle = nullptr) noexcept
        : handle_(handle)
    {
    }

    ~WinHttpHandle()
    {
        if (handle_ != nullptr) {
            WinHttpCloseHandle(handle_);
        }
    }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    [[nodiscard]] HINTERNET Get() const noexcept
    {
        return handle_;
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return handle_ != nullptr;
    }

private:
    HINTERNET handle_ = nullptr;
};

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

[[nodiscard]] std::filesystem::path DatabaseRoot()
{
    wchar_t buffer[MAX_PATH] = {};
    DWORD size = GetEnvironmentVariableW(L"ProgramData", buffer, ARRAYSIZE(buffer));
    if (size == 0 || size >= ARRAYSIZE(buffer)) {
        return std::filesystem::path(L".") / L"avdb";
    }

    return std::filesystem::path(buffer) / L"TraySampleService" / L"avdb";
}

[[nodiscard]] std::string Base64Encode(const std::string& value)
{
    if (value.empty()) {
        return {};
    }

    DWORD size = 0;
    if (!CryptBinaryToStringA(
            reinterpret_cast<const BYTE*>(value.data()),
            static_cast<DWORD>(value.size()),
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
            nullptr,
            &size)) {
        return {};
    }

    std::string result(size, '\0');
    if (!CryptBinaryToStringA(
            reinterpret_cast<const BYTE*>(value.data()),
            static_cast<DWORD>(value.size()),
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
            result.data(),
            &size)) {
        return {};
    }

    if (!result.empty() && result.back() == '\0') {
        result.pop_back();
    }
    return result;
}

[[nodiscard]] bool HttpGet(const wchar_t* path, std::string& contentType, std::vector<unsigned char>& response)
{
    WinHttpHandle session(WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        return false;
    }

    DWORD secureProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session.Get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &secureProtocols, sizeof(secureProtocols));

    WinHttpHandle connection(WinHttpConnect(session.Get(), kServerHost, kServerPort, 0));
    if (!connection) {
        return false;
    }

    WinHttpHandle request(WinHttpOpenRequest(
        connection.Get(),
        L"GET",
        path,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (!request) {
        return false;
    }

    DWORD securityFlags =
        SECURITY_FLAG_IGNORE_UNKNOWN_CA |
        SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
        SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
        SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(request.Get(), WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags));

    const std::string credentials = "user:Strong#456";
    const std::string authorization = "Authorization: Basic " + Base64Encode(credentials);
    const std::wstring headers = Utf8ToWide(authorization + "\r\nAccept: multipart/mixed\r\n");

    const BOOL sent = WinHttpSendRequest(
        request.Get(),
        headers.c_str(),
        static_cast<DWORD>(headers.size()),
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0);
    if (!sent || !WinHttpReceiveResponse(request.Get(), nullptr)) {
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        request.Get(),
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusCodeSize,
        WINHTTP_NO_HEADER_INDEX);
    if (statusCode < 200 || statusCode >= 300) {
        return false;
    }

    wchar_t contentTypeBuffer[512] = {};
    DWORD contentTypeSize = sizeof(contentTypeBuffer);
    if (WinHttpQueryHeaders(
            request.Get(),
            WINHTTP_QUERY_CONTENT_TYPE,
            WINHTTP_HEADER_NAME_BY_INDEX,
            contentTypeBuffer,
            &contentTypeSize,
            WINHTTP_NO_HEADER_INDEX)) {
        contentType.clear();
        for (const wchar_t* ch = contentTypeBuffer; *ch != L'\0'; ++ch) {
            contentType.push_back(*ch <= 0x7F ? static_cast<char>(*ch) : '?');
        }
    }

    response.clear();
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.Get(), &available)) {
            return false;
        }
        if (available == 0) {
            break;
        }
        const std::size_t offset = response.size();
        response.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.Get(), response.data() + offset, available, &read)) {
            return false;
        }
        response.resize(offset + read);
    }

    return true;
}

[[nodiscard]] std::string LowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

[[nodiscard]] bool ExtractBoundary(const std::string& contentType, std::string& boundary)
{
    const std::string lower = LowerAscii(contentType);
    const std::string marker = "boundary=";
    const std::size_t pos = lower.find(marker);
    if (pos == std::string::npos) {
        return false;
    }
    boundary = contentType.substr(pos + marker.size());
    if (!boundary.empty() && boundary.front() == '"') {
        boundary.erase(boundary.begin());
    }
    if (!boundary.empty() && boundary.back() == '"') {
        boundary.pop_back();
    }
    return !boundary.empty();
}

void PutPart(const std::string& headers, const std::vector<unsigned char>& body, AvDatabasePackage& package)
{
    const std::string lower = LowerAscii(headers);
    if (lower.find("manifest.sig") != std::string::npos ||
        lower.find("signature-manifest-signature") != std::string::npos) {
        package.manifestSignature = body;
    } else if (lower.find("manifest.bin") != std::string::npos ||
        lower.find("signature-manifest") != std::string::npos) {
        package.manifest = body;
    } else if (lower.find("signatures.bin") != std::string::npos ||
        lower.find("signatures+octet-stream") != std::string::npos) {
        package.data = body;
    }
}

[[nodiscard]] AvDatabasePackage ParseMultipart(const std::string& contentType, const std::vector<unsigned char>& response)
{
    AvDatabasePackage package;
    std::string boundary;
    if (!ExtractBoundary(contentType, boundary)) {
        return package;
    }

    const std::string text(response.begin(), response.end());
    const std::string delimiter = "--" + boundary;
    std::size_t pos = 0;
    while ((pos = text.find(delimiter, pos)) != std::string::npos) {
        pos += delimiter.size();
        if (text.compare(pos, 2, "--") == 0) {
            break;
        }
        if (text.compare(pos, 2, "\r\n") == 0) {
            pos += 2;
        }

        const std::size_t headerEnd = text.find("\r\n\r\n", pos);
        if (headerEnd == std::string::npos) {
            break;
        }

        const std::size_t bodyStart = headerEnd + 4;
        std::size_t bodyEnd = text.find("\r\n" + delimiter, bodyStart);
        if (bodyEnd == std::string::npos) {
            break;
        }

        PutPart(
            text.substr(pos, headerEnd - pos),
            std::vector<unsigned char>(response.begin() + static_cast<std::ptrdiff_t>(bodyStart), response.begin() + static_cast<std::ptrdiff_t>(bodyEnd)),
            package);
        pos = bodyEnd + 2;
    }

    return package;
}

} // namespace

AntivirusManager::AntivirusManager()
    : databaseStore_(DatabaseRoot())
{
}

AntivirusManager::~AntivirusManager()
{
    Stop();
}

void AntivirusManager::Start()
{
    stopping_ = false;
    schedulerThread_ = std::thread(&AntivirusManager::SchedulerLoop, this);
    updateThread_ = std::thread(&AntivirusManager::UpdateLoop, this);
    monitorThread_ = std::thread(&AntivirusManager::MonitorLoop, this);
}

void AntivirusManager::Stop()
{
    stopping_ = true;
    wake_.notify_all();
    if (schedulerThread_.joinable()) {
        schedulerThread_.join();
    }
    if (updateThread_.joinable()) {
        updateThread_.join();
    }
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
}

void AntivirusManager::LoadDatabases()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!LoadDatabasesLocked()) {
        engine_.LoadBuiltinDatabase();
    }
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

void AntivirusManager::UpdateLoop()
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_) {
        wake_.wait_for(lock, kDatabaseUpdateInterval);
        if (stopping_) {
            break;
        }

        lock.unlock();
        static_cast<void>(UpdateDatabases());
        lock.lock();
    }
}

bool AntivirusManager::LoadDatabasesLocked()
{
    std::vector<AvRecord> records;
    long long releaseDateUnix = 0;
    if (databaseStore_.EnsureDefaultDatabase() &&
        databaseStore_.Load(records, releaseDateUnix)) {
        engine_.LoadRecords(records, releaseDateUnix);
        return true;
    }

    if (databaseStore_.RestoreBackup() &&
        databaseStore_.Load(records, releaseDateUnix)) {
        engine_.LoadRecords(records, releaseDateUnix);
        return true;
    }

    const AvDatabasePackage defaults = AntivirusDatabaseStore::BuildDefaultPackage();
    if (databaseStore_.SavePackage(defaults) &&
        AntivirusDatabaseStore::VerifyAndParse(defaults, records, releaseDateUnix)) {
        engine_.LoadRecords(records, releaseDateUnix);
        return true;
    }

    return false;
}

bool AntivirusManager::UpdateDatabases()
{
    std::string contentType;
    std::vector<unsigned char> response;
    if (!HttpGet(L"/api/signatures/binary", contentType, response)) {
        return false;
    }

    AvDatabasePackage package = ParseMultipart(contentType, response);
    std::vector<AvRecord> records;
    long long releaseDateUnix = 0;
    if (!AntivirusDatabaseStore::VerifyAndParse(package, records, releaseDateUnix)) {
        return false;
    }

    if (!databaseStore_.BackupCurrent()) {
        return false;
    }

    if (!databaseStore_.SavePackage(package)) {
        static_cast<void>(databaseStore_.RestoreBackup());
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!LoadDatabasesLocked()) {
            static_cast<void>(databaseStore_.RestoreBackup());
            return LoadDatabasesLocked();
        }
    }

    return true;
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
