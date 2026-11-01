#include "pch.h"
#include "MainWindow.xaml.h"
#include "resource.h"

#include <shellapi.h>
#include <commctrl.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.System.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Comctl32.lib")

namespace
{
    constexpr UINT kTrayIconId = 1;
    constexpr UINT kTrayCallback = WM_APP + 61;
    constexpr UINT_PTR kTraySubclassId = 0x474153;
    constexpr UINT_PTR kPopupSubclassId = 0x474154;
    constexpr UINT kOpenPanelId = 0x6100;
    constexpr UINT kLaunchGameId = 0x6101;
    constexpr UINT kExitAppId = 0x6102;
    constexpr UINT kAccountBaseId = 0x6200;
    constexpr size_t kMaxTrayAccounts = 2048;
    constexpr double kMenuWidth = 304.0;

    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;

    HWND GetWindowHandle(Window const& window)
    {
        HWND hwnd{};
        if (window)
            window.as<::IWindowNative>()->get_WindowHandle(&hwnd);
        return hwnd;
    }

    bool OwnsForeground(HWND popup)
    {
        auto foreground = GetForegroundWindow();
        return foreground && (foreground == popup || IsChild(popup, foreground));
    }
}

namespace winrt::GenshinAccountSwitcher::implementation
{
    // Temporary diagnostic trace: records window state only, never account details.
    void MainWindow::TraceTray(std::wstring const& eventName) const noexcept
    {
        try
        {
            if (!m_initialized) return;
            HWND popup = GetWindowHandle(m_trayMenuWindow);
            HWND foreground = GetForegroundWindow();
            HWND active = GetActiveWindow();
            HWND focus = GetFocus();
            POINT cursor{};
            GetCursorPos(&cursor);
            RECT bounds{};
            if (popup) GetWindowRect(popup, &bounds);

            auto handleText = [](HWND h)
            {
                return std::to_wstring(reinterpret_cast<std::uintptr_t>(h));
            };

            std::wstring line =
                L"tick=" + std::to_wstring(GetTickCount64()) +
                L" event=" + eventName +
                L" menuFlag=" + std::to_wstring(m_trayMenuVisible) +
                L" hiddenFlag=" + std::to_wstring(m_trayHidden) +
                L" popupHWND=" + handleText(popup) +
                L" popupExists=" + std::to_wstring(popup && IsWindow(popup)) +
                L" popupVisible=" + std::to_wstring(popup && IsWindowVisible(popup)) +
                L" appWindowVisible=" +
                    std::to_wstring(m_trayMenuWindow && m_trayMenuWindow.AppWindow().IsVisible()) +
                L" foreground=" + handleText(foreground) +
                L" active=" + handleText(active) +
                L" focus=" + handleText(focus) +
                L" hasForeground=" + std::to_wstring(popup && OwnsForeground(popup)) +
                L" cursor=" + std::to_wstring(cursor.x) + L"," + std::to_wstring(cursor.y) +
                L" popupRect=" + std::to_wstring(bounds.left) + L"," +
                    std::to_wstring(bounds.top) + L"," + std::to_wstring(bounds.right) +
                    L"," + std::to_wstring(bounds.bottom);

            struct WindowsState
            {
                std::wstring description;
                DWORD pid;
                int count;
            } all{ L"", GetCurrentProcessId(), 0 };
            EnumWindows([](HWND window, LPARAM data) -> BOOL
            {
                auto& state = *reinterpret_cast<WindowsState*>(data);
                DWORD pid{};
                GetWindowThreadProcessId(window, &pid);
                if (pid != state.pid) return TRUE;
                wchar_t className[96]{};
                GetClassNameW(window, className, 96);
                state.description += L" [" +
                    std::to_wstring(reinterpret_cast<std::uintptr_t>(window)) +
                    L",v=" + std::to_wstring(IsWindowVisible(window)) +
                    L",class=" + className + L"]";
                return ++state.count < 16;
            }, reinterpret_cast<LPARAM>(&all));

            line += L" processWindows=" + all.description + L"\r\n";
            auto file = m_core.DataDirectory() / L"tray-diagnostics.log";
            HANDLE out = CreateFileW(file.c_str(), FILE_APPEND_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (out == INVALID_HANDLE_VALUE) return;
            int required = WideCharToMultiByte(CP_UTF8, 0, line.data(),
                static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
            if (required > 0)
            {
                std::string utf8(static_cast<size_t>(required), '\0');
                WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
                    utf8.data(), required, nullptr, nullptr);
                DWORD written{};
                WriteFile(out, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
            }
            CloseHandle(out);
        }
        catch (...) {}
    }

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
            IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON), LR_SHARED));
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
        HideTrayMenu();
        m_trayHidden = true;
        ShowWindow(m_trayWindow, SW_HIDE);
    }

    void MainWindow::RestoreFromTray()
    {
        TraceTray(L"Restore.enter");
        HideTrayMenu();
        TraceTray(L"Restore.afterHide");
        auto hwnd = WindowHandle();
        if (!hwnd || m_appExiting) return;
        m_trayHidden = false;
        ShowWindow(hwnd, SW_RESTORE);
        ShowWindow(hwnd, SW_SHOW);
        Activate();
        SetForegroundWindow(hwnd);
        TraceTray(L"Restore.afterMainActivation");
        UpdateButtonStates();
    }

    void MainWindow::HideTrayMenu()
    {
        TraceTray(L"Hide.enter");
        m_trayMenuVisible = false;
        if (m_trayMenuDismissTimer)
            m_trayMenuDismissTimer.Stop();
        if (m_trayMenuWindow)
        {
            m_trayMenuWindow.AppWindow().Hide();
            TraceTray(L"Hide.afterAppWindowHide");
            if (auto hwnd = GetWindowHandle(m_trayMenuWindow))
                ShowWindow(hwnd, SW_HIDE);
        }
        TraceTray(L"Hide.afterWin32Hide");
    }

    void MainWindow::BuildTrayMenu()
    {
        using namespace Microsoft::UI::Xaml;
        using namespace Microsoft::UI::Xaml::Controls;
        using namespace Microsoft::UI::Xaml::Input;

        if (!m_trayMenuWindow)
        {
            m_trayMenuWindow = Window();
            m_trayMenuWindow.Title(L"原神账号管理");

            // A regular activatable window is needed for focus and Escape handling.
            auto appWindow = m_trayMenuWindow.AppWindow();
            auto presenter = Microsoft::UI::Windowing::OverlappedPresenter::Create();
            presenter.SetBorderAndTitleBar(true, false);
            presenter.IsResizable(false);
            presenter.IsMaximizable(false);
            presenter.IsMinimizable(false);
            presenter.IsAlwaysOnTop(true);
            appWindow.SetPresenter(presenter);
            appWindow.IsShownInSwitchers(false);

            auto hwnd = GetWindowHandle(m_trayMenuWindow);
            if (!hwnd || !SetWindowSubclass(hwnd, &MainWindow::TrayPopupSubclassProc,
                kPopupSubclassId, reinterpret_cast<DWORD_PTR>(this)))
                winrt::throw_hresult(E_FAIL);

            m_trayMenuWindow.Activated([this](
                IInspectable const&, WindowActivatedEventArgs const& args)
            {
                TraceTray(args.WindowActivationState() == WindowActivationState::Deactivated
                    ? L"Window.Activated.deactivated" : L"Window.Activated.activated");
                if (m_trayMenuVisible &&
                    args.WindowActivationState() == WindowActivationState::Deactivated)
                    HideTrayMenu();
            });

            m_trayMenuDismissTimer = DispatcherQueue().CreateTimer();
            m_trayMenuDismissTimer.Interval(std::chrono::milliseconds(80));
            m_trayMenuDismissTimer.IsRepeating(true);
            m_trayMenuDismissTimer.Tick([this](
                Microsoft::UI::Dispatching::DispatcherQueueTimer const&, IInspectable const&)
            {
                if (!m_trayMenuVisible || m_appExiting) return;
                if (GetTickCount64() - m_trayTraceLastTick >= 1000)
                {
                    m_trayTraceLastTick = GetTickCount64();
                    TraceTray(L"timer.sample");
                }
                auto hwnd = GetWindowHandle(m_trayMenuWindow);
                if (!hwnd || !IsWindowVisible(hwnd) || !OwnsForeground(hwnd))
                {
                    TraceTray(L"timer.dismiss.visibilityOrFocus");
                    HideTrayMenu();
                    return;
                }

                if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0)
                {
                    TraceTray(L"timer.dismiss.escape");
                    HideTrayMenu();
                    return;
                }

                // Taskbar or desktop interactions may not change foreground focus.
                if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) ||
                    (GetAsyncKeyState(VK_RBUTTON) & 0x8000))
                {
                    POINT pointer{};
                    RECT popupBounds{};
                    if (GetCursorPos(&pointer) && GetWindowRect(hwnd, &popupBounds) &&
                        !PtInRect(&popupBounds, pointer))
                    {
                        TraceTray(L"timer.dismiss.outsideClick");
                        HideTrayMenu();
                    }
                }
            });
        }

        m_trayMenuLaunchItem = nullptr;
        m_trayMenuAccountItems.clear();

        MenuFlyoutPresenter menu;
        menu.Width(kMenuWidth);

        MenuFlyoutItem open;
        open.Text(L"打开面板");
        open.Click([this](IInspectable const&, RoutedEventArgs const&)
        {
            OnTrayCommand(kOpenPanelId);
        });
        menu.Items().Append(open);

        m_trayMenuLaunchItem = MenuFlyoutItem();
        m_trayMenuLaunchItem.Text(L"启动游戏");
        m_trayMenuLaunchItem.Click([this](IInspectable const&, RoutedEventArgs const&)
        {
            OnTrayCommand(kLaunchGameId);
        });
        menu.Items().Append(m_trayMenuLaunchItem);
        menu.Items().Append(MenuFlyoutSeparator());

        const auto& accounts = m_core.Accounts();
        const size_t count = (std::min)(accounts.size(), kMaxTrayAccounts);
        if (!m_initialized || count == 0)
        {
            MenuFlyoutItem empty;
            empty.Text(L"暂无已保存账号");
            empty.IsEnabled(false);
            menu.Items().Append(empty);
        }
        else
        {
            for (size_t i = 0; i < count; ++i)
            {
                ToggleMenuFlyoutItem item;
                item.Text(winrt::hstring(accounts[i].name + L"  UID " + accounts[i].uid));
                item.Click([this, index = static_cast<int>(i)](
                    IInspectable const&, RoutedEventArgs const&)
                {
                    OnTrayCommand(kAccountBaseId + static_cast<UINT>(index));
                });
                menu.Items().Append(item);
                m_trayMenuAccountItems.push_back(item);
            }
        }

        menu.Items().Append(MenuFlyoutSeparator());
        MenuFlyoutItem exit;
        exit.Text(L"退出");
        exit.Click([this](IInspectable const&, RoutedEventArgs const&)
        {
            OnTrayCommand(kExitAppId);
        });
        menu.Items().Append(exit);

        m_trayMenuScroll = ScrollViewer();
        m_trayMenuScroll.Width(kMenuWidth);
        m_trayMenuScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        m_trayMenuScroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        m_trayMenuScroll.Content(menu);
        m_trayMenuScroll.RequestedTheme(ElementTheme::Dark);
        m_trayMenuWindow.Content(m_trayMenuScroll);
        RefreshTrayMenuState();
    }

    void MainWindow::RefreshTrayMenuState()
    {
        if (!m_trayMenuWindow || !m_initialized || m_appExiting) return;

        const bool allowed = !IsInteractionBlocked();
        if (m_trayMenuLaunchItem)
            m_trayMenuLaunchItem.IsEnabled(allowed);

        int checkedIndex = -1;
        try
        {
            auto probe = m_core.ProbeCurrent();
            auto state = ClassifyProbe(probe, m_core.IsGameRunning());
            if (state.matchKind == gas::CurrentMatchKind::ExactCredential ||
                state.matchKind == gas::CurrentMatchKind::UniqueUidCredentialChanged)
                checkedIndex = state.accountIndex;
        }
        catch (...) {}

        for (size_t i = 0; i < m_trayMenuAccountItems.size(); ++i)
        {
            m_trayMenuAccountItems[i].IsEnabled(allowed);
            m_trayMenuAccountItems[i].IsChecked(static_cast<int>(i) == checkedIndex);
        }
    }

    void MainWindow::ShowTrayMenu()
    {
        TraceTray(L"Show.enter");
        if (!m_trayWindow || !m_initialized || m_appExiting) return;
        if (m_trayMenuVisible) { HideTrayMenu(); return; }

        try
        {
            BuildTrayMenu();
            TraceTray(L"Show.afterBuild");
            auto hwnd = GetWindowHandle(m_trayMenuWindow);
            if (!hwnd) return;

            POINT cursor{};
            if (!GetCursorPos(&cursor)) return;
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(monitorInfo);
            auto monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
            if (!GetMonitorInfoW(monitor, &monitorInfo)) return;
            const RECT workArea = monitorInfo.rcWork;

            auto appWindow = m_trayMenuWindow.AppWindow();
            appWindow.Move(winrt::Windows::Graphics::PointInt32{ cursor.x, cursor.y });

            const UINT dpi = GetDpiForWindow(hwnd);
            const double scale = static_cast<double>(dpi ? dpi : 96) / 96.0;
            const double availableHeight = static_cast<double>(
                workArea.bottom - workArea.top - 16) / scale;
            m_trayMenuScroll.MaxHeight(availableHeight);

            m_trayMenuScroll.Measure(winrt::Windows::Foundation::Size{
                static_cast<float>(kMenuWidth), static_cast<float>(availableHeight) });
            const double desiredHeight = m_trayMenuScroll.DesiredSize().Height;
            const int clientWidth = static_cast<int>(std::ceil(kMenuWidth * scale));
            const int clientHeight = static_cast<int>(std::ceil(
                (std::max)(1.0, (std::min)(availableHeight, desiredHeight)) * scale));
            appWindow.ResizeClient(winrt::Windows::Graphics::SizeInt32{
                clientWidth, clientHeight });

            const auto outerSize = appWindow.Size();
            const int x = (std::max)(workArea.left,
                (std::min)(cursor.x, workArea.right - outerSize.Width));
            const int y = (std::max)(workArea.top,
                (std::min)(cursor.y - outerSize.Height, workArea.bottom - outerSize.Height));
            appWindow.Move(winrt::Windows::Graphics::PointInt32{ x, y });
            TraceTray(L"Show.afterPosition");

            m_trayMenuWindow.Activate();
            TraceTray(L"Show.afterActivate");
            if (!SetForegroundWindow(hwnd))
            {
                TraceTray(L"Show.SetForegroundWindow.failed");
                HideTrayMenu();
                return;
            }

            m_trayMenuVisible = true;
            TraceTray(L"Show.visibleFlagSet");
            m_trayTraceLastTick = 0;
            m_trayMenuDismissTimer.Start();
            RefreshTrayMenuState();
        }
        catch (...)
        {
            TraceTray(L"Show.exception");
            HideTrayMenu();
            if (m_trayHidden) RestoreFromTray();
            SetStatus(L"托盘菜单显示失败，请通过主窗口操作。");
        }
    }

    void MainWindow::OnTrayCommand(UINT command)
    {
        TraceTray(L"Command." + std::to_wstring(command));
        if (m_appExiting) return;
        if (command == kOpenPanelId) { RestoreFromTray(); return; }
        if (command == kLaunchGameId) { LaunchGameFromUi(); return; }
        if (command == kExitAppId)
        {
            HideTrayMenu();
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
            {
                RefreshTrayMenuState();
                return;
            }

            m_selectedIndex = index;
            AccountsList().SelectedIndex(index);
            SwitchSelectedAsync(false);
            RefreshTrayMenuState();
        }
        catch (...)
        {
            RestoreFromTray();
            ShowMessage(L"切换失败", L"无法读取当前账号状态，请打开面板检查。");
        }
    }

    LRESULT CALLBACK MainWindow::TrayPopupSubclassProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR referenceData)
    {
        auto self = reinterpret_cast<MainWindow*>(referenceData);
        if (!self) return DefSubclassProc(hwnd, message, wParam, lParam);

        if (message == WM_ACTIVATE)
        {
            self->TraceTray(LOWORD(wParam) == WA_INACTIVE
                ? L"Popup.WM_ACTIVATE.inactive" : L"Popup.WM_ACTIVATE.active");
            if (LOWORD(wParam) == WA_INACTIVE)
                self->HideTrayMenu();
        }
        if (message == WM_SHOWWINDOW)
            self->TraceTray(wParam ? L"Popup.WM_SHOWWINDOW.show" : L"Popup.WM_SHOWWINDOW.hide");

        if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && wParam == VK_ESCAPE)
        {
            self->TraceTray(L"Popup.Escape");
            self->HideTrayMenu();
            return 0;
        }

        if (message == WM_CLOSE && !self->m_appExiting)
        {
            self->TraceTray(L"Popup.WM_CLOSE");
            self->HideTrayMenu();
            return 0;
        }

        if (message == WM_NCDESTROY)
        {
            self->m_trayMenuVisible = false;
            RemoveWindowSubclass(hwnd, &MainWindow::TrayPopupSubclassProc, subclassId);
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
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

        if (message == WM_CLOSE && !self->m_appExiting)
        {
            self->TraceTray(L"Main.WM_CLOSE");
            self->m_appExiting = true;
            self->HideTrayMenu();
            if (self->m_trayMenuWindow)
            {
                auto popup = self->m_trayMenuWindow;
                self->m_trayMenuWindow = nullptr;
                self->m_trayMenuLaunchItem = nullptr;
                self->m_trayMenuScroll = nullptr;
                self->m_trayMenuAccountItems.clear();
                popup.Close();
            }
            self->RemoveTrayIcon();
            Application::Current().Exit();
            return 0;
        }

        if (message == WM_NCDESTROY)
        {
            self->RemoveTrayIcon();
            self->m_trayWindow = nullptr;
            RemoveWindowSubclass(hwnd, &MainWindow::WindowSubclassProc, subclassId);
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }
}
