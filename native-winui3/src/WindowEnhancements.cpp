#include "pch.h"
#include "MainWindow.xaml.h"
#include "resource.h"

#include <commctrl.h>
#include <tlhelp32.h>
#include <cmath>

#pragma comment(lib, "Comctl32.lib")

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::UI;

namespace
{
    constexpr UINT kTrackedGameExitedMessage = WM_APP + 0x45;
    constexpr UINT_PTR kWindowSubclassId = 0x4741534Du;
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
}

namespace winrt::GenshinAccountSwitcher::implementation
{
    void MainWindow::OnRootLoaded(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_enhancementsInitialized) return;
        m_enhancementsInitialized = true;

        Title(L"原神账号管理");
        ConfigureFixedWindow();
        ApplyWindowIcon();

        if (m_monitorTimer)
        {
            m_monitorTimer.Stop();
        }

        auto hwnd = WindowHandle();
        if (hwnd)
        {
            SetWindowSubclass(hwnd, &MainWindow::WindowSubclassProc, kWindowSubclassId,
                reinterpret_cast<DWORD_PTR>(this));
        }

        AccountsList().LayoutUpdated({ this, &MainWindow::OnAccountsLayoutUpdated });
        m_statusTextCallbackToken = StatusText().RegisterPropertyChangedCallback(
            TextBlock::TextProperty(),
            { this, &MainWindow::OnStatusTextPropertyChanged });

        NormalizeAccountRows();
        StartEnhancedMonitoring();
        RefreshUnifiedStatus(true);
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
            static_cast<LONG>(std::lround(720.0 * scale)),
            static_cast<LONG>(std::lround(960.0 * scale))
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
            visual.card.Padding(Thickness{ 16, 0, 16, 0 });
            visual.card.CornerRadius(CornerRadius{ 4 });

            auto row = visual.card.Child().try_as<Grid>();
            if (!row) continue;

            auto columns = row.ColumnDefinitions();
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
                }
            }

            if (visual.badge)
            {
                Grid::SetColumn(visual.badge, 2);
                visual.badge.HorizontalAlignment(HorizontalAlignment::Right);
                visual.badge.VerticalAlignment(VerticalAlignment::Center);
                visual.badge.CornerRadius(CornerRadius{ 4 });
                visual.badge.Padding(Thickness{ 8, 2, 8, 2 });
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

        m_gameWasRunning = false;
        ReleaseTrackedGameProcess();

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

        HANDLE waitHandle{};
        if (!RegisterWaitForSingleObject(
            &waitHandle,
            process,
            &MainWindow::GameExitWaitCallback,
            reinterpret_cast<PVOID>(WindowHandle()),
            INFINITE,
            WT_EXECUTEONLYONCE))
        {
            CloseHandle(process);
            return;
        }

        m_trackedGameProcess = process;
        m_gameExitWait = waitHandle;
        m_trackedGamePid = pid;
        m_gameWasRunning = true;
        RefreshUi(false, false);
        RefreshUnifiedStatus(true);
    }

    VOID CALLBACK MainWindow::GameExitWaitCallback(PVOID context, BOOLEAN)
    {
        auto hwnd = reinterpret_cast<HWND>(context);
        if (hwnd) PostMessageW(hwnd, kTrackedGameExitedMessage, 0, 0);
    }

    LRESULT CALLBACK MainWindow::WindowSubclassProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR subclassId,
        DWORD_PTR referenceData)
    {
        auto self = reinterpret_cast<MainWindow*>(referenceData);
        if (message == kTrackedGameExitedMessage && self)
        {
            self->OnTrackedGameExited();
            return 0;
        }
        if (message == WM_NCDESTROY)
        {
            RemoveWindowSubclass(hwnd, &MainWindow::WindowSubclassProc, subclassId);
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    void MainWindow::ReleaseTrackedGameProcess()
    {
        if (m_gameExitWait)
        {
            UnregisterWaitEx(m_gameExitWait, nullptr);
            m_gameExitWait = nullptr;
        }
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
        RefreshUnifiedStatus(false);
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
