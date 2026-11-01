#include "pch.h"
#include "MainWindow.xaml.h"
#include "resource.h"

#include <shellapi.h>
#include <commctrl.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.System.h>
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
    constexpr size_t kMaxTrayAccounts = 2048;
    constexpr int kVisibleAccountRows = 8;
    constexpr double kMenuWidth = 304.0;
    constexpr double kRowHeight = 38.0;

    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;
    using namespace winrt::Microsoft::UI::Xaml::Media;

    HWND GetWindowHandle(Window const& window)
    {
        HWND hwnd{};
        if (window)
            window.as<::IWindowNative>()->get_WindowHandle(&hwnd);
        return hwnd;
    }

    SolidColorBrush ColorBrush(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
    {
        return SolidColorBrush(winrt::Windows::UI::Color{ a, r, g, b });
    }

    Brush ThemeBrush(wchar_t const* name, Brush const& fallback)
    {
        try
        {
            auto resource = Application::Current().Resources().TryLookup(winrt::box_value(name));
            if (auto brush = resource.try_as<Brush>()) return brush;
        }
        catch (...) {}
        return fallback;
    }

    Button MenuButton(std::wstring const& title)
    {
        Button button;
        button.MinHeight(kRowHeight);
        button.HorizontalAlignment(HorizontalAlignment::Stretch);
        button.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        button.Padding(Thickness{ 10, 2, 10, 2 });
        try
        {
            auto style = Application::Current().Resources().TryLookup(winrt::box_value(L"SubtleButtonStyle"));
            if (auto value = style.try_as<Style>()) button.Style(value);
        }
        catch (...) {}

        TextBlock label;
        label.Text(winrt::hstring(title));
        label.TextTrimming(TextTrimming::CharacterEllipsis);
        label.VerticalAlignment(VerticalAlignment::Center);
        button.Content(label);
        return button;
    }

    void AddSeparator(StackPanel const& panel)
    {
        Border line;
        line.Height(1);
        line.Margin(Thickness{ 7, 4, 7, 4 });
        line.Background(ColorBrush(65, 170, 170, 170));
        panel.Children().Append(line);
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
        HideTrayMenu();
        m_trayHidden = true;
        ShowWindow(m_trayWindow, SW_HIDE);
    }

    void MainWindow::RestoreFromTray()
    {
        HideTrayMenu();
        auto hwnd = WindowHandle();
        if (!hwnd) return;
        m_trayHidden = false;
        ShowWindow(hwnd, SW_RESTORE);
        ShowWindow(hwnd, SW_SHOW);
        Activate();
        SetForegroundWindow(hwnd);
        UpdateButtonStates();
    }

    void MainWindow::HideTrayMenu()
    {
        if (!m_trayMenuVisible) return;
        m_trayMenuVisible = false;
        if (auto hwnd = GetWindowHandle(m_trayMenuWindow))
            ShowWindow(hwnd, SW_HIDE);
    }

    void MainWindow::BuildTrayMenu()
    {
        using namespace Microsoft::UI::Xaml;
        using namespace Microsoft::UI::Xaml::Controls;
        using namespace Microsoft::UI::Xaml::Media;

        if (!m_trayMenuWindow)
        {
            m_trayMenuWindow = Window();
            m_trayMenuWindow.Title(L"原神账号管理");
            m_trayMenuWindow.Activated([this](
                IInspectable const&, WindowActivatedEventArgs const& args)
            {
                if (args.WindowActivationState() == WindowActivationState::Deactivated)
                    HideTrayMenu();
            });

            auto hwnd = GetWindowHandle(m_trayMenuWindow);
            auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
            style &= ~(static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW));
            style |= WS_POPUP;
            SetWindowLongPtrW(hwnd, GWL_STYLE, style);

            auto exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            exStyle &= ~static_cast<LONG_PTR>(WS_EX_APPWINDOW);
            exStyle |= WS_EX_TOOLWINDOW;
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
        }

        m_trayMenuAccountButtons.clear();
        m_trayMenuAccountChecks.clear();
        m_trayMenuLaunchButton = nullptr;

        Border surface;
        surface.RequestedTheme(ElementTheme::Dark);
        surface.Background(ThemeBrush(L"SolidBackgroundFillColorBaseBrush",
            ColorBrush(255, 36, 36, 36)));
        surface.BorderBrush(ThemeBrush(L"SurfaceStrokeColorDefaultBrush",
            ColorBrush(255, 76, 76, 76)));
        surface.BorderThickness(Thickness{ 1 });
        surface.CornerRadius(CornerRadius{ 10 });
        surface.Padding(Thickness{ 6 });
        surface.KeyDown([this](IInspectable const&,
            Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
        {
            if (args.Key() == Windows::System::VirtualKey::Escape)
            {
                HideTrayMenu();
                args.Handled(true);
            }
        });

        StackPanel layout;
        layout.Spacing(0);

        auto open = MenuButton(L"打开面板");
        open.Click([this](IInspectable const&, RoutedEventArgs const&)
        {
            OnTrayCommand(kOpenPanelId);
        });
        layout.Children().Append(open);

        m_trayMenuLaunchButton = MenuButton(L"启动游戏");
        m_trayMenuLaunchButton.Click([this](IInspectable const&, RoutedEventArgs const&)
        {
            OnTrayCommand(kLaunchGameId);
        });
        layout.Children().Append(m_trayMenuLaunchButton);
        AddSeparator(layout);

        StackPanel accountList;
        accountList.Spacing(0);

        const auto& accounts = m_core.Accounts();
        size_t count = (std::min)(accounts.size(), kMaxTrayAccounts);
        if (!m_initialized || count == 0)
        {
            auto empty = MenuButton(L"暂无已保存账号");
            empty.IsEnabled(false);
            accountList.Children().Append(empty);
        }
        else
        {
            for (size_t i = 0; i < count; ++i)
            {
                auto button = MenuButton(L"");
                Grid row;
                ColumnDefinition checkColumn;
                checkColumn.Width(GridLength{ 26, GridUnitType::Pixel });
                ColumnDefinition labelColumn;
                labelColumn.Width(GridLength{ 1, GridUnitType::Star });
                row.ColumnDefinitions().Append(checkColumn);
                row.ColumnDefinitions().Append(labelColumn);

                TextBlock check;
                check.Text(L"✓");
                check.FontSize(15);
                check.VerticalAlignment(VerticalAlignment::Center);
                check.Opacity(0);
                row.Children().Append(check);

                TextBlock label;
                label.Text(winrt::hstring(accounts[i].name + L"  UID " + accounts[i].uid));
                label.FontSize(13);
                label.TextTrimming(TextTrimming::CharacterEllipsis);
                label.VerticalAlignment(VerticalAlignment::Center);
                Grid::SetColumn(label, 1);
                row.Children().Append(label);

                button.Content(row);
                button.Click([this, index = static_cast<int>(i)](
                    IInspectable const&, RoutedEventArgs const&)
                {
                    OnTrayCommand(kAccountBaseId + static_cast<UINT>(index));
                });
                accountList.Children().Append(button);
                m_trayMenuAccountButtons.push_back(button);
                m_trayMenuAccountChecks.push_back(check);
            }
        }

        ScrollViewer accountScroll;
        accountScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        accountScroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        accountScroll.MaxHeight(kRowHeight * kVisibleAccountRows);
        accountScroll.Content(accountList);
        layout.Children().Append(accountScroll);

        AddSeparator(layout);
        auto exit = MenuButton(L"退出");
        exit.Click([this](IInspectable const&, RoutedEventArgs const&)
        {
            OnTrayCommand(kExitAppId);
        });
        layout.Children().Append(exit);

        surface.Child(layout);
        m_trayMenuWindow.Content(surface);
        RefreshTrayMenuState();
    }

    void MainWindow::RefreshTrayMenuState()
    {
        if (!m_trayMenuWindow || !m_initialized) return;

        bool allowed = !IsInteractionBlocked();
        if (m_trayMenuLaunchButton)
            m_trayMenuLaunchButton.IsEnabled(allowed);

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

        for (size_t i = 0; i < m_trayMenuAccountButtons.size(); ++i)
        {
            m_trayMenuAccountButtons[i].IsEnabled(allowed);
            m_trayMenuAccountChecks[i].Opacity(
                static_cast<int>(i) == checkedIndex ? 1.0 : 0.0);
        }
    }

    void MainWindow::ShowTrayMenu()
    {
        if (!m_trayWindow || !m_initialized) return;
        if (m_trayMenuVisible) { HideTrayMenu(); return; }

        BuildTrayMenu();
        auto hwnd = GetWindowHandle(m_trayMenuWindow);
        if (!hwnd) return;

        POINT cursor{};
        GetCursorPos(&cursor);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        auto monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        GetMonitorInfoW(monitor, &monitorInfo);
        const RECT workArea = monitorInfo.rcWork;
        const UINT dpi = GetDpiForWindow(hwnd);
        const int width = MulDiv(static_cast<int>(kMenuWidth), dpi, 96);
        auto count = (std::min)(m_core.Accounts().size(), kMaxTrayAccounts);
        const double logicalHeight = 3 * kRowHeight +
            2 * 9.0 + 14.0 + (std::min)(count ? count : 1, static_cast<size_t>(kVisibleAccountRows)) * kRowHeight;
        const int height = MulDiv(static_cast<int>(logicalHeight), dpi, 96);

        int x = (std::max)(workArea.left, (std::min)(cursor.x, workArea.right - width));
        int y = (std::max)(workArea.top, (std::min)(cursor.y - height, workArea.bottom - height));
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height,
            SWP_NOACTIVATE | SWP_FRAMECHANGED);

        m_trayMenuVisible = true;
        m_trayMenuWindow.Activate();
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
        SetForegroundWindow(hwnd);
        RefreshTrayMenuState();
    }

    void MainWindow::OnTrayCommand(UINT command)
    {
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
                return;
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
        {
            self->HideTrayMenu();
            if (self->m_trayMenuWindow)
            {
                self->m_trayMenuWindow.Close();
                self->m_trayMenuWindow = nullptr;
            }
            self->RemoveTrayIcon();
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
