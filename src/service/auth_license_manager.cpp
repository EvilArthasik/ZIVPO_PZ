#include "service/auth_license_manager.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace trayapp::service {
namespace {

constexpr wchar_t kServerHost[] = L"localhost";
constexpr INTERNET_PORT kServerPort = 8080;
constexpr wchar_t kUserAgent[] = L"TraySampleService/1.0";
constexpr auto kMinimumRefreshDelay = std::chrono::seconds(15);
constexpr auto kRefreshSafetyMargin = std::chrono::seconds(60);
constexpr auto kFallbackTicketRefresh = std::chrono::minutes(5);

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

[[nodiscard]] std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }

    std::string result(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
}

[[nodiscard]] std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size <= 1) {
        return {};
    }

    std::wstring result(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size);
    return result;
}

[[nodiscard]] std::string EscapeJson(const std::wstring& value)
{
    std::string input = WideToUtf8(value);
    std::string escaped;
    escaped.reserve(input.size() + 8);

    for (char ch : input) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }

    return escaped;
}

[[nodiscard]] bool HttpPost(const wchar_t* path, const std::string& body, const std::wstring& bearerToken, std::string& response)
{
    WinHttpHandle session(WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        return false;
    }

    WinHttpHandle connection(WinHttpConnect(session.Get(), kServerHost, kServerPort, 0));
    if (!connection) {
        return false;
    }

    WinHttpHandle request(WinHttpOpenRequest(
        connection.Get(),
        L"POST",
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

    std::wstring headers = L"Content-Type: application/json\r\n";
    if (!bearerToken.empty()) {
        headers += L"Authorization: Bearer ";
        headers += bearerToken;
        headers += L"\r\n";
    }

    const BOOL sent = WinHttpSendRequest(
        request.Get(),
        headers.c_str(),
        static_cast<DWORD>(headers.size()),
        const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()),
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

    response.clear();
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.Get(), &available)) {
            return false;
        }

        if (available == 0) {
            break;
        }

        const size_t offset = response.size();
        response.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.Get(), response.data() + offset, available, &read)) {
            return false;
        }

        response.resize(offset + read);
    }

    return statusCode >= 200 && statusCode < 300;
}

[[nodiscard]] size_t FindValueStart(const std::string& json, const char* key)
{
    const std::string quotedKey = std::string("\"") + key + "\"";
    const size_t keyPos = json.find(quotedKey);
    if (keyPos == std::string::npos) {
        return std::string::npos;
    }

    const size_t colon = json.find(':', keyPos + quotedKey.size());
    if (colon == std::string::npos) {
        return std::string::npos;
    }

    size_t pos = colon + 1;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
        ++pos;
    }

    return pos;
}

[[nodiscard]] bool ExtractString(const std::string& json, const char* key, std::string& value)
{
    size_t pos = FindValueStart(json, key);
    if (pos == std::string::npos || pos >= json.size() || json[pos] != '"') {
        return false;
    }

    ++pos;
    value.clear();
    while (pos < json.size()) {
        const char ch = json[pos++];
        if (ch == '"') {
            return true;
        }

        if (ch == '\\' && pos < json.size()) {
            value.push_back(json[pos++]);
        } else {
            value.push_back(ch);
        }
    }

    return false;
}

[[nodiscard]] bool ExtractLong(const std::string& json, const char* key, long long& value)
{
    size_t pos = FindValueStart(json, key);
    if (pos == std::string::npos) {
        return false;
    }

    char* end = nullptr;
    value = std::strtoll(json.c_str() + pos, &end, 10);
    return end != json.c_str() + pos;
}

[[nodiscard]] bool ExtractBool(const std::string& json, const char* key, bool& value)
{
    const size_t pos = FindValueStart(json, key);
    if (pos == std::string::npos) {
        return false;
    }

    if (json.compare(pos, 4, "true") == 0) {
        value = true;
        return true;
    }

    if (json.compare(pos, 5, "false") == 0) {
        value = false;
        return true;
    }

    return false;
}

[[nodiscard]] long long TimePointToUnix(std::chrono::system_clock::time_point value)
{
    return std::chrono::duration_cast<std::chrono::seconds>(value.time_since_epoch()).count();
}

[[nodiscard]] std::chrono::system_clock::time_point UnixToTimePoint(long long value)
{
    return std::chrono::system_clock::time_point(std::chrono::seconds(value));
}

[[nodiscard]] bool ParseLocalDateTime(const std::string& value, long long& unixTime)
{
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    if (sscanf_s(value.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) < 5) {
        return false;
    }

    std::tm tmValue = {};
    tmValue.tm_year = year - 1900;
    tmValue.tm_mon = month - 1;
    tmValue.tm_mday = day;
    tmValue.tm_hour = hour;
    tmValue.tm_min = minute;
    tmValue.tm_sec = second;
    tmValue.tm_isdst = -1;

    const std::time_t local = std::mktime(&tmValue);
    if (local == static_cast<std::time_t>(-1)) {
        return false;
    }

    unixTime = static_cast<long long>(local);
    return true;
}

[[nodiscard]] std::chrono::system_clock::time_point CalculateRefreshTime(
    std::chrono::system_clock::time_point expiresAt,
    std::chrono::seconds fallback)
{
    const auto now = std::chrono::system_clock::now();
    if (expiresAt <= now + kMinimumRefreshDelay) {
        return now + kMinimumRefreshDelay;
    }

    const auto refreshAt = expiresAt - kRefreshSafetyMargin;
    return refreshAt > now + kMinimumRefreshDelay ? refreshAt : now + fallback;
}

} // namespace

AuthLicenseManager::AuthLicenseManager() = default;

AuthLicenseManager::~AuthLicenseManager()
{
    Stop();
}

void AuthLicenseManager::Start()
{
    stopping_ = false;
    worker_ = std::thread(&AuthLicenseManager::WorkerLoop, this);
}

void AuthLicenseManager::Stop()
{
    stopping_ = true;
    WakeWorker();
    if (worker_.joinable()) {
        worker_.join();
    }
}

unsigned long AuthLicenseManager::Login(const std::wstring& username, const std::wstring& password, AuthSnapshot& snapshot)
{
    const std::string body =
        "{\"username\":\"" + EscapeJson(username) +
        "\",\"password\":\"" + EscapeJson(password) + "\"}";

    std::string response;
    if (!HttpPost(L"/api/auth/login", body, {}, response)) {
        return kRpcAuthenticationFailed;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        const unsigned long result = StoreTokenResponseLocked(response, username);
        if (result != kRpcSuccess) {
            return result;
        }

        ticket_ = Ticket {};
        licenseKey_.clear();
        snapshot = AuthSnapshot { true, username_ };
    }

    WakeWorker();
    return kRpcSuccess;
}

void AuthLicenseManager::Logout()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        authenticated_ = false;
        username_.clear();
        tokens_ = TokenPair {};
        ticket_ = Ticket {};
        licenseKey_.clear();
    }

    WakeWorker();
}

AuthSnapshot AuthLicenseManager::GetAuthSnapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return AuthSnapshot { authenticated_, username_ };
}

unsigned long AuthLicenseManager::GetLicenseSnapshot(LicenseSnapshot& snapshot)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!authenticated_) {
        return kRpcNotAuthenticated;
    }

    if (!ticket_.present || ticket_.expiresAtUnix <= TimePointToUnix(std::chrono::system_clock::now())) {
        snapshot = LicenseSnapshot {};
        return kRpcNoLicense;
    }

    snapshot = LicenseSnapshot { !ticket_.blocked, ticket_.blocked, ticket_.expiresAtUnix };
    return ticket_.blocked ? kRpcNoLicense : kRpcSuccess;
}

unsigned long AuthLicenseManager::Activate(const std::wstring& licenseKey, LicenseSnapshot& snapshot)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!authenticated_) {
        return kRpcNotAuthenticated;
    }

    const std::wstring fingerprint = DeviceFingerprint();
    const std::wstring deviceName = DeviceName();
    const std::string body =
        "{\"licenseKey\":\"" + EscapeJson(licenseKey) +
        "\",\"deviceFingerprint\":\"" + EscapeJson(fingerprint) +
        "\",\"deviceName\":\"" + EscapeJson(deviceName) + "\"}";

    lock.unlock();
    std::string response;
    const bool ok = HttpPost(L"/api/licenses/activate", body, {}, response);
    lock.lock();

    if (!ok) {
        return kRpcActivationFailed;
    }

    licenseKey_ = licenseKey;
    unsigned long result = StoreTicketResponseLocked(response);
    if (result != kRpcSuccess) {
        result = CheckLicenseLocked();
    }

    if (result != kRpcSuccess) {
        ticket_ = Ticket {};
        licenseKey_.clear();
        return result;
    }

    snapshot = LicenseSnapshot { !ticket_.blocked, ticket_.blocked, ticket_.expiresAtUnix };
    WakeWorker();
    return ticket_.blocked ? kRpcNoLicense : kRpcSuccess;
}

unsigned long AuthLicenseManager::EnsureAntivirusAvailable() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!authenticated_) {
        return kRpcNotAuthenticated;
    }

    if (!ticket_.present || ticket_.blocked || ticket_.expiresAtUnix <= TimePointToUnix(std::chrono::system_clock::now())) {
        return kRpcNoLicense;
    }

    return kRpcSuccess;
}

void AuthLicenseManager::WorkerLoop()
{
    std::unique_lock<std::mutex> lock(mutex_);

    while (!stopping_) {
        auto nextWake = std::chrono::system_clock::now() + std::chrono::hours(24);

        if (authenticated_) {
            const auto tokenRefresh = CalculateRefreshTime(tokens_.accessExpiresAt, std::chrono::seconds(30));
            nextWake = (std::min)(nextWake, tokenRefresh);

            if (ticket_.present) {
                nextWake = (std::min)(nextWake, ticket_.refreshAt);
            }
        }

        workerWake_.wait_until(lock, nextWake);
        if (stopping_) {
            break;
        }

        const auto now = std::chrono::system_clock::now();
        if (authenticated_ && tokens_.accessExpiresAt <= now + kRefreshSafetyMargin) {
            static_cast<void>(RefreshTokensLocked());
        }

        if (authenticated_ && ticket_.present && ticket_.refreshAt <= now) {
            static_cast<void>(CheckLicenseLocked());
        }
    }
}

void AuthLicenseManager::WakeWorker()
{
    workerWake_.notify_one();
}

unsigned long AuthLicenseManager::RefreshTokensLocked()
{
    if (tokens_.refreshToken.empty()) {
        authenticated_ = false;
        username_.clear();
        tokens_ = TokenPair {};
        ticket_ = Ticket {};
        licenseKey_.clear();
        return kRpcNotAuthenticated;
    }

    const std::wstring refreshToken = tokens_.refreshToken;
    const std::wstring username = username_;
    const std::string body = "{\"refreshToken\":\"" + EscapeJson(refreshToken) + "\"}";

    std::string response;
    const bool ok = HttpPost(L"/api/auth/refresh", body, {}, response);

    if (!ok) {
        return kRpcNetworkError;
    }

    return StoreTokenResponseLocked(response, username);
}

unsigned long AuthLicenseManager::CheckLicenseLocked()
{
    if (licenseKey_.empty()) {
        ticket_ = Ticket {};
        return kRpcNoLicense;
    }

    const std::wstring licenseKey = licenseKey_;
    const std::wstring fingerprint = DeviceFingerprint();
    const std::string body =
        "{\"licenseKey\":\"" + EscapeJson(licenseKey) +
        "\",\"deviceFingerprint\":\"" + EscapeJson(fingerprint) + "\"}";

    std::string response;
    const bool ok = HttpPost(L"/api/licenses/check", body, {}, response);

    if (!ok) {
        ticket_ = Ticket {};
        return kRpcNoLicense;
    }

    return StoreTicketResponseLocked(response);
}

unsigned long AuthLicenseManager::StoreTokenResponseLocked(const std::string& response, const std::wstring& username)
{
    std::string accessToken;
    std::string refreshToken;
    long long accessExpires = 0;
    long long refreshExpires = 0;

    if (!ExtractString(response, "accessToken", accessToken) ||
        !ExtractString(response, "refreshToken", refreshToken) ||
        !ExtractLong(response, "accessExpiresInSeconds", accessExpires) ||
        !ExtractLong(response, "refreshExpiresInSeconds", refreshExpires)) {
        return kRpcInvalidResponse;
    }

    const auto now = std::chrono::system_clock::now();
    tokens_.accessToken = Utf8ToWide(accessToken);
    tokens_.refreshToken = Utf8ToWide(refreshToken);
    tokens_.accessExpiresAt = now + std::chrono::seconds(accessExpires);
    tokens_.refreshExpiresAt = now + std::chrono::seconds(refreshExpires);
    username_ = username;
    authenticated_ = true;
    return kRpcSuccess;
}

unsigned long AuthLicenseManager::StoreTicketResponseLocked(const std::string& response)
{
    std::string expiresAt;
    long long ttlSeconds = 0;
    bool blocked = false;
    long long expiresAtUnix = 0;

    if (!ExtractString(response, "licenseExpiresAt", expiresAt) ||
        !ExtractLong(response, "ticketTtlSeconds", ttlSeconds) ||
        !ExtractBool(response, "blocked", blocked) ||
        !ParseLocalDateTime(expiresAt, expiresAtUnix)) {
        return kRpcInvalidResponse;
    }

    const auto now = std::chrono::system_clock::now();
    const auto ttlRefresh = ttlSeconds > 0 ? now + std::chrono::seconds((std::max<long long>)(15, ttlSeconds - 30)) : now + kFallbackTicketRefresh;
    ticket_.present = true;
    ticket_.blocked = blocked;
    ticket_.expiresAtUnix = expiresAtUnix;
    ticket_.refreshAt = (std::min)(ttlRefresh, UnixToTimePoint(expiresAtUnix) - kRefreshSafetyMargin);
    if (ticket_.refreshAt <= now) {
        ticket_.refreshAt = now + kMinimumRefreshDelay;
    }

    return kRpcSuccess;
}

std::wstring AuthLicenseManager::DeviceFingerprint() const
{
    wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = ARRAYSIZE(computerName);
    if (!GetComputerNameW(computerName, &size)) {
        return L"unknown-device";
    }

    return std::wstring(L"windows-service-") + computerName;
}

std::wstring AuthLicenseManager::DeviceName() const
{
    wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = ARRAYSIZE(computerName);
    if (!GetComputerNameW(computerName, &size)) {
        return L"Windows workstation";
    }

    return computerName;
}

} // namespace trayapp::service
