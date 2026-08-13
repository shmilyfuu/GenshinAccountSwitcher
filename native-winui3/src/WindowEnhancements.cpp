#include "pch.h"
#include "MainWindow.xaml.h"
#include "resource.h"

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

            // Phase 1-3 used a one-second full process enumeration timer. Stop it after
            // the enhanced monitor is ready so only one monitor owns the game lifecycle.
            if (m_monitorTimer)
            {
                m_monitorTimer.Stop();
            }

            // LayoutUpdated is retained only as a way to catch newly rebuilt account rows.
            // NormalizeAccountRows is intentionally idempotent: once a row is normalized,
            // it performs no further layout mutations, preventing the Phase 4 layout loop.
            AccountsList().LayoutUpdated({ this, &MainWindow::OnAccountsLayoutUpdated });

            // Existing controller code calls SetStatus. This callback keeps those messages
            // visible for a short period before the permanent base status returns.
            m_statusTextCallbackToken = StatusText().RegisterPropertyChangedCallback(
                TextBlock::TextProperty(),
                { this, &MainWindow::OnStatusTextPropertyChanged });

            NormalizeAccountRows();
            StartEnhancedMonitoring();
            RefreshUnifiedStatus(true);
        }
        catch (...)
        {
            // UI enhancement failure must never terminate the account manager. The core
            // functionality remains usable and the old monitor can continue as fallback.
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

        // Requested dimensions are the client area, excluding the title bar/frame.
        UINT dpi = GetDpiForWindow(hwnd);
        double scale = static_cast<double>(dpi) / 96.0;
        RECT rect{
            0,
            0,
            static_cast<LONG>(std::lround(315.0 * scale)),
            static_cast<LONG>(std::lround(560.0 * scale))
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

        if (smallIcon) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
        if (bigIcon) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
    }

    void MainWindow::OnAccountsLayoutUpdated(IInspectable const&, IInspectable const&)
    {
        NormalizeAccountRows();
    }

    void MainWindow::NormalizeAccountRows()
    {
        auto secondaryAccent = LookupBrush(
            L"AccentFillColorSecondaryBrush",
            MakeFallbackBrush(0xFF, 0x2D, 0x5F, 0x86));
        auto textOnAccent = LookupBrush(
            L"TextOnAccentFillColorPrimaryBrush",
            MakeFallbackBrush(0xFF, 0xFF, 0xFF, 0xFF));

        for (auto& visual : m_visuals)
        {
            if (!visual.card) continue;
            auto row = visual.card.Child().try_as<Grid>();
            if (!row) continue;

            auto columns = row.ColumnDefinitions();
            bool alreadyNormalized = columns.Size() == 3;
            if (alreadyNormalized)
            {
                for (uint32_t i = 0; i < 3; ++i)
                {
                    if (!IsUnitStar(columns.GetAt(i).Width()))
                    {
                        alreadyNormalized = false;
                        break;
                    }
                }
            }

            // Critical: do not write any layout property when the row already has the
            // requested shape. Re-writing columns from LayoutUpdated would schedule another
            // layout pass indefinitely.
            if (alreadyNormalized) continue;

            visual.card.Padding(Thickness{ 16, 0, 16, 0 });
            visual.card.CornerRadius(CornerRadius{ 4 });

            columns.Clear();
            for (int i = 0; i < 3; ++i)
            {
                ColumnDefinition column;
                column.Width(GridLength{ 1.0, GridUnitType::Star });
                columns.Append(column);
            }

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
                    uid.HorizontalAlignment(HorizontalAlignment::Center);
                    uid.VerticalAlignment(VerticalAlignment::Center);
                    uid.TextAlignment(TextAlignment::Center);
                    uid.TextTrimming(TextTrimming::CharacterEllipsis);
                }
            }

            if (visual.badge)
            {
                Grid::SetColumn(visual.badge, 2);
                visual.badge.HorizontalAlignment(HorizontalAlignment::Right);
                visual.badge.VerticalAlignment(VerticalAlignment::Center);
                visual.badge.CornerRadius(CornerRadius{ 4 });
                visual.badge.Padding(Thickness{ 6, 2, 6, 2 });
                visual.badge.Background(secondaryAccent);
            }
            if (visual.badgeText)
            {
                visual.badgeText.Foreground(textOnAccent);
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

        // A low-frequency full scan only discovers games started from any external entry.
        m_processDiscoveryTimer = queue.CreateTimer();
        m_processDiscoveryTimer.Interval(std::chrono::milliseconds(2000));
        m_processDiscoveryTimer.IsRepeating(true);
        m_processDiscoveryTimer.Tick({ this, &MainWindow::OnProcessDiscoveryTick });
        m_processDiscoveryTimer.Start();

        // Once a process is found, this timer checks the retained process handle. It does
        // not enumerate all processes, and detects exit within roughly half a second.
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
        // Phase 5 no longer registers a thread-pool wait or subclasses the WinUI HWND.
        // Keep the legacy member clear for binary/source compatibility with the header.
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
