#include "service/antivirus_database_store.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <utility>

namespace trayapp::service {
namespace {

constexpr unsigned char kDataMagic[] = { 'Z', 'S', 'G', 'D' };
constexpr unsigned char kManifestMagic[] = { 'Z', 'S', 'G', 'M' };
constexpr int kVersion = 1;
constexpr const wchar_t* kManifestFile = L"manifest.bin";
constexpr const wchar_t* kManifestSignatureFile = L"manifest.sig";
constexpr const wchar_t* kDataFile = L"signatures.bin";

[[nodiscard]] std::vector<unsigned char> ReadFile(const std::filesystem::path& path);

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
    if ((size > 0 && BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(size), 0) != 0) ||
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

class CertContext {
public:
    explicit CertContext(PCCERT_CONTEXT context = nullptr) noexcept
        : context_(context)
    {
    }

    ~CertContext()
    {
        if (context_ != nullptr) {
            CertFreeCertificateContext(context_);
        }
    }

    CertContext(const CertContext&) = delete;
    CertContext& operator=(const CertContext&) = delete;

    [[nodiscard]] PCCERT_CONTEXT Get() const noexcept
    {
        return context_;
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return context_ != nullptr;
    }

private:
    PCCERT_CONTEXT context_ = nullptr;
};

class BCryptKey {
public:
    explicit BCryptKey(BCRYPT_KEY_HANDLE key = nullptr) noexcept
        : key_(key)
    {
    }

    ~BCryptKey()
    {
        if (key_ != nullptr) {
            BCryptDestroyKey(key_);
        }
    }

    BCryptKey(const BCryptKey&) = delete;
    BCryptKey& operator=(const BCryptKey&) = delete;

    [[nodiscard]] BCRYPT_KEY_HANDLE Get() const noexcept
    {
        return key_;
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return key_ != nullptr;
    }

private:
    BCRYPT_KEY_HANDLE key_ = nullptr;
};

[[nodiscard]] std::filesystem::path ExecutableDirectory()
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            return {};
        }
        if (length < path.size() - 1) {
            path.resize(length);
            break;
        }
        path.resize(path.size() * 2);
    }

    return std::filesystem::path(path).parent_path();
}

[[nodiscard]] std::vector<std::filesystem::path> CertificateCandidates()
{
    std::vector<std::filesystem::path> candidates;

    wchar_t configured[MAX_PATH] = {};
    const DWORD configuredSize = GetEnvironmentVariableW(L"SIGNATURE_CERT_PATH", configured, ARRAYSIZE(configured));
    if (configuredSize > 0 && configuredSize < ARRAYSIZE(configured)) {
        candidates.emplace_back(configured);
    }

    const std::filesystem::path exeDir = ExecutableDirectory();
    if (!exeDir.empty()) {
        candidates.push_back(exeDir / L"ticket-signing.cer");
        candidates.push_back(exeDir / L"certs" / L"local" / L"ticket-signing.cer");
        candidates.push_back(exeDir / L"..\\..\\..\\..\\ZIVPO_Labs\\demo\\certs\\local\\ticket-signing.cer");
    }

    candidates.emplace_back(L"C:\\ProgramData\\TraySampleService\\ticket-signing.cer");
    return candidates;
}

[[nodiscard]] std::vector<unsigned char> LoadSigningCertificate()
{
    std::error_code error;
    for (const std::filesystem::path& candidate : CertificateCandidates()) {
        const std::filesystem::path normalized = std::filesystem::weakly_canonical(candidate, error);
        error.clear();
        const std::filesystem::path path = normalized.empty() ? candidate : normalized;
        if (std::filesystem::exists(path, error)) {
            std::vector<unsigned char> bytes = ReadFile(path);
            if (!bytes.empty()) {
                return bytes;
            }
        }
        error.clear();
    }

    return {};
}

[[nodiscard]] std::vector<unsigned char> DecodeCertificateDer(const std::vector<unsigned char>& certificate)
{
    if (certificate.empty()) {
        return {};
    }

    const std::string text(certificate.begin(), certificate.end());
    if (text.find("-----BEGIN CERTIFICATE-----") == std::string::npos) {
        return certificate;
    }

    DWORD decodedSize = 0;
    if (!CryptStringToBinaryA(
            text.c_str(),
            static_cast<DWORD>(text.size()),
            CRYPT_STRING_BASE64HEADER,
            nullptr,
            &decodedSize,
            nullptr,
            nullptr)) {
        return {};
    }

    std::vector<unsigned char> decoded(decodedSize);
    if (!CryptStringToBinaryA(
            text.c_str(),
            static_cast<DWORD>(text.size()),
            CRYPT_STRING_BASE64HEADER,
            decoded.data(),
            &decodedSize,
            nullptr,
            nullptr)) {
        return {};
    }

    decoded.resize(decodedSize);
    return decoded;
}

[[nodiscard]] bool VerifySha256Rsa(const std::vector<unsigned char>& data, const std::vector<unsigned char>& signature)
{
    const std::vector<unsigned char> certBytes = DecodeCertificateDer(LoadSigningCertificate());
    if (certBytes.empty()) {
        return false;
    }

    CertContext cert(CertCreateCertificateContext(
        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
        certBytes.data(),
        static_cast<DWORD>(certBytes.size())));
    if (!cert) {
        return false;
    }

    BCRYPT_KEY_HANDLE rawKey = nullptr;
    if (!CryptImportPublicKeyInfoEx2(
            X509_ASN_ENCODING,
            &cert.Get()->pCertInfo->SubjectPublicKeyInfo,
            0,
            nullptr,
            &rawKey)) {
        return false;
    }

    BCryptKey key(rawKey);
    const std::vector<unsigned char> digest = Sha256(data);
    if (digest.empty()) {
        return false;
    }

    BCRYPT_PKCS1_PADDING_INFO paddingInfo {};
    paddingInfo.pszAlgId = BCRYPT_SHA256_ALGORITHM;
    return BCryptVerifySignature(
        key.Get(),
        &paddingInfo,
        const_cast<PUCHAR>(digest.data()),
        static_cast<ULONG>(digest.size()),
        const_cast<PUCHAR>(signature.data()),
        static_cast<ULONG>(signature.size()),
        BCRYPT_PAD_PKCS1) == 0;
}

void AppendU8(std::vector<unsigned char>& out, unsigned char value)
{
    out.push_back(value);
}

void AppendI32(std::vector<unsigned char>& out, std::uint32_t value)
{
    out.push_back(static_cast<unsigned char>((value >> 24) & 0xFF));
    out.push_back(static_cast<unsigned char>((value >> 16) & 0xFF));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
    out.push_back(static_cast<unsigned char>(value & 0xFF));
}

void AppendI64(std::vector<unsigned char>& out, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<unsigned char>((value >> shift) & 0xFF));
    }
}

void AppendUtf8(std::vector<unsigned char>& out, const std::string& value)
{
    AppendI32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

void AppendBytes(std::vector<unsigned char>& out, const std::vector<unsigned char>& value)
{
    AppendI32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

[[nodiscard]] std::uint64_t ReadLittleEndianPrefix(const std::vector<unsigned char>& value)
{
    std::uint64_t prefix = 0;
    for (std::size_t index = 0; index < 8 && index < value.size(); ++index) {
        prefix |= static_cast<std::uint64_t>(value[index]) << (index * 8);
    }
    return prefix;
}

[[nodiscard]] ObjectType FileTypeToObjectType(const std::string& value)
{
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lower.find("pe") != std::string::npos || lower.find("exe") != std::string::npos
        ? ObjectType::PeFile
        : ObjectType::Script;
}

void AppendRecordSignatureBytes(std::vector<unsigned char>& out, const AvRecord& record)
{
    AppendI64(out, record.objectSignaturePrefix);
    AppendI32(out, record.objectSignatureLength);
    AppendBytes(out, record.firstBytes);
    AppendBytes(out, record.objectSignature);
    AppendI64(out, record.remainderLength);
    AppendI64(out, record.offsetBegin);
    AppendI64(out, record.offsetEnd);
    AppendU8(out, static_cast<unsigned char>(record.objectType));
    AppendUtf8(out, record.name);
}

[[nodiscard]] std::string HexUpper(const std::vector<unsigned char>& bytes)
{
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (unsigned char byte : bytes) {
        result.push_back(digits[(byte >> 4) & 0x0F]);
        result.push_back(digits[byte & 0x0F]);
    }
    return result;
}

void AppendCanonicalJsonString(std::string& out, const std::string& value)
{
    out.push_back('"');
    for (unsigned char ch : value) {
        switch (ch) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\r':
            out += "\\r";
            break;
        default:
            if (ch <= 0x1F) {
                constexpr char digits[] = "0123456789abcdef";
                out += "\\u00";
                out.push_back(digits[(ch >> 4) & 0x0F]);
                out.push_back(digits[ch & 0x0F]);
            } else {
                out.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    out.push_back('"');
}

void AppendCanonicalJsonField(std::string& out, const char* name, const std::string& value, bool& first)
{
    if (!first) {
        out.push_back(',');
    }
    first = false;
    AppendCanonicalJsonString(out, name);
    out.push_back(':');
    AppendCanonicalJsonString(out, value);
}

void AppendCanonicalJsonField(std::string& out, const char* name, std::uint64_t value, bool& first)
{
    if (!first) {
        out.push_back(',');
    }
    first = false;
    AppendCanonicalJsonString(out, name);
    out.push_back(':');
    out += std::to_string(value);
}

[[nodiscard]] std::vector<unsigned char> CanonicalRecordPayload(
    const std::string& threatName,
    const std::vector<unsigned char>& firstBytes,
    const std::vector<unsigned char>& remainderHash,
    std::uint64_t remainderLength,
    const std::string& fileType,
    std::uint64_t offsetStart,
    std::uint64_t offsetEnd,
    unsigned char status)
{
    std::string json;
    json.push_back('{');
    bool first = true;
    AppendCanonicalJsonField(json, "fileType", fileType, first);
    AppendCanonicalJsonField(json, "firstBytesHex", HexUpper(firstBytes), first);
    AppendCanonicalJsonField(json, "offsetEnd", offsetEnd, first);
    AppendCanonicalJsonField(json, "offsetStart", offsetStart, first);
    AppendCanonicalJsonField(json, "remainderHashHex", HexUpper(remainderHash), first);
    AppendCanonicalJsonField(json, "remainderLength", remainderLength, first);
    AppendCanonicalJsonField(json, "status", status == 2 ? "DELETED" : "ACTUAL", first);
    AppendCanonicalJsonField(json, "threatName", threatName, first);
    json.push_back('}');

    return std::vector<unsigned char>(json.begin(), json.end());
}

[[nodiscard]] std::vector<unsigned char> SignRecord(const AvRecord& record)
{
    std::vector<unsigned char> bytes;
    AppendRecordSignatureBytes(bytes, record);
    return Sha256(bytes);
}

class BinaryReader {
public:
    explicit BinaryReader(const std::vector<unsigned char>& data)
        : data_(data)
    {
    }

    bool ReadMagic(const unsigned char* magic, std::size_t size)
    {
        if (position_ + size > data_.size() || !std::equal(magic, magic + size, data_.begin() + static_cast<std::ptrdiff_t>(position_))) {
            return false;
        }
        position_ += size;
        return true;
    }

    bool ReadU8(unsigned char& value)
    {
        if (position_ >= data_.size()) {
            return false;
        }
        value = data_[position_++];
        return true;
    }

    bool ReadI32(std::uint32_t& value)
    {
        if (position_ + 4 > data_.size()) {
            return false;
        }
        value = (static_cast<std::uint32_t>(data_[position_]) << 24) |
            (static_cast<std::uint32_t>(data_[position_ + 1]) << 16) |
            (static_cast<std::uint32_t>(data_[position_ + 2]) << 8) |
            static_cast<std::uint32_t>(data_[position_ + 3]);
        position_ += 4;
        return true;
    }

    bool ReadI64(std::uint64_t& value)
    {
        if (position_ + 8 > data_.size()) {
            return false;
        }
        value = 0;
        for (int index = 0; index < 8; ++index) {
            value = (value << 8) | data_[position_ + static_cast<std::size_t>(index)];
        }
        position_ += 8;
        return true;
    }

    bool ReadUtf8(std::string& value)
    {
        std::vector<unsigned char> bytes;
        if (!ReadBytes(bytes)) {
            return false;
        }
        value.assign(bytes.begin(), bytes.end());
        return true;
    }

    bool ReadBytes(std::vector<unsigned char>& value)
    {
        std::uint32_t size = 0;
        if (!ReadI32(size) || position_ + size > data_.size()) {
            return false;
        }
        value.assign(data_.begin() + static_cast<std::ptrdiff_t>(position_), data_.begin() + static_cast<std::ptrdiff_t>(position_ + size));
        position_ += size;
        return true;
    }

private:
    const std::vector<unsigned char>& data_;
    std::size_t position_ = 0;
};

[[nodiscard]] std::vector<unsigned char> ReadFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool WriteFile(const std::filesystem::path& path, const std::vector<unsigned char>& data)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return file.good();
}

AvRecord MakeDefaultRecord(const std::string& name, const std::vector<unsigned char>& signature, std::uint64_t offsetBegin, std::uint64_t offsetEnd, ObjectType type)
{
    AvRecord record;
    record.objectSignaturePrefix = ReadLittleEndianPrefix(signature);
    record.objectSignatureLength = static_cast<std::uint32_t>(signature.size());
    record.firstBytes = signature;
    record.objectSignature = Sha256(nullptr, 0);
    record.remainderLength = 0;
    record.offsetBegin = offsetBegin;
    record.offsetEnd = offsetEnd;
    record.objectType = type;
    record.name = name;
    record.avRecordSignature = SignRecord(record);
    return record;
}

[[nodiscard]] std::vector<AvRecord> DefaultRecords()
{
    return {
        MakeDefaultRecord(
            "Demo.EICAR.Script",
            { 'X', '5', 'O', '!', 'P', '%', '@', 'A', 'P', '[', '4', '\\', 'P', 'Z', 'X', '5', '4', '(', 'P', '^', ')', '7', 'C', 'C' },
            0,
            4096,
            ObjectType::Script),
        MakeDefaultRecord(
            "Demo.PE.TestSignature",
            { 'M', 'Z', 'T', 'R', 'A', 'Y', 'A', 'V', 'D', 'E', 'M', 'O', 'S', 'I', 'G', 'N' },
            0,
            1024 * 1024,
            ObjectType::PeFile)
    };
}

[[nodiscard]] std::vector<unsigned char> BuildData(const std::vector<AvRecord>& records)
{
    std::vector<unsigned char> data;
    data.insert(data.end(), std::begin(kDataMagic), std::end(kDataMagic));
    AppendI32(data, kVersion);
    AppendI32(data, static_cast<std::uint32_t>(records.size()));
    for (const AvRecord& record : records) {
        AppendI64(data, 0);
        AppendI64(data, 0);
        AppendUtf8(data, record.name);
        AppendBytes(data, record.firstBytes);
        AppendBytes(data, record.objectSignature);
        AppendI64(data, record.remainderLength);
        AppendUtf8(data, record.objectType == ObjectType::PeFile ? "PE" : "SCRIPT");
        AppendI64(data, record.offsetBegin);
        AppendI64(data, record.offsetEnd);
        AppendI64(data, 0);
        AppendU8(data, 1);
        AppendBytes(data, record.avRecordSignature);
    }
    return data;
}

[[nodiscard]] std::vector<unsigned char> BuildManifest(int count, const std::vector<unsigned char>& data, long long releaseMillis)
{
    std::vector<unsigned char> manifest;
    manifest.insert(manifest.end(), std::begin(kManifestMagic), std::end(kManifestMagic));
    AppendI32(manifest, kVersion);
    AppendUtf8(manifest, "FULL");
    AppendI64(manifest, static_cast<std::uint64_t>(releaseMillis));
    AppendI32(manifest, static_cast<std::uint32_t>(count));
    AppendUtf8(manifest, "signatures.bin");
    AppendUtf8(manifest, "application/vnd.zivpo.signatures+octet-stream");
    AppendI64(manifest, data.size());
    AppendBytes(manifest, Sha256(data));
    AppendUtf8(manifest, "SHA-256");
    AppendUtf8(manifest, "SHA256withRSA");
    AppendI32(manifest, 0);
    AppendUtf8(manifest, "local-service-format-compatible-with-web-binary-v1");
    return manifest;
}

} // namespace

AntivirusDatabaseStore::AntivirusDatabaseStore(std::filesystem::path root)
    : root_(std::move(root))
{
}

const std::filesystem::path& AntivirusDatabaseStore::Root() const noexcept
{
    return root_;
}

std::filesystem::path AntivirusDatabaseStore::BackupRoot() const
{
    return root_.parent_path() / L"avdb-backup";
}

bool AntivirusDatabaseStore::EnsureDefaultDatabase()
{
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    if (std::filesystem::exists(root_ / kManifestFile, error) &&
        std::filesystem::exists(root_ / kManifestSignatureFile, error) &&
        std::filesystem::exists(root_ / kDataFile, error)) {
        return true;
    }
    return WritePackage(root_, BuildDefaultPackage());
}

bool AntivirusDatabaseStore::Load(std::vector<AvRecord>& records, long long& releaseDateUnix) const
{
    return VerifyAndParse(ReadPackage(root_), records, releaseDateUnix);
}

bool AntivirusDatabaseStore::LoadBackup(std::vector<AvRecord>& records, long long& releaseDateUnix) const
{
    return VerifyAndParse(ReadPackage(BackupRoot()), records, releaseDateUnix);
}

bool AntivirusDatabaseStore::RestoreBackup()
{
    std::error_code error;
    const std::filesystem::path backup = BackupRoot();
    if (!std::filesystem::exists(backup / kManifestFile, error)) {
        return false;
    }

    std::filesystem::create_directories(root_, error);
    return std::filesystem::copy_file(backup / kManifestFile, root_ / kManifestFile, std::filesystem::copy_options::overwrite_existing, error) &&
        std::filesystem::copy_file(backup / kManifestSignatureFile, root_ / kManifestSignatureFile, std::filesystem::copy_options::overwrite_existing, error) &&
        std::filesystem::copy_file(backup / kDataFile, root_ / kDataFile, std::filesystem::copy_options::overwrite_existing, error);
}

bool AntivirusDatabaseStore::BackupCurrent()
{
    std::error_code error;
    const std::filesystem::path backup = BackupRoot();
    if (!std::filesystem::exists(root_ / kManifestFile, error) ||
        !std::filesystem::exists(root_ / kManifestSignatureFile, error) ||
        !std::filesystem::exists(root_ / kDataFile, error)) {
        return false;
    }

    std::filesystem::remove_all(backup, error);
    error.clear();
    std::filesystem::create_directories(backup, error);
    if (error) {
        return false;
    }

    error.clear();
    std::filesystem::copy_file(root_ / kManifestFile, backup / kManifestFile, std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        return false;
    }

    error.clear();
    std::filesystem::copy_file(root_ / kManifestSignatureFile, backup / kManifestSignatureFile, std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        return false;
    }

    error.clear();
    std::filesystem::copy_file(root_ / kDataFile, backup / kDataFile, std::filesystem::copy_options::overwrite_existing, error);
    return !error;
}

bool AntivirusDatabaseStore::SavePackage(const AvDatabasePackage& package)
{
    return WritePackage(root_, package);
}

AvDatabasePackage AntivirusDatabaseStore::BuildDefaultPackage()
{
    const std::vector<AvRecord> records = DefaultRecords();
    AvDatabasePackage package;
    package.data = BuildData(records);
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    package.manifest = BuildManifest(static_cast<int>(records.size()), package.data, now);
    package.manifestSignature = Sha256(package.manifest);
    return package;
}

bool AntivirusDatabaseStore::VerifyAndParse(const AvDatabasePackage& package, std::vector<AvRecord>& records, long long& releaseDateUnix)
{
    if (package.manifest.empty() || package.manifestSignature.empty() || package.data.empty()) {
        return false;
    }

    BinaryReader manifest(package.manifest);
    std::uint32_t version = 0;
    std::string scope;
    std::uint64_t releaseMillis = 0;
    std::uint32_t count = 0;
    std::string dataFile;
    std::string contentType;
    std::uint64_t dataSize = 0;
    std::vector<unsigned char> dataHash;
    std::string hashAlgorithm;
    std::string signatureAlgorithm;
    if (!manifest.ReadMagic(kManifestMagic, sizeof(kManifestMagic)) ||
        !manifest.ReadI32(version) ||
        version != kVersion ||
        !manifest.ReadUtf8(scope) ||
        !manifest.ReadI64(releaseMillis) ||
        !manifest.ReadI32(count) ||
        !manifest.ReadUtf8(dataFile) ||
        !manifest.ReadUtf8(contentType) ||
        !manifest.ReadI64(dataSize) ||
        !manifest.ReadBytes(dataHash) ||
        !manifest.ReadUtf8(hashAlgorithm) ||
        !manifest.ReadUtf8(signatureAlgorithm)) {
        return false;
    }
    static_cast<void>(scope);
    static_cast<void>(count);
    static_cast<void>(dataFile);
    static_cast<void>(contentType);
    static_cast<void>(hashAlgorithm);

    const bool localManifestSignature = Sha256(package.manifest) == package.manifestSignature;
    const bool webManifestSignature = signatureAlgorithm == "SHA256withRSA" &&
        VerifySha256Rsa(package.manifest, package.manifestSignature);
    if (!localManifestSignature && !webManifestSignature) {
        return false;
    }

    if (dataSize != package.data.size() || dataHash != Sha256(package.data)) {
        return false;
    }

    BinaryReader data(package.data);
    std::uint32_t dataVersion = 0;
    std::uint32_t dataCount = 0;
    if (!data.ReadMagic(kDataMagic, sizeof(kDataMagic)) ||
        !data.ReadI32(dataVersion) ||
        dataVersion != kVersion ||
        !data.ReadI32(dataCount)) {
        return false;
    }

    records.clear();
    for (std::uint32_t index = 0; index < dataCount; ++index) {
        std::uint64_t most = 0;
        std::uint64_t least = 0;
        std::string name;
        std::vector<unsigned char> firstBytes;
        std::vector<unsigned char> remainderHash;
        std::uint64_t remainderLength = 0;
        std::string fileType;
        std::uint64_t offsetStart = 0;
        std::uint64_t offsetEnd = 0;
        std::uint64_t updatedAt = 0;
        unsigned char status = 0;
        std::vector<unsigned char> signature;

        if (!data.ReadI64(most) ||
            !data.ReadI64(least) ||
            !data.ReadUtf8(name) ||
            !data.ReadBytes(firstBytes) ||
            !data.ReadBytes(remainderHash) ||
            !data.ReadI64(remainderLength) ||
            !data.ReadUtf8(fileType) ||
            !data.ReadI64(offsetStart) ||
            !data.ReadI64(offsetEnd) ||
            !data.ReadI64(updatedAt) ||
            !data.ReadU8(status) ||
            !data.ReadBytes(signature)) {
            return false;
        }
        static_cast<void>(most);
        static_cast<void>(least);
        static_cast<void>(updatedAt);

        const std::vector<unsigned char> canonicalPayload = CanonicalRecordPayload(
            name,
            firstBytes,
            remainderHash,
            remainderLength,
            fileType,
            offsetStart,
            offsetEnd,
            status);

        if (firstBytes.size() < 8 || remainderHash.size() != 32) {
            continue;
        }

        const std::uint64_t totalLength = firstBytes.size() + remainderLength;
        if (totalLength > std::numeric_limits<std::uint32_t>::max()) {
            continue;
        }

        AvRecord record;
        record.objectSignaturePrefix = ReadLittleEndianPrefix(firstBytes);
        record.objectSignatureLength = static_cast<std::uint32_t>(totalLength);
        record.firstBytes = std::move(firstBytes);
        record.objectSignature = std::move(remainderHash);
        record.remainderLength = remainderLength;
        record.offsetBegin = offsetStart;
        record.offsetEnd = offsetEnd;
        record.objectType = FileTypeToObjectType(fileType);
        record.name = std::move(name);
        record.avRecordSignature = std::move(signature);

        const bool localRecordSignature = record.avRecordSignature == SignRecord(record);
        const bool webRecordSignature = VerifySha256Rsa(canonicalPayload, record.avRecordSignature);
        if (!localRecordSignature && !webRecordSignature) {
            continue;
        }

        if (status != 1) {
            continue;
        }

        records.push_back(std::move(record));
    }

    releaseDateUnix = static_cast<long long>(releaseMillis / 1000);
    return true;
}

AvDatabasePackage AntivirusDatabaseStore::ReadPackage(const std::filesystem::path& directory) const
{
    return AvDatabasePackage {
        ReadFile(directory / kManifestFile),
        ReadFile(directory / kManifestSignatureFile),
        ReadFile(directory / kDataFile)
    };
}

bool AntivirusDatabaseStore::WritePackage(const std::filesystem::path& directory, const AvDatabasePackage& package) const
{
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return WriteFile(directory / kManifestFile, package.manifest) &&
        WriteFile(directory / kManifestSignatureFile, package.manifestSignature) &&
        WriteFile(directory / kDataFile, package.data);
}

} // namespace trayapp::service
