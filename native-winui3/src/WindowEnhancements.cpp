#include "pch.h"
#include "MainWindow.xaml.h"
#include "resource.h"

#include <winrt/Microsoft.UI.Interop.h>
#include <tlhelp32.h>
#include <cmath>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::UI;

namespace
{
    constexpr ULONGLONG kTransientStatusMilliseconds = 6000;
    constexpr double kSavedAccountUidSlotWidth = 92.0;
    constexpr double kSavedAccountStatusSlotWidth = 64.0;

    SolidColorBrush MakeFallbackBrush(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
    {
        return SolidColorBrush(Color{ a, r, g, b });
    }

    Brush LookupBrush(wchar_t const* key, Brush const& fallback)
    {
        try
        {
            auto value = Application::Current().Resources().TryLookup(box_value(key));
            if (auto brush = value.try_as<Brush>()) return brush;
        }
        catch (...) {}
        return fallback;
    }

    bool IsUnitStar(GridLength const& value) noexcept
    {
        return value.GridUnitType == GridUnitType::Star && std::abs(value.Value - 1.0) < 0.001;
    }

    bool IsFixedPixel(GridLength const& value, double expected) noexcept
    {
        return value.GridUnitType == GridUnitType::Pixel && std::abs(value.Value - expected) < 0.001;
    }
}

namespace winrt::GenshinAccountSwitcher::implementation
{
    void MainWindow::OnRootLoaded(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_enhancementsInitialized) return;
        m_enhancementsInitialized = true;

        try
        {
            Title(L"原神账号管理");
            ConfigureFixedWindow();
            ApplyWindowIcon();

            if (m_monitorTimer)
            {
                m_monitorTimer.Stop();
            }

            AccountsList().LayoutUpdated({ this, &MainWindow::OnAccountsLayoutUpdated });

            m_statusTextCallbackToken = StatusText().RegisterPropertyChangedCallback(
                TextBlock::TextProperty(),
                { this, &MainWindow::OnStatusTextPropertyChanged });

            NormalizeAccountRows();
            StartEnhancedMonitoring();
            RefreshUnifiedStatus(true);
        }
        catch (...)
        {
            if (m_monitorTimer && !m_monitorTimer.IsRunning())
            {
                m_monitorTimer.Start();
            }
            StatusText().Text(L"界面增强初始化失败，已回退到基础监测模式。");
            StatusText().Visibility(Visibility::Visible);
        }
    }

    void MainWindow::ConfigureFixedWindow()
    {
        auto hwnd = WindowHandle();
        if (!hwnd) return;

        auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        style &= ~(static_cast<LONG_PTR>(WS_THICKFRAME) | static_cast<LONG_PTR>(WS_MAXIMIZEBOX));
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);

        if (auto menu = GetSystemMenu(hwnd, FALSE))
        {
            EnableMenuItem(menu, SC_MAXIMIZE, MF_BYCOMMAND | MF_GRAYED);
            DrawMenuBar(hwnd);
        }

        UINT dpi = GetDpiForWindow(hwnd);
        double scale = static_cast<double>(dpi) / 96.0;
        RECT rect{
            0,
            0,
            static_cast<LONG>(std::lround(320.0 * scale)),
            static_cast<LONG>(std::lround(480.0 * scale))
        };

        auto exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (AdjustWindowRectExForDpi(
            &rect,
            static_cast<DWORD>(style),
            FALSE,
            static_cast<DWORD>(exStyle),
            dpi))
        {
            SetWindowPos(
                hwnd,
                nullptr,
                0,
                0,
                rect.right - rect.left,
                rect.bottom - rect.top,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
    }

    void MainWindow::ApplyWindowIcon()
    {
        auto hwnd = WindowHandle();
        if (!hwnd) return;

        auto instance = GetModuleHandleW(nullptr);
        UINT dpi = GetDpiForWindow(hwnd);
        int smallCx = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
        int smallCy = GetSystemMetricsForDpi(SM_CYSMICON, dpi);
        int bigCx = GetSystemMetricsForDpi(SM_CXICON, dpi);
        int bigCy = GetSystemMetricsForDpi(SM_CYICON, dpi);

        auto smallIcon = reinterpret_cast<HICON>(LoadImageW(
            instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
            smallCx, smallCy, LR_DEFAULTCOLOR | LR_SHARED));
        auto bigIcon = reinterpret_cast<HICON>(LoadImageW(
            instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
            bigCx, bigCy, LR_DEFAULTCOLOR | LR_SHARED));

        if (bigIcon)
        {
            try
            {
                auto windowId = Microsoft::UI::GetWindowIdFromWindow(hwnd);
                auto appWindow = Microsoft::UI::Windowing::AppWindow::GetFromWindowId(windowId);
                auto iconId = Microsoft::UI::GetIconIdFromIcon(bigIcon);
                appWindow.SetIcon(iconId);
            }
            catch (...) {}
        }

        // Also update the underlying HWND. With a valid embedded multi-size ICO this keeps
        // the classic window icon and WinUI AppWindow icon consistent across shell surfaces.
        if (smallIcon) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
        if (bigIcon) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
    }

    void MainWindow::OnAccountsLayoutUpdated(IInspectable const&, IInspectable const&)
    {
        NormalizeAccountRows();
    }

    void MainWindow::NormalizeAccountRows()
    {
        auto badgeBackground = LookupBrush(
            L"SubtleFillColorSecondaryBrush",
            MakeFallbackBrush(0xFF, 0x32, 0x32, 0x32));
        auto badgeForeground = LookupBrush(
            L"AccentTextFillColorPrimaryBrush",
            MakeFallbackBrush(0xFF, 0x60, 0xCD, 0xFF));

        for (auto& visual : m_visuals)
        {
            if (!visual.card) continue;
            auto row = visual.card.Child().try_as<Grid>();
            if (!row) continue;

            auto columns = row.ColumnDefinitions();
            bool alreadyNormalized =
                columns.Size() == 3 &&
                IsUnitStar(columns.GetAt(0).Width()) &&
                IsFixedPixel(columns.GetAt(1).Width(), kSavedAccountUidSlotWidth) &&
                IsFixedPixel(columns.GetAt(2).Width(), kSavedAccountStatusSlotWidth);

            if (alreadyNormalized) continue;

            // C++/WinRT CornerRadius is a four-field struct. A single aggregate value only
            // initializes TopLeft, which was the reason the badge previously had one rounded
            // corner. Always specify all four corners explicitly for code-created elements.
            visual.item.Padding(Thickness{ 0, 0, 0, 0 });
            visual.card.Padding(Thickness{ 16, 0, 16, 0 });
            visual.card.CornerRadius(CornerRadius{ 4, 4, 4, 4 });

            row.ColumnSpacing(16);
            columns.Clear();

            ColumnDefinition nameColumn;
            nameColumn.Width(GridLength{ 1.0, GridUnitType::Star });
            columns.Append(nameColumn);

            // Keep the UID and status areas at the same width for every row. The status
            // column therefore keeps its space even while the current-account badge is collapsed.
            ColumnDefinition uidColumn;
            uidColumn.Width(GridLength{ kSavedAccountUidSlotWidth, GridUnitType::Pixel });
            columns.Append(uidColumn);

            ColumnDefinition badgeColumn;
            badgeColumn.Width(GridLength{ kSavedAccountStatusSlotWidth, GridUnitType::Pixel });
            columns.Append(badgeColumn);

            auto children = row.Children();
            if (children.Size() >= 1)
            {
                if (auto name = children.GetAt(0).try_as<TextBlock>())
                {
                    Grid::SetColumn(name, 0);
                    name.HorizontalAlignment(HorizontalAlignment::Left);
                    name.VerticalAlignment(VerticalAlignment::Center);
                    name.TextTrimming(TextTrimming::CharacterEllipsis);
                }
            }
            if (children.Size() >= 2)
            {
                if (auto uid = children.GetAt(1).try_as<TextBlock>())
                {
                    Grid::SetColumn(uid, 1);
                    uid.HorizontalAlignment(HorizontalAlignment::Right);
                    uid.VerticalAlignment(VerticalAlignment::Center);
                    uid.TextAlignment(TextAlignment::Right);
                    uid.TextTrimming(TextTrimming::None);
                }
            }

            if (visual.badge)
            {
                Grid::SetColumn(visual.badge, 2);
                visual.badge.HorizontalAlignment(HorizontalAlignment::Right);
                visual.badge.VerticalAlignment(VerticalAlignment::Center);
                visual.badge.CornerRadius(CornerRadius{ 4, 4, 4, 4 });
                visual.badge.Padding(Thickness{ 6, 2, 6, 2 });
                visual.badge.Margin(Thickness{ 0, 0, 0, 0 });
                visual.badge.Background(badgeBackground);
            }
            if (visual.badgeText)
            {
                visual.badgeText.Foreground(badgeForeground);
                visual.badgeText.Opacity(1.0);
            }
        }
    }

    DWORD MainWindow::FindCurrentSessionGameProcessId()
    {
        DWORD currentSession{};
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSession)) return 0;

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return 0;

        DWORD resultPid = 0;
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                if (_wcsicmp(entry.szExeFile, L"YuanShen.exe") != 0) continue;
                DWORD session{};
                if (ProcessIdToSessionId(entry.th32ProcessID, &session) && session == currentSession)
                {
                    resultPid = entry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return resultPid;
    }

    void MainWindow::StartEnhancedMonitoring()
    {
        auto queue = DispatcherQueue();
        if (!queue) return;

        ReleaseTrackedGameProcess();
        m_gameWasRunning = false;

        m_processDiscoveryTimer = queue.CreateTimer();
        m_processDiscoveryTimer.Interval(std::chrono::milliseconds(2000));
        m_processDiscoveryTimer.IsRepeating(true);
        m_processDiscoveryTimer.Tick({ this, &MainWindow::OnProcessDiscoveryTick });
        m_processDiscoveryTimer.Start();

        m_statusTimer = queue.CreateTimer();
        m_statusTimer.Interval(std::chrono::milliseconds(500));
        m_statusTimer.IsRepeating(true);
        m_statusTimer.Tick({ this, &MainWindow::OnStatusTimerTick });
        m_statusTimer.Start();

        DiscoverCurrentSessionGame();
    }

    void MainWindow::OnProcessDiscoveryTick(Microsoft::UI::Dispatching::DispatcherQueueTimer const&, IInspectable const&)
    {
        DiscoverCurrentSessionGame();
    }

    void MainWindow::DiscoverCurrentSessionGame()
    {
        if (m_trackedGameProcess) return;
        DWORD pid = FindCurrentSessionGameProcessId();
        if (pid == 0)
        {
            m_gameWasRunning = false;
            return;
        }
        TrackGameProcess(pid);
    }

    void MainWindow::TrackGameProcess(DWORD pid)
    {
        if (pid == 0 || m_trackedGameProcess) return;

        HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return;

        m_trackedGameProcess = process;
        m_trackedGamePid = pid;
        m_gameWasRunning = true;
        RefreshUi(false, false);
        RefreshUnifiedStatus(true);
    }

    void MainWindow::ReleaseTrackedGameProcess()
    {
        m_gameExitWait = nullptr;
        if (m_trackedGameProcess)
        {
            CloseHandle(m_trackedGameProcess);
            m_trackedGameProcess = nullptr;
        }
        m_trackedGamePid = 0;
    }

    void MainWindow::OnTrackedGameExited()
    {
        ReleaseTrackedGameProcess();
        m_gameWasRunning = false;
        SetStatus(L"游戏已退出，正在同步账号状态…");
        SettleAndReconcileAsync(false);
    }

    void MainWindow::OnStatusTextPropertyChanged(DependencyObject const&, DependencyProperty const&)
    {
        if (m_settingUnifiedStatus) return;
        auto text = StatusText().Text();
        StatusText().Visibility(Visibility::Visible);
        m_statusOverrideUntil = text.empty()
            ? 0
            : GetTickCount64() + kTransientStatusMilliseconds;
    }

    void MainWindow::OnStatusTimerTick(Microsoft::UI::Dispatching::DispatcherQueueTimer const&, IInspectable const&)
    {
        if (m_trackedGameProcess)
        {
            DWORD waitResult = WaitForSingleObject(m_trackedGameProcess, 0);
            if (waitResult == WAIT_OBJECT_0)
            {
                OnTrackedGameExited();
                return;
            }
            if (waitResult == WAIT_FAILED)
            {
                ReleaseTrackedGameProcess();
                m_gameWasRunning = false;
            }
        }

        RefreshUnifiedStatus(false);
    }

    void MainWindow::RefreshUnifiedStatus(bool force)
    {
        if (!m_initialized)
        {
            StatusText().Visibility(Visibility::Visible);
            return;
        }

        if (!force && m_statusOverrideUntil != 0 && GetTickCount64() < m_statusOverrideUntil)
        {
            StatusText().Visibility(Visibility::Visible);
            return;
        }

        m_statusOverrideUntil = 0;
        SetUnifiedStatusText(BuildBaseStatusText());
    }

    std::wstring MainWindow::BuildBaseStatusText() const
    {
        switch (m_currentState.matchKind)
        {
        case gas::CurrentMatchKind::UniqueUidCredentialChanged:
            return m_gameWasRunning
                ? L"登录信息已变化，游戏退出后将自动同步。"
                : L"登录信息已变化，正在等待自动同步。";
        case gas::CurrentMatchKind::AmbiguousUid:
            return L"当前 UID 对应多条已保存记录，无法自动判断对应记录。";
        case gas::CurrentMatchKind::Inconsistent:
            return L"UID 与登录凭据暂时不一致，本次不会自动写入。";
        case gas::CurrentMatchKind::None:
            if (m_currentState.hasAdl && !m_currentState.uid.empty())
                return L"检测到未保存账号，可在当前账号区域添加。";
            break;
        default:
            break;
        }

        return m_gameWasRunning ? L"游戏运行中" : L"游戏未运行";
    }

    void MainWindow::SetUnifiedStatusText(std::wstring const& text)
    {
        m_settingUnifiedStatus = true;
        StatusText().Text(hstring(text));
        StatusText().Visibility(Visibility::Visible);
        m_settingUnifiedStatus = false;
    }
}
