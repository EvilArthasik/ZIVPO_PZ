#include "service/antivirus_engine.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <limits>

namespace trayapp::service {
namespace {

constexpr std::size_t kPrefixSize = 8;

[[nodiscard]] std::uint64_t ReadLittleEndianPrefix(const unsigned char* data)
{
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < kPrefixSize; ++index) {
        value |= static_cast<std::uint64_t>(data[index]) << (index * 8);
    }

    return value;
}

[[nodiscard]] std::vector<unsigned char> Sha256(const unsigned char* data, std::size_t size)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return {};
    }

    DWORD objectLength = 0;
    DWORD resultSize = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultSize, 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }

    std::vector<unsigned char> hashObject(objectLength);
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(algorithm, &hash, hashObject.data(), objectLength, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }

    std::vector<unsigned char> digest(32);
    if (BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(size), 0) != 0 ||
        BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) != 0) {
        digest.clear();
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return digest;
}

[[nodiscard]] std::vector<unsigned char> Sha256(const std::vector<unsigned char>& data)
{
    return Sha256(data.data(), data.size());
}

void AppendInteger(std::vector<unsigned char>& output, std::uint64_t value, std::size_t bytes)
{
    for (std::size_t index = 0; index < bytes; ++index) {
        output.push_back(static_cast<unsigned char>((value >> (index * 8)) & 0xFF));
    }
}

[[nodiscard]] std::vector<unsigned char> SignRecord(const AvRecord& record)
{
    std::vector<unsigned char> bytes;
    AppendInteger(bytes, record.objectSignaturePrefix, 8);
    AppendInteger(bytes, record.objectSignatureLength, 4);
    bytes.insert(bytes.end(), record.objectSignature.begin(), record.objectSignature.end());
    AppendInteger(bytes, record.offsetBegin, 8);
    AppendInteger(bytes, record.offsetEnd, 8);
    AppendInteger(bytes, static_cast<unsigned char>(record.objectType), 1);
    return Sha256(bytes);
}

[[nodiscard]] std::wstring Lowercase(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

[[nodiscard]] std::wstring ExtensionOf(const std::wstring& path)
{
    const std::size_t slash = path.find_last_of(L"\\/");
    const std::size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) {
        return {};
    }

    return Lowercase(path.substr(dot));
}

} // namespace

void AntivirusEngine::LoadBuiltinDatabase()
{
    records_.clear();

    AddRecord(
        "Demo.EICAR.Script",
        std::vector<unsigned char> {
            'X', '5', 'O', '!', 'P', '%', '@', 'A',
            'P', '[', '4', '\\', 'P', 'Z', 'X', '5',
            '4', '(', 'P', '^', ')', '7', 'C', 'C'
        },
        0,
        4096,
        ObjectType::Script);

    AddRecord(
        "Demo.PE.TestSignature",
        std::vector<unsigned char> {
            'M', 'Z', 'T', 'R', 'A', 'Y', 'A', 'V',
            'D', 'E', 'M', 'O', 'S', 'I', 'G', 'N'
        },
        0,
        1024 * 1024,
        ObjectType::PeFile);

    info_.releaseDateUnix = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    info_.recordCount = 0;
    for (const auto& bucket : records_) {
        info_.recordCount += static_cast<unsigned long>(bucket.second.size());
    }
}

AvDatabaseInfo AntivirusEngine::DatabaseInfo() const
{
    return info_;
}

ScanResult AntivirusEngine::Scan(IByteStream& stream, ObjectType objectType) const
{
    if (records_.empty() || stream.Size() < kPrefixSize) {
        return {};
    }

    std::array<unsigned char, kPrefixSize> prefix {};
    const std::uint64_t maxOffset = stream.Size() - kPrefixSize;

    for (std::uint64_t offset = 0; offset <= maxOffset; ++offset) {
        if (!stream.Seek(offset)) {
            break;
        }

        std::size_t bytesRead = 0;
        if (!stream.Read(prefix.data(), prefix.size(), bytesRead) || bytesRead != prefix.size()) {
            break;
        }

        const auto found = records_.find(ReadLittleEndianPrefix(prefix.data()));
        if (found == records_.end()) {
            continue;
        }

        for (const AvRecord& record : found->second) {
            if (record.objectType != objectType) {
                continue;
            }

            if (offset < record.offsetBegin || offset > record.offsetEnd || record.objectSignatureLength < kPrefixSize) {
                continue;
            }

            const std::uint64_t remainingSize = stream.Size() - offset;
            if (remainingSize < record.objectSignatureLength) {
                continue;
            }

            std::vector<unsigned char> signatureBytes(record.objectSignatureLength);
            std::copy(prefix.begin(), prefix.end(), signatureBytes.begin());

            const std::size_t tailSize = static_cast<std::size_t>(record.objectSignatureLength - kPrefixSize);
            std::size_t tailRead = 0;
            if (!stream.Read(signatureBytes.data() + kPrefixSize, tailSize, tailRead) || tailRead != tailSize) {
                continue;
            }

            if (Sha256(signatureBytes) == record.objectSignature) {
                return ScanResult { true, record.name, offset };
            }
        }
    }

    return {};
}

void AntivirusEngine::AddRecord(
    const std::string& name,
    const std::vector<unsigned char>& signature,
    std::uint64_t offsetBegin,
    std::uint64_t offsetEnd,
    ObjectType objectType)
{
    if (signature.size() < kPrefixSize || signature.size() > std::numeric_limits<std::uint32_t>::max()) {
        return;
    }

    AvRecord record;
    record.objectSignaturePrefix = ReadLittleEndianPrefix(signature.data());
    record.objectSignatureLength = static_cast<std::uint32_t>(signature.size());
    record.objectSignature = Sha256(signature);
    record.offsetBegin = offsetBegin;
    record.offsetEnd = offsetEnd;
    record.objectType = objectType;
    record.name = name;
    record.avRecordSignature = SignRecord(record);

    records_[record.objectSignaturePrefix].push_back(std::move(record));
}

ObjectType DetectObjectType(const std::wstring& path)
{
    const std::wstring extension = ExtensionOf(path);
    if (extension == L".exe" || extension == L".dll" || extension == L".sys") {
        return ObjectType::PeFile;
    }

    return ObjectType::Script;
}

} // namespace trayapp::service
