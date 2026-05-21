#pragma once

#include <string>

namespace trayapp::gui {

inline constexpr unsigned long kRpcSuccess = 0;
inline constexpr unsigned long kRpcNotAuthenticated = 1;
inline constexpr unsigned long kRpcAuthenticationFailed = 2;
inline constexpr unsigned long kRpcNoLicense = 3;
inline constexpr unsigned long kRpcActivationFailed = 4;
inline constexpr unsigned long kRpcNetworkError = 5;
inline constexpr unsigned long kRpcInvalidResponse = 6;

struct AuthState {
    bool authenticated = false;
    std::wstring username;
};

struct LicenseState {
    bool licensed = false;
    bool blocked = false;
    long long expiresAtUnix = 0;
};

[[nodiscard]] bool RequestServiceStop();
[[nodiscard]] unsigned long GetAuthState(AuthState& state);
[[nodiscard]] unsigned long Login(const std::wstring& username, const std::wstring& password, AuthState& state);
void Logout();
[[nodiscard]] unsigned long GetLicenseState(LicenseState& state);
[[nodiscard]] unsigned long ActivateProduct(const std::wstring& licenseKey, LicenseState& state);
[[nodiscard]] unsigned long EnsureAntivirusAvailable();
[[nodiscard]] std::wstring DescribeRpcStatus(unsigned long status);

}
