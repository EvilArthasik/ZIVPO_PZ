#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace trayapp::service {

enum class ObjectType : unsigned char {
    PeFile = 1,
    Script = 2
};

struct AvRecord {
    std::uint64_t objectSignaturePrefix = 0;
    std::uint32_t objectSignatureLength = 0;
    std::vector<unsigned char> objectSignature;
    std::uint64_t offsetBegin = 0;
    std::uint64_t offsetEnd = 0;
    ObjectType objectType = ObjectType::PeFile;
    std::vector<unsigned char> avRecordSignature;
    std::string name;
};

struct AvDatabaseInfo {
    long long releaseDateUnix = 0;
    unsigned long recordCount = 0;
};

struct ScanResult {
    bool malicious = false;
    std::string recordName;
    std::uint64_t offset = 0;
};

class IByteStream {
public:
    virtual ~IByteStream() = default;
    virtual bool Seek(std::uint64_t position) = 0;
    virtual std::uint64_t Position() const = 0;
    virtual std::uint64_t Size() const = 0;
    virtual bool Read(unsigned char* buffer, std::size_t size, std::size_t& bytesRead) = 0;
};

class AntivirusEngine {
public:
    void LoadBuiltinDatabase();
    [[nodiscard]] AvDatabaseInfo DatabaseInfo() const;
    [[nodiscard]] ScanResult Scan(IByteStream& stream, ObjectType objectType) const;

private:
    void AddRecord(const std::string& name, const std::vector<unsigned char>& signature, std::uint64_t offsetBegin, std::uint64_t offsetEnd, ObjectType objectType);

    std::map<std::uint64_t, std::vector<AvRecord>> records_;
    AvDatabaseInfo info_;
};

[[nodiscard]] ObjectType DetectObjectType(const std::wstring& path);

} // namespace trayapp::service
