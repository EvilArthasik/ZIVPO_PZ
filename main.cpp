#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <sddl.h>
#include <ctime>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "common/constants.h"
#include "gui/rpc_client.h"
#include "gui/service_guard.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

constexpr wchar_t kWindowClassName[] = L"TraySampleWindowClass";
constexpr wchar_t kWindowTitle[] = L"Tray Sample App";
constexpr UINT kTrayIconId = 1001;
constexpr UINT WM_TRAYICON = WM_APP + 1;

constexpr UINT IDM_TRAY_OPEN = 2001;
constexpr UINT IDM_TRAY_EXIT = 2002;
constexpr UINT IDM_FILE_EXIT = 3001;
constexpr UINT IDM_FILE_LOGOUT = 3002;
constexpr int kTrayMenuBottomMargin = 12;
constexpr UINT_PTR kLicensePollTimer = 4001;

constexpr int IDC_LOGIN_EDIT = 5001;
constexpr int IDC_PASSWORD_EDIT = 5002;
constexpr int IDC_LOGIN_BUTTON = 5003;
constexpr int IDC_ACTIVATION_EDIT = 5004;
constexpr int IDC_ACTIVATE_BUTTON = 5005;
constexpr int IDC_LOGOUT_BUTTON = 5006;
constexpr int IDC_SCAN_FILE_BUTTON = 5007;
constexpr int IDC_SCAN_DIR_BUTTON = 5008;
constexpr int IDC_SCAN_DRIVES_BUTTON = 5009;
constexpr int IDC_SCHEDULE_BUTTON = 5010;
constexpr int IDC_MONITOR_BUTTON = 5011;
constexpr int IDC_MONITOR_REPORT_BUTTON = 5012;

HINSTANCE g_hInstance = nullptr;
HWND g_mainWindow = nullptr;
UINT g_taskbarCreatedMessage = 0;
HANDLE g_singleInstanceMutex = nullptr;
HICON g_trayIcon = nullptr;
HFONT g_defaultFont = nullptr;

trayapp::gui::AuthState g_authState;
trayapp::gui::LicenseState g_licenseState;
trayapp::gui::AvDatabaseInfo g_avDatabaseInfo;
std::wstring g_statusText;

std::wstring GetCurrentUserSidString() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return L"unknown-user";
    }

    DWORD requiredSize = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &requiredSize);
    if (requiredSize == 0) {
        CloseHandle(token);
        return L"unknown-user";
    }

    std::vector<BYTE> buffer(requiredSize);
    if (!GetTokenInformation(token, TokenUser, buffer.data(), requiredSize, &requiredSize)) {
        CloseHandle(token);
        return L"unknown-user";
    }

    const auto* tokenUser = reinterpret_cast<TOKEN_USER*>(buffer.data());
    LPWSTR sidText = nullptr;
    std::wstring result = L"unknown-user";

    if (ConvertSidToStringSidW(tokenUser->User.Sid, &sidText) && sidText != nullptr) {
        result = sidText;
        LocalFree(sidText);
    }

    CloseHandle(token);
    return result;
}

bool EnsureSingleInstancePerUser() {
    std::wstring mutexName = L"Local\\TraySampleApp_" + GetCurrentUserSidString();
    g_singleInstanceMutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
    if (g_singleInstanceMutex == nullptr) {
        return false;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
        return false;
    }

    return true;
}

void ReleaseSingleInstanceMutex() {
    if (g_singleInstanceMutex != nullptr) {
        ReleaseMutex(g_singleInstanceMutex);
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
    }
}

void ShowMainWindow() {
    if (g_mainWindow == nullptr) {
        return;
    }

    ShowWindow(g_mainWindow, SW_RESTORE);
    ShowWindow(g_mainWindow, SW_SHOW);
    SetForegroundWindow(g_mainWindow);
}

bool AddTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = g_trayIcon;
    lstrcpynW(nid.szTip, L"Tray Sample App", ARRAYSIZE(nid.szTip));

    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        return false;
    }

    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
    return true;
}

void RemoveTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ShowTrayContextMenu(HWND hwnd) {
    POINT cursor{};
    GetCursorPos(&cursor);

    POINT menuPoint = cursor;
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (GetMonitorInfoW(monitor, &monitorInfo)) {
        menuPoint.x = min(menuPoint.x, monitorInfo.rcWork.right - 1);
        menuPoint.y = min(menuPoint.y, monitorInfo.rcWork.bottom - kTrayMenuBottomMargin);
    }

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    AppendMenuW(menu, MF_STRING, IDM_TRAY_OPEN, L"Открыть");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, L"Выход");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RIGHTALIGN, menuPoint.x, menuPoint.y, 0, hwnd, nullptr);
    DestroyMenu(menu);

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_SETFOCUS, &nid);
}

void BuildMainMenu(HWND hwnd) {
    HMENU menuBar = CreateMenu();
    HMENU fileMenu = CreatePopupMenu();

    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_LOGOUT, L"Выйти из аккаунта");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_EXIT, L"Выход");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"Файл");

    SetMenu(hwnd, menuBar);
}

HWND AddControl(HWND parent, const wchar_t* className, const wchar_t* text, DWORD style, int x, int y, int width, int height, int id) {
    HWND control = CreateWindowExW(
        0,
        className,
        text,
        WS_CHILD | WS_VISIBLE | style,
        x,
        y,
        width,
        height,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        g_hInstance,
        nullptr);

    if (control != nullptr && g_defaultFont != nullptr) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_defaultFont), TRUE);
    }

    return control;
}

std::wstring FormatUnixDate(long long unixTime) {
    if (unixTime <= 0) {
        return L"неизвестно";
    }

    std::time_t raw = static_cast<std::time_t>(unixTime);
    std::tm localTime {};
    if (localtime_s(&localTime, &raw) != 0) {
        return L"неизвестно";
    }

    wchar_t buffer[64] = {};
    if (wcsftime(buffer, ARRAYSIZE(buffer), L"%d.%m.%Y %H:%M", &localTime) == 0) {
        return L"неизвестно";
    }

    return buffer;
}

void ClearMainContent(HWND hwnd) {
    HWND child = GetWindow(hwnd, GW_CHILD);
    while (child != nullptr) {
        HWND next = GetWindow(child, GW_HWNDNEXT);
        DestroyWindow(child);
        child = next;
    }
}

std::wstring FormatScanReport(const trayapp::gui::ScanReport& report) {
    std::wstringstream stream;
    stream << L"Проверено файлов: " << report.scannedFiles
           << L", заражено: " << report.infectedFiles << L". ";
    if (!report.summary.empty()) {
        stream << report.summary;
    } else {
        stream << (report.malicious ? L"Найдены угрозы." : L"Угроз не найдено.");
    }
    return stream.str();
}

std::wstring PickFile(HWND hwnd) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW dialog {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFile = path;
    dialog.nMaxFile = ARRAYSIZE(path);
    dialog.lpstrFilter = L"Все файлы\0*.*\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&dialog) ? path : L"";
}

std::wstring PickDirectory(HWND hwnd) {
    BROWSEINFOW browse {};
    browse.hwndOwner = hwnd;
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    browse.lpszTitle = L"Выберите директорию";

    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&browse);
    if (item == nullptr) {
        return {};
    }

    wchar_t path[MAX_PATH] = {};
    const bool ok = SHGetPathFromIDListW(item, path) != FALSE;
    CoTaskMemFree(item);
    return ok ? path : L"";
}

std::wstring WindowText(HWND hwnd, int id) {
    HWND control = GetDlgItem(hwnd, id);
    if (control == nullptr) {
        return {};
    }

    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

void CreateLoginContent(HWND hwnd) {
    AddControl(hwnd, L"STATIC", L"Вход в учетную запись", 0, 20, 20, 300, 24, 0);
    AddControl(hwnd, L"STATIC", L"Логин", 0, 20, 58, 120, 22, 0);
    AddControl(hwnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 150, 55, 260, 24, IDC_LOGIN_EDIT);
    AddControl(hwnd, L"STATIC", L"Пароль", 0, 20, 92, 120, 22, 0);
    AddControl(hwnd, L"EDIT", L"", WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL, 150, 89, 260, 24, IDC_PASSWORD_EDIT);
    AddControl(hwnd, L"BUTTON", L"Войти", BS_PUSHBUTTON, 150, 125, 120, 30, IDC_LOGIN_BUTTON);

    if (!g_statusText.empty()) {
        AddControl(hwnd, L"STATIC", g_statusText.c_str(), 0, 20, 170, 460, 42, 0);
    }
}

void CreateActivationContent(HWND hwnd) {
    const std::wstring header = L"Пользователь: " + g_authState.username;
    AddControl(hwnd, L"STATIC", header.c_str(), 0, 20, 20, 420, 24, 0);
    AddControl(hwnd, L"BUTTON", L"Выйти", BS_PUSHBUTTON, 370, 18, 90, 28, IDC_LOGOUT_BUTTON);
    AddControl(hwnd, L"STATIC", L"Активная лицензия отсутствует. Антивирусная функциональность заблокирована.", 0, 20, 62, 460, 38, 0);
    AddControl(hwnd, L"STATIC", L"Код активации", 0, 20, 112, 120, 22, 0);
    AddControl(hwnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 150, 109, 260, 24, IDC_ACTIVATION_EDIT);
    AddControl(hwnd, L"BUTTON", L"Активировать", BS_PUSHBUTTON, 150, 145, 140, 30, IDC_ACTIVATE_BUTTON);

    if (!g_statusText.empty()) {
        AddControl(hwnd, L"STATIC", g_statusText.c_str(), 0, 20, 190, 460, 42, 0);
    }
}

void CreateLicensedContent(HWND hwnd) {
    const std::wstring user = L"Пользователь: " + g_authState.username;
    const std::wstring license = L"Лицензия активна до: " + FormatUnixDate(g_licenseState.expiresAtUnix);
    const std::wstring database = L"Антивирусные базы: " +
        FormatUnixDate(g_avDatabaseInfo.releaseDateUnix) +
        L", записей: " + std::to_wstring(g_avDatabaseInfo.recordCount);

    AddControl(hwnd, L"STATIC", user.c_str(), 0, 20, 20, 350, 24, 0);
    AddControl(hwnd, L"BUTTON", L"Выйти", BS_PUSHBUTTON, 370, 18, 90, 28, IDC_LOGOUT_BUTTON);
    AddControl(hwnd, L"STATIC", license.c_str(), 0, 20, 58, 420, 24, 0);
    AddControl(hwnd, L"STATIC", database.c_str(), 0, 20, 88, 460, 24, 0);
    AddControl(hwnd, L"BUTTON", L"Сканировать файл", BS_PUSHBUTTON, 20, 125, 150, 30, IDC_SCAN_FILE_BUTTON);
    AddControl(hwnd, L"BUTTON", L"Сканировать папку", BS_PUSHBUTTON, 180, 125, 160, 30, IDC_SCAN_DIR_BUTTON);
    AddControl(hwnd, L"BUTTON", L"Все диски", BS_PUSHBUTTON, 350, 125, 110, 30, IDC_SCAN_DRIVES_BUTTON);
    AddControl(hwnd, L"BUTTON", L"Расписание 60 мин", BS_PUSHBUTTON, 20, 165, 150, 30, IDC_SCHEDULE_BUTTON);
    AddControl(hwnd, L"BUTTON", L"Мониторинг папки", BS_PUSHBUTTON, 180, 165, 160, 30, IDC_MONITOR_BUTTON);
    AddControl(hwnd, L"BUTTON", L"Фоновые итоги", BS_PUSHBUTTON, 350, 165, 110, 30, IDC_MONITOR_REPORT_BUTTON);

    if (!g_statusText.empty()) {
        AddControl(hwnd, L"STATIC", g_statusText.c_str(), 0, 20, 210, 460, 86, 0);
    }
}

void RenderMainContent(HWND hwnd) {
    ClearMainContent(hwnd);

    if (!g_authState.authenticated) {
        CreateLoginContent(hwnd);
        return;
    }

    if (!g_licenseState.licensed) {
        CreateActivationContent(hwnd);
        return;
    }

    CreateLicensedContent(hwnd);
}

void RefreshStartupState(HWND hwnd) {
    g_statusText.clear();
    const unsigned long authStatus = trayapp::gui::GetAuthState(g_authState);
    if (authStatus != trayapp::gui::kRpcSuccess) {
        g_statusText = trayapp::gui::DescribeRpcStatus(authStatus);
    }

    if (g_authState.authenticated) {
        const unsigned long licenseStatus = trayapp::gui::GetLicenseState(g_licenseState);
        if (licenseStatus != trayapp::gui::kRpcSuccess) {
            g_licenseState = {};
            if (licenseStatus != trayapp::gui::kRpcNoLicense) {
                g_statusText = trayapp::gui::DescribeRpcStatus(licenseStatus);
            }
        }

        if (g_licenseState.licensed) {
            trayapp::gui::AvDatabaseInfo info;
            if (trayapp::gui::GetAvDatabaseInfo(info) == trayapp::gui::kRpcSuccess) {
                g_avDatabaseInfo = info;
            }
        }
    } else {
        g_licenseState = {};
        g_avDatabaseInfo = {};
    }

    RenderMainContent(hwnd);
}

void PollLicenseState(HWND hwnd) {
    if (!g_authState.authenticated) {
        return;
    }

    trayapp::gui::LicenseState state;
    const unsigned long status = trayapp::gui::GetLicenseState(state);
    if (status == trayapp::gui::kRpcSuccess) {
        g_licenseState = state;
        g_statusText.clear();
    } else if (status == trayapp::gui::kRpcNoLicense) {
        g_licenseState = {};
        g_avDatabaseInfo = {};
    } else {
        g_statusText = trayapp::gui::DescribeRpcStatus(status);
    }

    if (g_licenseState.licensed) {
        trayapp::gui::AvDatabaseInfo info;
        if (trayapp::gui::GetAvDatabaseInfo(info) == trayapp::gui::kRpcSuccess) {
            g_avDatabaseInfo = info;
        }
    }

    RenderMainContent(hwnd);
}

void HandleLogin(HWND hwnd) {
    trayapp::gui::AuthState state;
    const unsigned long status = trayapp::gui::Login(WindowText(hwnd, IDC_LOGIN_EDIT), WindowText(hwnd, IDC_PASSWORD_EDIT), state);
    if (status != trayapp::gui::kRpcSuccess) {
        g_authState = {};
        g_licenseState = {};
        g_statusText = trayapp::gui::DescribeRpcStatus(status);
        RenderMainContent(hwnd);
        return;
    }

    g_authState = state;
    g_licenseState = {};
    g_statusText.clear();
    PollLicenseState(hwnd);
}

void HandleActivate(HWND hwnd) {
    trayapp::gui::LicenseState state;
    const unsigned long status = trayapp::gui::ActivateProduct(WindowText(hwnd, IDC_ACTIVATION_EDIT), state);
    if (status != trayapp::gui::kRpcSuccess) {
        g_licenseState = {};
        g_statusText = trayapp::gui::DescribeRpcStatus(status);
        RenderMainContent(hwnd);
        return;
    }

    g_licenseState = state;
    trayapp::gui::AvDatabaseInfo info;
    if (trayapp::gui::GetAvDatabaseInfo(info) == trayapp::gui::kRpcSuccess) {
        g_avDatabaseInfo = info;
    }
    g_statusText.clear();
    RenderMainContent(hwnd);
}

void HandleLogout(HWND hwnd) {
    trayapp::gui::Logout();
    g_authState = {};
    g_licenseState = {};
    g_avDatabaseInfo = {};
    g_statusText.clear();
    RenderMainContent(hwnd);
}

void HandleScanFile(HWND hwnd) {
    const std::wstring path = PickFile(hwnd);
    if (path.empty()) {
        return;
    }

    trayapp::gui::ScanReport report;
    const unsigned long status = trayapp::gui::ScanFile(path, report);
    g_statusText = status == trayapp::gui::kRpcSuccess ? FormatScanReport(report) : trayapp::gui::DescribeRpcStatus(status);
    if (status != trayapp::gui::kRpcSuccess) {
        g_licenseState = {};
    }
    RenderMainContent(hwnd);
}

void HandleScanDirectory(HWND hwnd) {
    const std::wstring path = PickDirectory(hwnd);
    if (path.empty()) {
        return;
    }

    trayapp::gui::ScanReport report;
    const unsigned long status = trayapp::gui::ScanDirectory(path, report);
    g_statusText = status == trayapp::gui::kRpcSuccess ? FormatScanReport(report) : trayapp::gui::DescribeRpcStatus(status);
    if (status != trayapp::gui::kRpcSuccess) {
        g_licenseState = {};
    }
    RenderMainContent(hwnd);
}

void HandleScanFixedDrives(HWND hwnd) {
    trayapp::gui::ScanReport report;
    const unsigned long status = trayapp::gui::ScanFixedDrives(report);
    g_statusText = status == trayapp::gui::kRpcSuccess ? FormatScanReport(report) : trayapp::gui::DescribeRpcStatus(status);
    if (status != trayapp::gui::kRpcSuccess) {
        g_licenseState = {};
    }
    RenderMainContent(hwnd);
}

void HandleSchedule(HWND hwnd) {
    const unsigned long status = trayapp::gui::ConfigureSchedule(60);
    g_statusText = status == trayapp::gui::kRpcSuccess
        ? L"Сканирование по расписанию включено: каждые 60 минут."
        : trayapp::gui::DescribeRpcStatus(status);
    if (status != trayapp::gui::kRpcSuccess) {
        g_licenseState = {};
    }
    RenderMainContent(hwnd);
}

void HandleMonitorDirectory(HWND hwnd) {
    const std::wstring path = PickDirectory(hwnd);
    if (path.empty()) {
        return;
    }

    const unsigned long status = trayapp::gui::AddMonitorDirectory(path);
    g_statusText = status == trayapp::gui::kRpcSuccess
        ? L"Мониторинг директории включен: " + path
        : trayapp::gui::DescribeRpcStatus(status);
    if (status != trayapp::gui::kRpcSuccess) {
        g_licenseState = {};
    }
    RenderMainContent(hwnd);
}

void HandleMonitorReport(HWND hwnd) {
    trayapp::gui::ScanReport report;
    unsigned long status = trayapp::gui::GetMonitorScanReport(report);
    if (status == trayapp::gui::kRpcSuccess && report.scannedFiles == 0) {
        trayapp::gui::ScanReport scheduled;
        status = trayapp::gui::GetScheduledScanReport(scheduled);
        g_statusText = status == trayapp::gui::kRpcSuccess
            ? (scheduled.scannedFiles == 0 ? L"Фоновых результатов пока нет." : FormatScanReport(scheduled))
            : trayapp::gui::DescribeRpcStatus(status);
    } else {
        g_statusText = status == trayapp::gui::kRpcSuccess ? FormatScanReport(report) : trayapp::gui::DescribeRpcStatus(status);
    }
    if (status != trayapp::gui::kRpcSuccess) {
        g_licenseState = {};
    }
    RenderMainContent(hwnd);
}

bool IsHiddenStartupRequested() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return false;
    }

    bool hidden = false;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"/hidden") == 0 ||
            _wcsicmp(argv[i], L"-hidden") == 0 ||
            _wcsicmp(argv[i], L"/background") == 0 ||
            _wcsicmp(argv[i], L"-background") == 0) {
            hidden = true;
            break;
        }
    }

    LocalFree(argv);
    return hidden;
}

bool IsServiceChildMode(std::wstring_view commandLine) {
    return commandLine.find(trayapp::kServiceChildArg) != std::wstring_view::npos;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == g_taskbarCreatedMessage) {
        AddTrayIcon(hwnd);
        return 0;
    }

    switch (message) {
    case WM_CREATE:
        BuildMainMenu(hwnd);
        g_defaultFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        RefreshStartupState(hwnd);
        SetTimer(hwnd, kLicensePollTimer, 30000, nullptr);
        AddTrayIcon(hwnd);
        return 0;

    case WM_COMMAND: {
        const UINT commandId = LOWORD(wParam);
        switch (commandId) {
        case IDM_TRAY_OPEN:
            ShowMainWindow();
            return 0;
        case IDM_TRAY_EXIT:
        case IDM_FILE_EXIT:
            static_cast<void>(trayapp::gui::RequestServiceStop());
            RemoveTrayIcon(hwnd);
            DestroyWindow(hwnd);
            return 0;
        case IDM_FILE_LOGOUT:
            HandleLogout(hwnd);
            return 0;
        case IDC_LOGIN_BUTTON:
            HandleLogin(hwnd);
            return 0;
        case IDC_ACTIVATE_BUTTON:
            HandleActivate(hwnd);
            return 0;
        case IDC_LOGOUT_BUTTON:
            HandleLogout(hwnd);
            return 0;
        case IDC_SCAN_FILE_BUTTON:
            HandleScanFile(hwnd);
            return 0;
        case IDC_SCAN_DIR_BUTTON:
            HandleScanDirectory(hwnd);
            return 0;
        case IDC_SCAN_DRIVES_BUTTON:
            HandleScanFixedDrives(hwnd);
            return 0;
        case IDC_SCHEDULE_BUTTON:
            HandleSchedule(hwnd);
            return 0;
        case IDC_MONITOR_BUTTON:
            HandleMonitorDirectory(hwnd);
            return 0;
        case IDC_MONITOR_REPORT_BUTTON:
            HandleMonitorReport(hwnd);
            return 0;
        default:
            break;
        }
        break;
    }

    case WM_TIMER:
        if (wParam == kLicensePollTimer) {
            PollLicenseState(hwnd);
            return 0;
        }
        break;
    case WM_TRAYICON:
        switch (LOWORD(lParam)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            ShowMainWindow();
            return 0;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayContextMenu(hwnd);
            return 0;
        default:
            break;
        }
        break;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, kLicensePollTimer);
        RemoveTrayIcon(hwnd);
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const std::wstring_view commandLine = GetCommandLineW();
    const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    if (trayapp::gui::CheckServiceStartup() == trayapp::gui::StartupDecision::Exit) {
        if (SUCCEEDED(comInit)) {
            CoUninitialize();
        }
        return 0;
    }

    if (!IsServiceChildMode(commandLine) || !trayapp::gui::IsParentServiceProcess()) {
        if (SUCCEEDED(comInit)) {
            CoUninitialize();
        }
        return 0;
    }

    g_hInstance = instance;
    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

    if (!EnsureSingleInstancePerUser()) {
        if (SUCCEEDED(comInit)) {
            CoUninitialize();
        }
        return 0;
    }

    g_trayIcon = LoadIcon(nullptr, IDI_APPLICATION);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = g_trayIcon;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClassName;

    if (!RegisterClassExW(&wc)) {
        ReleaseSingleInstanceMutex();
        if (SUCCEEDED(comInit)) {
            CoUninitialize();
        }
        return 1;
    }

    g_mainWindow = CreateWindowExW(
        0,
        kWindowClassName,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        520,
        340,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (g_mainWindow == nullptr) {
        ReleaseSingleInstanceMutex();
        if (SUCCEEDED(comInit)) {
            CoUninitialize();
        }
        return 1;
    }

    if (!IsHiddenStartupRequested()) {
        ShowWindow(g_mainWindow, SW_SHOW);
        UpdateWindow(g_mainWindow);
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ReleaseSingleInstanceMutex();
    if (SUCCEEDED(comInit)) {
        CoUninitialize();
    }
    return static_cast<int>(msg.wParam);
}
