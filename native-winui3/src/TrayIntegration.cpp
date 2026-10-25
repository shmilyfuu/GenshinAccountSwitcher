#include "pch.h"
#include "MainWindow.xaml.h"
#include "resource.h"

#include <shellapi.h>
#include <commctrl.h>
#include <algorithm>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Comctl32.lib")

namespace
{
    constexpr UINT kTrayIconId = 1;
    constexpr UINT kTrayCallback = WM_APP + 61;
    constexpr UINT_PTR kTraySubclassId = 0x474153;
    constexpr UINT kOpenPanelId = 0x6100;
    constexpr UINT kLaunchGameId = 0x6101;
    constexpr UINT kExitAppId = 0x6102;
    constexpr UINT kAccountBaseId = 0x6200;
    constexpr UINT kMaxTrayAccounts = 2048;

    std::wstring EscapeMenuText(std::wstring const& text)
    {
        std::wstring escaped;
        for (wchar_t c : text)
        {
            if (c == L'&') escaped.push_back(L'&');
            escaped.push_back(c);
        }
        return escaped;
    }
}

namespace winrt::GenshinAccountSwitcher::implementation
{
    bool MainWindow::InitializeTray()
    {
        auto hwnd = WindowHandle();
        if (!hwnd) return false;
        if (m_trayWindow) return m_trayIconAdded;

        m_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
        if (!SetWindowSubclass(hwnd, &MainWindow::WindowSubclassProc,
            kTraySubclassId, reinterpret_cast<DWORD_PTR>(this)))
            return false;

        m_trayWindow = hwnd;
        AddTrayIcon();
        return m_trayIconAdded;
    }

    void MainWindow::AddTrayIcon()
    {
        if (!m_trayWindow) return;
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = m_trayWindow;
        data.uID = kTrayIconId;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        data.uCallbackMessage = kTrayCallback;
        data.hIcon = reinterpret_cast<HICON>(LoadImageW(
            GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON),
            IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
        wcscpy_s(data.szTip, L"原神账号管理");

        m_trayIconAdded = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
        if (m_trayIconAdded)
        {
            data.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &data);
        }
    }

    void MainWindow::RemoveTrayIcon()
    {
        if (!m_trayWindow || !m_trayIconAdded) return;
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = m_trayWindow;
        data.uID = kTrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &data);
        m_trayIconAdded = false;
    }

    void MainWindow::HideToTray()
    {
        if (!m_trayWindow || !m_trayIconAdded) return;
        m_trayHidden = true;
        ShowWindow(m_trayWindow, SW_HIDE);
    }

    void MainWindow::RestoreFromTray()
    {
        auto hwnd = WindowHandle();
        if (!hwnd) return;
        m_trayHidden = false;
        ShowWindow(hwnd, SW_RESTORE);
        ShowWindow(hwnd, SW_SHOW);
        Activate();
        SetForegroundWindow(hwnd);
        UpdateButtonStates();
    }

    void MainWindow::ShowTrayMenu()
    {
        if (!m_trayWindow) return;
        HMENU menu = CreatePopupMenu();
        if (!menu) return;

        const bool allowed = m_initialized && !IsInteractionBlocked();
        AppendMenuW(menu, MF_STRING, kOpenPanelId, L"打开面板");
        AppendMenuW(menu, MF_STRING | (allowed ? 0 : MF_GRAYED), kLaunchGameId, L"启动游戏");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

        int currentIndex = -1;
        if (m_initialized)
        {
            try
            {
                auto probe = m_core.ProbeCurrent();
                auto state = ClassifyProbe(probe, m_core.IsGameRunning());
                if (state.matchKind == gas::CurrentMatchKind::ExactCredential ||
                    state.matchKind == gas::CurrentMatchKind::UniqueUidCredentialChanged)
                    currentIndex = state.accountIndex;
            }
            catch (...) {}
        }

        if (m_initialized)
        {
            auto const& accounts = m_core.Accounts();
            auto count = (std::min)(accounts.size(), static_cast<size_t>(kMaxTrayAccounts));
            for (size_t i = 0; i < count; ++i)
            {
                UINT id = kAccountBaseId + static_cast<UINT>(i);
                std::wstring title = EscapeMenuText(accounts[i].name + L"  UID " + accounts[i].uid);
                AppendMenuW(menu, MF_STRING | (allowed ? 0 : MF_GRAYED), id, title.c_str());
                if (static_cast<int>(i) == currentIndex)
                    CheckMenuItem(menu, id, MF_BYCOMMAND | MF_CHECKED);
            }
            if (count == 0)
                AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"暂无已保存账号");
        }

        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kExitAppId, L"退出");
        POINT point{};
        GetCursorPos(&point);
        SetForegroundWindow(m_trayWindow);
        UINT command = TrackPopupMenuEx(menu,
            TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
            point.x, point.y, m_trayWindow, nullptr);
        DestroyMenu(menu);
        PostMessageW(m_trayWindow, WM_NULL, 0, 0);
        if (command) OnTrayCommand(command);
    }

    void MainWindow::OnTrayCommand(UINT command)
    {
        if (command == kOpenPanelId) { RestoreFromTray(); return; }
        if (command == kLaunchGameId) { LaunchGameFromUi(); return; }
        if (command == kExitAppId)
        {
            if (m_trayWindow) PostMessageW(m_trayWindow, WM_CLOSE, 0, 0);
            return;
        }
        if (command < kAccountBaseId || command >= kAccountBaseId + kMaxTrayAccounts)
            return;

        const int index = static_cast<int>(command - kAccountBaseId);
        if (!m_initialized || IsInteractionBlocked() ||
            index >= static_cast<int>(m_core.Accounts().size())) return;

        try
        {
            auto probe = m_core.ProbeCurrent();
            auto state = ClassifyProbe(probe, false);
            if (state.accountIndex == index && state.matchKind == gas::CurrentMatchKind::ExactCredential)
                return;
            m_selectedIndex = index;
            AccountsList().SelectedIndex(index);
            SwitchSelectedAsync(false);
        }
        catch (...)
        {
            RestoreFromTray();
            ShowMessage(L"切换失败", L"无法读取当前账号状态，请打开面板检查。");
        }
    }

    LRESULT CALLBACK MainWindow::WindowSubclassProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR referenceData)
    {
        auto self = reinterpret_cast<MainWindow*>(referenceData);
        if (!self) return DefSubclassProc(hwnd, message, wParam, lParam);

        if (message == self->m_taskbarCreatedMessage && self->m_taskbarCreatedMessage)
        {
            self->m_trayIconAdded = false;
            self->AddTrayIcon();
            return 0;
        }
        if (message == kTrayCallback)
        {
            UINT event = LOWORD(static_cast<DWORD>(lParam));
            if (event == WM_LBUTTONUP || event == NIN_SELECT || event == NIN_KEYSELECT)
                self->RestoreFromTray();
            else if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP)
                self->ShowTrayMenu();
            return 0;
        }
        if (message == WM_SIZE && wParam == SIZE_MINIMIZED && self->m_trayIconAdded)
            self->HideToTray();
        if (message == WM_CLOSE)
            self->RemoveTrayIcon();
        if (message == WM_NCDESTROY)
        {
            self->RemoveTrayIcon();
            self->m_trayWindow = nullptr;
            RemoveWindowSubclass(hwnd, &MainWindow::WindowSubclassProc, subclassId);
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }
}
