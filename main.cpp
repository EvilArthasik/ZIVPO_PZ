#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <sddl.h>
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
constexpr int kTrayMenuBottomMargin = 12;

HINSTANCE g_hInstance = nullptr;
HWND g_mainWindow = nullptr;
UINT g_taskbarCreatedMessage = 0;
HANDLE g_singleInstanceMutex = nullptr;
HICON g_trayIcon = nullptr;

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

    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_EXIT, L"Выход");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"Файл");

    SetMenu(hwnd, menuBar);
}

void CreateMainContent(HWND hwnd) {
    CreateWindowExW(
        0,
        L"STATIC",
        L"Приложение работает в трее.\r\nЗакрытие окна скрывает его, но не завершает приложение.",
        WS_CHILD | WS_VISIBLE,
        20,
        20,
        420,
        60,
        hwnd,
        nullptr,
        g_hInstance,
        nullptr);
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
        CreateMainContent(hwnd);
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
        default:
            break;
        }
        break;
    }

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

    if (trayapp::gui::CheckServiceStartup() == trayapp::gui::StartupDecision::Exit) {
        return 0;
    }

    if (!IsServiceChildMode(commandLine) || !trayapp::gui::IsParentServiceProcess()) {
        return 0;
    }

    g_hInstance = instance;
    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

    if (!EnsureSingleInstancePerUser()) {
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
        220,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (g_mainWindow == nullptr) {
        ReleaseSingleInstanceMutex();
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
    return static_cast<int>(msg.wParam);
}
