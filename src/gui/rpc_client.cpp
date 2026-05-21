#include "gui/rpc_client.h"

#include "common/constants.h"

#include <rpc.h>

#include <cstdlib>
#include <string>

#include "rpc/tray_control_api.h"

extern "C" void* __RPC_USER midl_user_allocate(size_t size)
{
    return std::malloc(size);
}

extern "C" void __RPC_USER midl_user_free(void* pointer)
{
    std::free(pointer);
}

namespace trayapp::gui {
namespace {

[[nodiscard]] RPC_STATUS CreateBinding(handle_t* binding)
{
    RPC_WSTR stringBinding = nullptr;
    RPC_STATUS status = RpcStringBindingComposeW(
        nullptr,
        reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(L"ncalrpc")),
        nullptr,
        reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(trayapp::kRpcEndpoint)),
        nullptr,
        &stringBinding);

    if (status != RPC_S_OK) {
        return status;
    }

    status = RpcBindingFromStringBindingW(stringBinding, binding);
    RpcStringFreeW(&stringBinding);
    return status;
}

void CopyAuthState(const TrayAuthState& rpcState, AuthState& state)
{
    state.authenticated = rpcState.authenticated != 0;
    state.username = rpcState.username != nullptr ? rpcState.username : L"";
}

void CopyLicenseState(const TrayLicenseState& rpcState, LicenseState& state)
{
    state.licensed = rpcState.licensed != 0;
    state.blocked = rpcState.blocked != 0;
    state.expiresAtUnix = rpcState.expiresAtUnix;
}

void CopyDatabaseInfo(const TrayAvDatabaseInfo& rpcInfo, AvDatabaseInfo& info)
{
    info.releaseDateUnix = rpcInfo.releaseDateUnix;
    info.recordCount = rpcInfo.recordCount;
}

void CopyScanReport(const TrayScanReport& rpcReport, ScanReport& report)
{
    report.malicious = rpcReport.malicious != 0;
    report.scannedFiles = rpcReport.scannedFiles;
    report.infectedFiles = rpcReport.infectedFiles;
    report.summary = rpcReport.summary != nullptr ? rpcReport.summary : L"";
}

void FreeBinding(handle_t* binding)
{
    if (binding != nullptr && *binding != nullptr) {
        RpcBindingFree(binding);
    }
}

} // namespace

[[nodiscard]] bool RequestServiceStop()
{
    handle_t binding = nullptr;
    if (CreateBinding(&binding) != RPC_S_OK) {
        return false;
    }

    RPC_STATUS status = RPC_S_OK;
    RpcTryExcept
    {
        TrayStopService(binding);
        status = RPC_S_OK;
    }
    RpcExcept(1)
    {
        status = RpcExceptionCode();
    }
    RpcEndExcept

    FreeBinding(&binding);
    return status == RPC_S_OK;
}

unsigned long GetAuthState(AuthState& state)
{
    handle_t binding = nullptr;
    if (CreateBinding(&binding) != RPC_S_OK) {
        return kRpcNetworkError;
    }

    TrayAuthState rpcState {};
    unsigned long result = kRpcNetworkError;

    RpcTryExcept
    {
        result = TrayGetAuthState(binding, &rpcState);
    }
    RpcExcept(1)
    {
        result = kRpcNetworkError;
    }
    RpcEndExcept

    CopyAuthState(rpcState, state);
    midl_user_free(rpcState.username);
    FreeBinding(&binding);
    return result;
}

unsigned long Login(const std::wstring& username, const std::wstring& password, AuthState& state)
{
    handle_t binding = nullptr;
    if (CreateBinding(&binding) != RPC_S_OK) {
        return kRpcNetworkError;
    }

    TrayAuthState rpcState {};
    unsigned long result = kRpcNetworkError;

    RpcTryExcept
    {
        result = TrayLogin(
            binding,
            const_cast<wchar_t*>(username.c_str()),
            const_cast<wchar_t*>(password.c_str()),
            &rpcState);
    }
    RpcExcept(1)
    {
        result = kRpcNetworkError;
    }
    RpcEndExcept

    CopyAuthState(rpcState, state);
    midl_user_free(rpcState.username);
    FreeBinding(&binding);
    return result;
}

void Logout()
{
    handle_t binding = nullptr;
    if (CreateBinding(&binding) != RPC_S_OK) {
        return;
    }

    RpcTryExcept
    {
        TrayLogout(binding);
    }
    RpcExcept(1)
    {
    }
    RpcEndExcept

    FreeBinding(&binding);
}

unsigned long GetLicenseState(LicenseState& state)
{
    handle_t binding = nullptr;
    if (CreateBinding(&binding) != RPC_S_OK) {
        return kRpcNetworkError;
    }

    TrayLicenseState rpcState {};
    unsigned long result = kRpcNetworkError;

    RpcTryExcept
    {
        result = TrayGetLicenseState(binding, &rpcState);
    }
    RpcExcept(1)
    {
        result = kRpcNetworkError;
    }
    RpcEndExcept

    CopyLicenseState(rpcState, state);
    FreeBinding(&binding);
    return result;
}

unsigned long ActivateProduct(const std::wstring& licenseKey, LicenseState& state)
{
    handle_t binding = nullptr;
    if (CreateBinding(&binding) != RPC_S_OK) {
        return kRpcNetworkError;
    }

    TrayLicenseState rpcState {};
    unsigned long result = kRpcNetworkError;

    RpcTryExcept
    {
        result = TrayActivateProduct(binding, const_cast<wchar_t*>(licenseKey.c_str()), &rpcState);
    }
    RpcExcept(1)
    {
        result = kRpcNetworkError;
    }
    RpcEndExcept

    CopyLicenseState(rpcState, state);
    FreeBinding(&binding);
    return result;
}

unsigned long EnsureAntivirusAvailable()
{
    handle_t binding = nullptr;
    if (CreateBinding(&binding) != RPC_S_OK) {
        return kRpcNetworkError;
    }

    unsigned long result = kRpcNetworkError;
    RpcTryExcept
    {
        result = TrayEnsureAntivirusAvailable(binding);
    }
    RpcExcept(1)
    {
        result = kRpcNetworkError;
    }
    RpcEndExcept

    FreeBinding(&binding);
    return result;
}

unsigned long GetAvDatabaseInfo(AvDatabaseInfo& info)
{
    handle_t binding = nullptr;
    if (CreateBinding(&binding) != RPC_S_OK) {
        return kRpcNetworkError;
    }

    TrayAvDatabaseInfo rpcInfo {};
    unsigned long result = kRpcNetworkError;
    RpcTryExcept
    {
        result = TrayGetAvDatabaseInfo(binding, &rpcInfo);
    }
    RpcExcept(1)
    {
        result = kRpcNetworkError;
    }
    RpcEndExcept

    CopyDatabaseInfo(rpcInfo, info);
    FreeBinding(&binding);
    return result;
}

unsigned long ScanFile(const std::wstring& path, ScanReport& report)
{
    handle_t binding = nullptr;
    if (CreateBinding(&binding) != RPC_S_OK) {
        return kRpcNetworkError;
    }

    TrayScanReport rpcReport {};
    unsigned long result = kRpcNetworkError;
    RpcTryExcept
    {
        result = TrayScanFile(binding, const_cast<wchar_t*>(path.c_str()), &rpcReport);
    }
    RpcExcept(1)
    {
        result = kRpcNetworkError;
    }
    RpcEndExcept

    CopyScanReport(rpcReport, report);
    midl_user_free(rpcReport.summary);
    FreeBinding(&binding);
    return result;
}

unsigned long ScanDirectory(const std::wstring& path, ScanReport& report)
{
    handle_t binding = nullptr;
    if (CreateBinding(&binding) != RPC_S_OK) {
        return kRpcNetworkError;
    }

    TrayScanReport rpcReport {};
    unsigned long result = kRpcNetworkError;
    RpcTryExcept
    {
        result = TrayScanDirectory(binding, const_cast<wchar_t*>(path.c_str()), &rpcReport);
    }
    RpcExcept(1)
    {
        result = kRpcNetworkError;
    }
    RpcEndExcept

    CopyScanReport(rpcReport, report);
    midl_user_free(rpcReport.summary);
    FreeBinding(&binding);
    return result;
}

unsigned long ScanFixedDrives(ScanReport& report)
{
    handle_t binding = nullptr;
    if (CreateBinding(&binding) != RPC_S_OK) {
        return kRpcNetworkError;
    }

    TrayScanReport rpcReport {};
    unsigned long result = kRpcNetworkError;
    RpcTryExcept
    {
        result = TrayScanFixedDrives(binding, &rpcReport);
    }
    RpcExcept(1)
    {
        result = kRpcNetworkError;
    }
    RpcEndExcept

    CopyScanReport(rpcReport, report);
    midl_user_free(rpcReport.summary);
    FreeBinding(&binding);
    return result;
}

unsigned long ConfigureSchedule(unsigned long intervalMinutes)
{
    handle_t binding = nullptr;
    if (CreateBinding(&binding) != RPC_S_OK) {
        return kRpcNetworkError;
    }

    unsigned long result = kRpcNetworkError;
    RpcTryExcept
    {
        result = TrayConfigureSchedule(binding, intervalMinutes);
    }
    RpcExcept(1)
    {
        result = kRpcNetworkError;
    }
    RpcEndExcept

    FreeBinding(&binding);
    return result;
}

unsigned long GetScheduledScanReport(ScanReport& report)
{
    handle_t binding = nullptr;
    if (CreateBinding(&binding) != RPC_S_OK) {
        return kRpcNetworkError;
    }

    TrayScanReport rpcReport {};
    unsigned long result = kRpcNetworkError;
    RpcTryExcept
    {
        result = TrayGetScheduledScanReport(binding, &rpcReport);
    }
    RpcExcept(1)
    {
        result = kRpcNetworkError;
    }
    RpcEndExcept

    CopyScanReport(rpcReport, report);
    midl_user_free(rpcReport.summary);
    FreeBinding(&binding);
    return result;
}

unsigned long AddMonitorDirectory(const std::wstring& path)
{
    handle_t binding = nullptr;
    if (CreateBinding(&binding) != RPC_S_OK) {
        return kRpcNetworkError;
    }

    unsigned long result = kRpcNetworkError;
    RpcTryExcept
    {
        result = TrayAddMonitorDirectory(binding, const_cast<wchar_t*>(path.c_str()));
    }
    RpcExcept(1)
    {
        result = kRpcNetworkError;
    }
    RpcEndExcept

    FreeBinding(&binding);
    return result;
}

unsigned long GetMonitorScanReport(ScanReport& report)
{
    handle_t binding = nullptr;
    if (CreateBinding(&binding) != RPC_S_OK) {
        return kRpcNetworkError;
    }

    TrayScanReport rpcReport {};
    unsigned long result = kRpcNetworkError;
    RpcTryExcept
    {
        result = TrayGetMonitorScanReport(binding, &rpcReport);
    }
    RpcExcept(1)
    {
        result = kRpcNetworkError;
    }
    RpcEndExcept

    CopyScanReport(rpcReport, report);
    midl_user_free(rpcReport.summary);
    FreeBinding(&binding);
    return result;
}

std::wstring DescribeRpcStatus(unsigned long status)
{
    switch (status) {
    case kRpcSuccess:
        return L"Операция выполнена.";
    case kRpcNotAuthenticated:
        return L"Пользователь не аутентифицирован.";
    case kRpcAuthenticationFailed:
        return L"Не удалось войти. Проверьте логин, пароль и доступность сервера.";
    case kRpcNoLicense:
        return L"Активная лицензия отсутствует.";
    case kRpcActivationFailed:
        return L"Не удалось активировать продукт. Проверьте код активации.";
    case kRpcNetworkError:
        return L"Служба или сервер недоступны.";
    case kRpcInvalidResponse:
        return L"Сервер вернул неожиданный ответ.";
    default:
        return L"Неизвестная ошибка.";
    }
}

}
