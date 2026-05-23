#pragma once

#include "service/antivirus_engine.h"

#include <filesystem>
#include <string>
#include <vector>

namespace trayapp::service {

struct AvDatabasePackage {
    std::vector<unsigned char> manifest;
    std::vector<unsigned char> manifestSignature;
    std::vector<unsigned char> data;
};

class AntivirusDatabaseStore {
public:
    explicit AntivirusDatabaseStore(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& Root() const noexcept;
    [[nodiscard]] std::filesystem::path BackupRoot() const;

    bool EnsureDefaultDatabase();
    bool Load(std::vector<AvRecord>& records, long long& releaseDateUnix, std::vector<std::string>* damagedRecordIds = nullptr) const;
    bool LoadBackup(std::vector<AvRecord>& records, long long& releaseDateUnix, std::vector<std::string>* damagedRecordIds = nullptr) const;
    bool RestoreBackup();
    bool BackupCurrent();
    bool SavePackage(const AvDatabasePackage& package);

    [[nodiscard]] static AvDatabasePackage BuildDefaultPackage();
    [[nodiscard]] static bool VerifyAndParse(
        const AvDatabasePackage& package,
        std::vector<AvRecord>& records,
        long long& releaseDateUnix,
        std::vector<std::string>* damagedRecordIds = nullptr);

private:
    [[nodiscard]] AvDatabasePackage ReadPackage(const std::filesystem::path& directory) const;
    bool WritePackage(const std::filesystem::path& directory, const AvDatabasePackage& package) const;

    std::filesystem::path root_;
};

} // namespace trayapp::service
