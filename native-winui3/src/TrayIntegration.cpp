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
#include <dwmapi.h>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Dwmapi.lib")

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

        // Cancel a pending frame-synchronized reveal while the HWND is hidden.
        if (m_mainRevealPending)
        {
            ShowWindow(m_trayWindow, SW_HIDE);
            m_mainRevealPending = false;
            Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(m_mainRenderingToken);
            if (m_mainRevealFallback) m_mainRevealFallback.Stop();
            BOOL uncloak = FALSE;
            DwmSetWindowAttribute(m_trayWindow, DWMWA_CLOAK,
                &uncloak, sizeof(uncloak));
        }
        m_trayHidden = true;
        ShowWindow(m_trayWindow, SW_HIDE);
    }

    void MainWindow::FinishMainRestore()
    {
        if (!m_mainRevealPending) return;
        m_mainRevealPending = false;
        Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(m_mainRenderingToken);
        if (m_mainRevealFallback) m_mainRevealFallback.Stop();

        auto hwnd = WindowHandle();
        if (!hwnd || m_appExiting) return;
        BOOL uncloak = FALSE;
        DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &uncloak, sizeof(uncloak));
        SetForegroundWindow(hwnd);
    }

    void MainWindow::RestoreFromTray()
    {
        HideTrayMenu();
        auto hwnd = WindowHandle();
        if (!hwnd || m_appExiting || m_mainRevealPending) return;

        const bool needsReveal = m_trayHidden || !IsWindowVisible(hwnd) || IsIconic(hwnd);
        m_trayHidden = false;
        if (!needsReveal)
        {
            SetForegroundWindow(hwnd);
            return;
        }

        // Preserve the HWND while WinUI composes its first restored frame.
        // DWM hides the window until XAML has produced rendered frames.
        BOOL cloak = TRUE;
        if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak))))
        {
            ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(hwnd);
            return;
        }

        m_mainRevealPending = true;
        m_mainRevealFrames = 0;
        m_mainRenderingToken = Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(
            [this](IInspectable const&, IInspectable const&)
            {
                if (!m_mainRevealPending) return;
                if (++m_mainRevealFrames >= 2) FinishMainRestore();
            });

        if (!m_mainRevealFallback)
        {
            m_mainRevealFallback = DispatcherQueue().CreateTimer();
            m_mainRevealFallback.IsRepeating(false);
            m_mainRevealFallback.Interval(std::chrono::milliseconds(700));
            m_mainRevealFallback.Tick([this](
                Microsoft::UI::Dispatching::DispatcherQueueTimer const&,
                IInspectable const&)
            {
                FinishMainRestore();
            });
        }
        m_mainRevealFallback.Start();
        ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
        UpdateButtonStates();
    }

    void MainWindow::HideTrayMenu()
    {
        m_trayMenuVisible = false;
        if (m_trayMenuDismissTimer)
            m_trayMenuDismissTimer.Stop();
        if (m_trayMenuWindow)
        {
            m_trayMenuWindow.AppWindow().Hide();
            if (auto hwnd = GetWindowHandle(m_trayMenuWindow))
                ShowWindow(hwnd, SW_HIDE);
        }
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
                AdjustTrayMenuToRenderedContent();
                auto hwnd = GetWindowHandle(m_trayMenuWindow);
                if (!hwnd || !IsWindowVisible(hwnd) || !OwnsForeground(hwnd))
                {
                    HideTrayMenu();
                    return;
                }

                if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0)
                {
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
                        HideTrayMenu();
                    }
                }
            });
        }

        if (m_trayMenuPresenter)
            m_trayMenuPresenter.LayoutUpdated(m_trayMenuLayoutToken);
        m_trayMenuExitItem = nullptr;
        m_trayMenuLaunchItem = nullptr;
        m_trayMenuAccountItems.clear();
        m_trayMenuCheckedIndex = -2;

        MenuFlyoutPresenter menu;
        menu.Width(kMenuWidth);
        menu.RequestedTheme(ElementTheme::Dark);

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
                MenuFlyoutItem item;
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
        m_trayMenuExitItem = exit;

        // MenuFlyoutPresenter owns its native menu ScrollViewer.
        // Wrapping it in another ScrollViewer causes extra viewport height.
        m_trayMenuPresenter = menu;
        m_trayMenuWindow.Content(menu);
        m_trayMenuLayoutToken = menu.LayoutUpdated([this](
            IInspectable const&, IInspectable const&)
        {
            if (m_trayMenuVisible && !m_trayMenuOpening)
                AdjustTrayMenuToRenderedContent();
        });
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

        for (auto const& item : m_trayMenuAccountItems)
            item.IsEnabled(allowed);

        if (checkedIndex != m_trayMenuCheckedIndex)
        {
            m_trayMenuCheckedIndex = checkedIndex;
            auto const& accounts = m_core.Accounts();
            for (size_t i = 0; i < m_trayMenuAccountItems.size() && i < accounts.size(); ++i)
            {
                // Use visible menu text because the standalone menu presenter
                // does not reliably paint icons assigned after construction.
                std::wstring prefix = static_cast<int>(i) == checkedIndex
                    ? L"✓  " : L"    ";
                m_trayMenuAccountItems[i].Text(winrt::hstring(
                    prefix + accounts[i].name + L"  UID " + accounts[i].uid));
            }
        }
    }

    void MainWindow::AdjustTrayMenuToRenderedContent()
    {
        if (!m_trayMenuVisible || m_trayMenuOpening || m_trayMenuSizing ||
            !m_trayMenuPresenter || !m_trayMenuExitItem || !m_trayMenuWindow)
            return;

        auto hwnd = GetWindowHandle(m_trayMenuWindow);
        if (!hwnd || !IsWindowVisible(hwnd) ||
            m_trayMenuExitItem.ActualHeight() <= 1.0)
            return;

        try
        {
            const auto rect = m_trayMenuExitItem.TransformToVisual(m_trayMenuPresenter)
                .TransformBounds(winrt::Windows::Foundation::Rect{
                    0.0f, 0.0f,
                    static_cast<float>(m_trayMenuExitItem.ActualWidth()),
                    static_cast<float>(m_trayMenuExitItem.ActualHeight()) });
            // The last item's real bounds include every visible row and separator.
            // Preserve the presenter's bottom inset and stroke.
            const double bottomInset = (std::max)(4.0,
                m_trayMenuPresenter.Padding().Bottom +
                m_trayMenuPresenter.BorderThickness().Bottom);
            const double contentHeight = rect.Y + rect.Height + bottomInset;
            if (!std::isfinite(contentHeight) || contentHeight < 100.0)
                return;

            const auto dpi = GetDpiForWindow(hwnd);
            const double scale = static_cast<double>(dpi ? dpi : 96) / 96.0;
            const auto desiredPixels = static_cast<int>(std::ceil(
                (std::min)(m_trayMenuAvailableHeight, contentHeight) * scale));
            auto appWindow = m_trayMenuWindow.AppWindow();
            if (std::abs(appWindow.ClientSize().Height - desiredPixels) <= 2)
                return;

            m_trayMenuSizing = true;
            struct ResetSizing
            {
                bool& flag;
                ~ResetSizing() { flag = false; }
            } reset{ m_trayMenuSizing };
            appWindow.ResizeClient(winrt::Windows::Graphics::SizeInt32{
                appWindow.ClientSize().Width, desiredPixels });

            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(monitorInfo);
            auto monitor = MonitorFromPoint(m_trayMenuAnchor, MONITOR_DEFAULTTONEAREST);
            if (GetMonitorInfoW(monitor, &monitorInfo))
            {
                const auto& area = monitorInfo.rcWork;
                const auto size = appWindow.Size();
                const int x = (std::max)(area.left,
                    (std::min)(m_trayMenuAnchor.x, area.right - size.Width));
                const int y = (std::max)(area.top,
                    (std::min)(m_trayMenuAnchor.y - size.Height, area.bottom - size.Height));
                appWindow.Move(winrt::Windows::Graphics::PointInt32{ x, y });
            }
        }
        catch (...) {}
    }

    void MainWindow::ShowTrayMenu()
    {
        if (!m_trayWindow || !m_initialized || m_appExiting || m_trayMenuOpening) return;
        if (m_trayMenuVisible) { HideTrayMenu(); return; }

        // Handle nested tray events while WinUI initializes its window.
        m_trayMenuOpening = true;
        struct OpenGuard
        {
            bool& opening;
            ~OpenGuard() { opening = false; }
        } guard{ m_trayMenuOpening };

        try
        {
            BuildTrayMenu();
            auto hwnd = GetWindowHandle(m_trayMenuWindow);
            if (!hwnd) return;

            POINT cursor{};
            if (!GetCursorPos(&cursor)) return;
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(monitorInfo);
            auto monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
            if (!GetMonitorInfoW(monitor, &monitorInfo)) return;
            const RECT workArea = monitorInfo.rcWork;
            m_trayMenuAnchor = cursor;

            auto appWindow = m_trayMenuWindow.AppWindow();
            appWindow.Move(winrt::Windows::Graphics::PointInt32{ cursor.x, cursor.y });

            const UINT dpi = GetDpiForWindow(hwnd);
            const double scale = static_cast<double>(dpi ? dpi : 96) / 96.0;
            const double availableHeight = static_cast<double>(
                workArea.bottom - workArea.top - 16) / scale;
            m_trayMenuAvailableHeight = availableHeight;
            m_trayMenuPresenter.MaxHeight(availableHeight);
            m_trayMenuPresenter.Measure(winrt::Windows::Foundation::Size{
                static_cast<float>(kMenuWidth), static_cast<float>(availableHeight) });
            const double desiredHeight = m_trayMenuPresenter.DesiredSize().Height;
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

            m_trayMenuVisible = true;
            m_trayMenuWindow.Activate();
            if (!IsWindowVisible(hwnd) || !SetForegroundWindow(hwnd) ||
                !IsWindowVisible(hwnd))
            {
                HideTrayMenu();
                return;
            }

            m_trayMenuDismissTimer.Start();
            RefreshTrayMenuState();
            // Correct the window against realized item bounds after XAML layout.
            // The initial measurement retains the proven complete-menu geometry.
        }
        catch (...)
        {
            HideTrayMenu();
            if (m_trayHidden) RestoreFromTray();
            SetStatus(L"托盘菜单显示失败，请通过主窗口操作。");
        }
    }

    void MainWindow::OnTrayCommand(UINT command)
    {
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
            if (state.accountIndex == index &&
                (state.matchKind == gas::CurrentMatchKind::ExactCredential ||
                 state.matchKind == gas::CurrentMatchKind::UniqueUidCredentialChanged))
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
            if (LOWORD(wParam) == WA_INACTIVE)
                self->HideTrayMenu();
        }
        if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && wParam == VK_ESCAPE)
        {
            self->HideTrayMenu();
            return 0;
        }

        if (message == WM_CLOSE && !self->m_appExiting)
        {
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
            else if (event == WM_CONTEXTMENU)
                self->ShowTrayMenu();
            return 0;
        }

        // Intercept minimize before the native window enters iconic state.
        // This prevents a one-frame blank XAML surface when restoring.
        if (message == WM_SYSCOMMAND &&
            (wParam & 0xFFF0) == SC_MINIMIZE && self->m_trayIconAdded)
        {
            self->HideToTray();
            return 0;
        }
        if (message == WM_SIZE && wParam == SIZE_MINIMIZED && self->m_trayIconAdded)
            self->HideToTray();

        if (message == WM_CLOSE && !self->m_appExiting)
        {
            self->m_appExiting = true;
            if (self->m_mainRevealPending)
            {
                self->m_mainRevealPending = false;
                Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(self->m_mainRenderingToken);
                if (self->m_mainRevealFallback) self->m_mainRevealFallback.Stop();
            }
            self->HideTrayMenu();
            if (self->m_trayMenuWindow)
            {
                auto popup = self->m_trayMenuWindow;
                self->m_trayMenuWindow = nullptr;
                self->m_trayMenuLaunchItem = nullptr;
                if (self->m_trayMenuPresenter)
                    self->m_trayMenuPresenter.LayoutUpdated(self->m_trayMenuLayoutToken);
                self->m_trayMenuPresenter = nullptr;
                self->m_trayMenuExitItem = nullptr;
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
