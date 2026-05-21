#pragma once

#include "service/antivirus_engine.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace trayapp::service {

struct ScanReport {
    bool malicious = false;
    unsigned long scannedFiles = 0;
    unsigned long infectedFiles = 0;
    std::wstring summary;
};

class AntivirusManager {
public:
    AntivirusManager();
    ~AntivirusManager();

    AntivirusManager(const AntivirusManager&) = delete;
    AntivirusManager& operator=(const AntivirusManager&) = delete;

    void Start();
    void Stop();
    void LoadDatabases();

    [[nodiscard]] AvDatabaseInfo DatabaseInfo() const;
    [[nodiscard]] ScanReport ScanFile(const std::wstring& path) const;
    [[nodiscard]] ScanReport ScanDirectory(const std::wstring& path) const;
    [[nodiscard]] ScanReport ScanFixedDrives() const;

    void ConfigureSchedule(unsigned long intervalMinutes);
    [[nodiscard]] ScanReport LastScheduledReport() const;

    void AddMonitorDirectory(const std::wstring& path);
    [[nodiscard]] ScanReport LastMonitorReport() const;

private:
    void SchedulerLoop();
    void MonitorLoop();
    void ScanChangedMonitorFiles();

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread schedulerThread_;
    std::thread monitorThread_;
    std::atomic_bool stopping_ = false;

    AntivirusEngine engine_;
    unsigned long scheduleIntervalMinutes_ = 0;
    ScanReport lastScheduledReport_;
    ScanReport lastMonitorReport_;
    std::vector<std::wstring> monitorDirectories_;
    std::map<std::wstring, unsigned long long> monitorSnapshot_;
};

} // namespace trayapp::service
