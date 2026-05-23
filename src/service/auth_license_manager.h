#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace trayapp::service {

inline constexpr unsigned long kRpcSuccess = 0;
inline constexpr unsigned long kRpcNotAuthenticated = 1;
inline constexpr unsigned long kRpcAuthenticationFailed = 2;
inline constexpr unsigned long kRpcNoLicense = 3;
inline constexpr unsigned long kRpcActivationFailed = 4;
inline constexpr unsigned long kRpcNetworkError = 5;
inline constexpr unsigned long kRpcInvalidResponse = 6;

struct AuthSnapshot {
    bool authenticated = false;
    std::wstring username;
};

struct LicenseSnapshot {
    bool licensed = false;
    bool blocked = false;
    long long expiresAtUnix = 0;
};

class AuthLicenseManager {
public:
    AuthLicenseManager();
    ~AuthLicenseManager();

    AuthLicenseManager(const AuthLicenseManager&) = delete;
    AuthLicenseManager& operator=(const AuthLicenseManager&) = delete;

    void Start();
    void Stop();

    unsigned long Login(const std::wstring& username, const std::wstring& password, AuthSnapshot& snapshot);
    void Logout();
    AuthSnapshot GetAuthSnapshot() const;

    unsigned long GetLicenseSnapshot(LicenseSnapshot& snapshot);
    unsigned long Activate(const std::wstring& licenseKey, LicenseSnapshot& snapshot);
    unsigned long EnsureAntivirusAvailable() const;

private:
    struct TokenPair {
        std::wstring accessToken;
        std::wstring refreshToken;
        std::chrono::system_clock::time_point accessExpiresAt = {};
        std::chrono::system_clock::time_point refreshExpiresAt = {};
    };

    struct Ticket {
        bool present = false;
        bool blocked = false;
        long long expiresAtUnix = 0;
        std::chrono::system_clock::time_point refreshAt = {};
    };

    void WorkerLoop();
    void WakeWorker();

    unsigned long RefreshTokensLocked();
    unsigned long CheckLicenseLocked();
    unsigned long StoreTokenResponseLocked(const std::string& response, const std::wstring& username);
    unsigned long StoreTicketResponseLocked(const std::string& response);

    std::wstring DeviceFingerprint() const;
    std::wstring DeviceName() const;

    mutable std::mutex mutex_;
    std::condition_variable workerWake_;
    std::thread worker_;
    std::atomic_bool stopping_ = false;

    bool authenticated_ = false;
    std::wstring username_;
    TokenPair tokens_;

    std::wstring licenseKey_;
    Ticket ticket_;
};

} // namespace trayapp::service
