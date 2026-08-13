#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <microsoft.ui.xaml.window.h>
#include <dwmapi.h>
#include <commdlg.h>
#include <shellapi.h>
#include <system_error>

#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Shell32.lib")

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::UI;

namespace
{
    SolidColorBrush MakeBrush(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
    {
        return SolidColorBrush(Color{ a, r, g, b });
    }

    Thickness MakeThickness(double l, double t, double r, double b)
    {
        return Thickness{ l, t, r, b };
    }

    hstring H(std::wstring const& value) { return hstring(value); }

    std::wstring Trim(std::wstring value)
    {
        auto first = value.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos) return {};
        auto last = value.find_last_not_of(L" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    std::wstring GuidFileStem(GUID const& id)
    {
        wchar_t buffer[64]{};
        if (StringFromGUID2(id, buffer, static_cast<int>(std::size(buffer))) <= 0) return {};
        std::wstring text(buffer);
        if (text.size() >= 2 && text.front() == L'{' && text.back() == L'}')
            text = text.substr(1, text.size() - 2);
        return text;
    }
}

namespace winrt::GenshinAccountSwitcher::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();
        Title(L"原神账号切换器");
        ResizeWindow();

        if (auto hwnd = WindowHandle())
        {
            BOOL dark = TRUE;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        }

        try
        {
            m_core.Initialize();
            m_initialized = true;
            m_gameWasRunning = m_core.IsGameRunning();
            RefreshUi(true, false);
            StartMonitoring();
        }
        catch (...)
        {
            m_initialized = false;
            CurrentAccountText().Text(L"初始化失败");
            CurrentUidText().Text(L"UID —");
            CredentialStateText().Text(L"");
            AddButton().Visibility(Visibility::Collapsed);
            SetStatus(L"程序目录不可写，或本地数据无法读取。请把程序移动到普通可写目录后重试。");
            UpdateButtonStates();
        }
    }

    HWND MainWindow::WindowHandle() const
    {
        HWND hwnd{};
        Microsoft::UI::Xaml::Window window = *this;
        window.as<::IWindowNative>()->get_WindowHandle(&hwnd);
        return hwnd;
    }

    void MainWindow::ResizeWindow()
    {
        auto hwnd = WindowHandle();
        if (!hwnd) return;
        auto dpi = GetDpiForWindow(hwnd);
        double scale = static_cast<double>(dpi) / 96.0;
        SetWindowPos(hwnd, nullptr, 0, 0,
            static_cast<int>(740 * scale),
            static_cast<int>(590 * scale),
            SWP_NOMOVE | SWP_NOZORDER);
    }

    void MainWindow::OnWindowActivated(IInspectable const&, WindowActivatedEventArgs const& args)
    {
        if (args.WindowActivationState() == WindowActivationState::Deactivated) return;
        if (!m_initialized) return;

        if (m_firstActivation)
        {
            m_firstActivation = false;
            if (!m_gameWasRunning) SettleAndReconcileAsync(true);
            return;
        }

        if (!m_settlementRunning) RefreshUi(false, false);
    }

    void MainWindow::OnRefreshClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_initialized) return;
        if (m_core.IsGameRunning()) RefreshUi(true, true);
        else SettleAndReconcileAsync(false);
    }

    void MainWindow::SetStatus(std::wstring const& text)
    {
        StatusText().Text(H(text));
    }

    bool MainWindow::IdentitySnapshotEquals(gas::RegistrySnapshot const& a, gas::RegistrySnapshot const& b) noexcept
    {
        return a.adl.ContentEquals(b.adl) && a.lastUid.ContentEquals(b.lastUid);
    }

    gas::CurrentState MainWindow::ClassifyProbe(gas::CurrentProbe const& probe, bool gameRunning)
    {
        gas::CurrentState state;
        state.uid = probe.uid;
        state.hasAdl = probe.snapshot.adl.exists && !probe.snapshot.adl.data.empty();
        state.gameRunning = gameRunning;

        if (!state.hasAdl)
            return state;

        auto const& accounts = m_core.Accounts();
        if (probe.exactAccountIndex >= 0 && probe.exactAccountIndex < static_cast<int>(accounts.size()))
        {
            auto const& exact = accounts[static_cast<size_t>(probe.exactAccountIndex)];
            if (!probe.uid.empty() && exact.uid != probe.uid)
            {
                state.matchKind = gas::CurrentMatchKind::Inconsistent;
                return state;
            }
            state.accountIndex = probe.exactAccountIndex;
            state.matchKind = gas::CurrentMatchKind::ExactCredential;
            return state;
        }

        if (probe.uidMatches.size() == 1)
        {
            state.accountIndex = probe.uidMatches.front();
            state.matchKind = gas::CurrentMatchKind::UniqueUidCredentialChanged;
        }
        else if (probe.uidMatches.size() > 1)
        {
            state.matchKind = gas::CurrentMatchKind::AmbiguousUid;
        }
        return state;
    }

    void MainWindow::RefreshUi(bool rebuildAccounts, bool showRefreshStatus)
    {
        if (!m_initialized) return;
        try
        {
            auto probe = m_core.ProbeCurrent();
            bool running = m_core.IsGameRunning();
            m_currentState = ClassifyProbe(probe, running);
            auto const& accounts = m_core.Accounts();

            CurrentUidText().Text(m_currentState.uid.empty() ? L"UID —" : H(L"UID " + m_currentState.uid));
            CredentialStateText().Text(L"");

            switch (m_currentState.matchKind)
            {
            case gas::CurrentMatchKind::ExactCredential:
                if (m_currentState.accountIndex >= 0)
                    CurrentAccountText().Text(H(accounts[static_cast<size_t>(m_currentState.accountIndex)].name));
                break;

            case gas::CurrentMatchKind::UniqueUidCredentialChanged:
                if (m_currentState.accountIndex >= 0)
                    CurrentAccountText().Text(H(accounts[static_cast<size_t>(m_currentState.accountIndex)].name));
                CredentialStateText().Text(running
                    ? L"登录信息已变化，游戏退出后将自动同步"
                    : L"登录信息已变化，正在等待自动同步");
                break;

            case gas::CurrentMatchKind::AmbiguousUid:
                CurrentAccountText().Text(L"UID 已保存（存在多条记录）");
                CredentialStateText().Text(L"无法自动判断对应记录，请在更多操作中手动处理");
                break;

            case gas::CurrentMatchKind::Inconsistent:
                CurrentAccountText().Text(L"登录状态正在变化…");
                CredentialStateText().Text(L"UID 与登录凭据暂时不一致，程序不会自动写入");
                break;

            default:
                if (m_currentState.hasAdl)
                    CurrentAccountText().Text(m_currentState.uid.empty() ? L"未保存登录态" : L"未保存账号");
                else
                    CurrentAccountText().Text(L"未检测到登录态");
                break;
            }

            bool showAdd = m_currentState.hasAdl &&
                (m_currentState.matchKind == gas::CurrentMatchKind::None);
            AddButton().Visibility(showAdd ? Visibility::Visible : Visibility::Collapsed);

            AccountCountText().Text(H(std::to_wstring(accounts.size()) + L" 个"));
            if (m_selectedIndex >= static_cast<int>(accounts.size())) m_selectedIndex = -1;
            if (m_selectedIndex < 0 && m_currentState.accountIndex >= 0)
                m_selectedIndex = m_currentState.accountIndex;

            if (rebuildAccounts) RebuildAccounts();
            else UpdateAccountVisuals();

            UpdateButtonStates();
            if (showRefreshStatus) SetStatus(L"状态已刷新。");
        }
        catch (...)
        {
            SetStatus(L"刷新失败：无法读取当前注册表或本地账号数据。");
        }
    }

    MainWindow::AccountVisual MainWindow::BuildAccountVisual(int index)
    {
        auto const& account = m_core.Accounts().at(static_cast<size_t>(index));
        AccountVisual v;
        v.item = ListViewItem();
        v.item.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        v.item.Padding(MakeThickness(0, 0, 0, 0));
        v.item.MinHeight(48);

        v.card = Border();
        v.card.CornerRadius(CornerRadius{ 6 });
        v.card.BorderThickness(Thickness{ 1 });
        v.card.Padding(MakeThickness(12, 0, 12, 0));

        Grid row;
        row.Height(48);
        ColumnDefinition c0; c0.Width(GridLength{ 1, GridUnitType::Star });
        ColumnDefinition c1; c1.Width(GridLength{ 150, GridUnitType::Pixel });
        ColumnDefinition c2; c2.Width(GridLength{ 116, GridUnitType::Pixel });
        row.ColumnDefinitions().Append(c0);
        row.ColumnDefinitions().Append(c1);
        row.ColumnDefinitions().Append(c2);

        TextBlock name;
        name.Text(H(account.name));
        name.FontSize(14);
        name.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        name.VerticalAlignment(VerticalAlignment::Center);
        row.Children().Append(name);

        TextBlock uid;
        uid.Text(H(L"UID " + account.uid));
        uid.FontSize(12);
        uid.Opacity(0.62);
        uid.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(uid, 1);
        row.Children().Append(uid);

        v.badge = Border();
        v.badge.CornerRadius(CornerRadius{ 9 });
        v.badge.Padding(MakeThickness(9, 3, 9, 3));
        v.badge.HorizontalAlignment(HorizontalAlignment::Left);
        v.badge.VerticalAlignment(VerticalAlignment::Center);
        v.badge.Visibility(Visibility::Collapsed);
        v.badge.Background(MakeBrush(0xFF, 0x1F, 0x45, 0x57));

        v.badgeText = TextBlock();
        v.badgeText.FontSize(11);
        v.badgeText.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        v.badgeText.Text(L"当前账号");
        v.badge.Child(v.badgeText);
        Grid::SetColumn(v.badge, 2);
        row.Children().Append(v.badge);

        v.card.Child(row);
        v.item.Content(v.card);
        return v;
    }

    void MainWindow::RebuildAccounts()
    {
        m_rebuildingAccounts = true;
        AccountsList().Items().Clear();
        m_visuals.clear();

        for (int i = 0; i < static_cast<int>(m_core.Accounts().size()); ++i)
        {
            auto visual = BuildAccountVisual(i);
            AccountsList().Items().Append(visual.item);
            m_visuals.push_back(std::move(visual));
        }

        AccountsList().SelectedIndex(m_selectedIndex);
        m_rebuildingAccounts = false;
        UpdateAccountVisuals();
    }

    void MainWindow::UpdateAccountVisuals()
    {
        for (int i = 0; i < static_cast<int>(m_visuals.size()); ++i)
        {
            auto& visual = m_visuals[static_cast<size_t>(i)];
            bool selected = i == m_selectedIndex;
            bool current = i == m_currentState.accountIndex &&
                (m_currentState.matchKind == gas::CurrentMatchKind::ExactCredential ||
                 m_currentState.matchKind == gas::CurrentMatchKind::UniqueUidCredentialChanged);

            visual.card.Background(selected
                ? MakeBrush(0xFF, 0x36, 0x36, 0x36)
                : MakeBrush(0xFF, 0x29, 0x29, 0x29));
            visual.card.BorderBrush(selected
                ? MakeBrush(0xFF, 0x5C, 0x5C, 0x5C)
                : MakeBrush(0xFF, 0x39, 0x39, 0x39));
            visual.badge.Visibility(current ? Visibility::Visible : Visibility::Collapsed);
        }
    }

    void MainWindow::OnAccountSelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_rebuildingAccounts) return;
        m_selectedIndex = AccountsList().SelectedIndex();
        UpdateAccountVisuals();
        UpdateButtonStates();
    }

    void MainWindow::UpdateButtonStates()
    {
        bool selected = m_initialized && m_selectedIndex >= 0 &&
            m_selectedIndex < static_cast<int>(m_core.Accounts().size());

        SwitchButton().IsEnabled(selected);
        SwitchLaunchButton().IsEnabled(selected);
        UpdateMenuItem().IsEnabled(selected);
        RenameMenuItem().IsEnabled(selected);
        DeleteMenuItem().IsEnabled(selected);
        RestoreMenuItem().IsEnabled(m_initialized && m_core.RecoveryExists());
        AddButton().IsEnabled(m_initialized);
    }

    void MainWindow::StartMonitoring()
    {
        auto queue = DispatcherQueue();
        if (!queue) return;
        m_monitorTimer = queue.CreateTimer();
        m_monitorTimer.Interval(std::chrono::milliseconds(1000));
        m_monitorTimer.IsRepeating(true);
        m_monitorTimer.Tick({ this, &MainWindow::OnMonitorTick });
        m_monitorTimer.Start();
    }

    void MainWindow::OnMonitorTick(Microsoft::UI::Dispatching::DispatcherQueueTimer const&, IInspectable const&)
    {
        if (!m_initialized) return;

        bool running{};
        try { running = m_core.IsGameRunning(); }
        catch (...) { return; }

        if (running)
        {
            if (!m_gameWasRunning)
            {
                m_gameWasRunning = true;
                m_runtimePollTicks = 0;
                RefreshUi(false, false);
            }
            else if (++m_runtimePollTicks >= 2)
            {
                m_runtimePollTicks = 0;
                RefreshUi(false, false);
            }
            return;
        }

        if (m_gameWasRunning)
        {
            m_gameWasRunning = false;
            m_runtimePollTicks = 0;
            SettleAndReconcileAsync(false);
        }
    }

    bool MainWindow::BackupCredential(gas::AccountProfile const& account)
    {
        try
        {
            auto stem = GuidFileStem(account.id);
            if (stem.empty()) return false;
            auto accountsDir = m_core.DataDirectory() / L"accounts";
            auto source = accountsDir / (stem + L".dat");
            auto previousDir = accountsDir / L"previous";
            auto destination = previousDir / (stem + L".dat");

            std::error_code ec;
            std::filesystem::create_directories(previousDir, ec);
            if (ec || !std::filesystem::exists(source)) return false;
            std::filesystem::copy_file(source, destination,
                std::filesystem::copy_options::overwrite_existing, ec);
            return !ec;
        }
        catch (...) { return false; }
    }

    void MainWindow::RemoveCredentialBackup(gas::AccountProfile const& account) noexcept
    {
        try
        {
            auto stem = GuidFileStem(account.id);
            if (stem.empty()) return;
            std::error_code ec;
            std::filesystem::remove(m_core.DataDirectory() / L"accounts" / L"previous" / (stem + L".dat"), ec);
        }
        catch (...) {}
    }

    fire_and_forget MainWindow::SettleAndReconcileAsync(bool initialPass)
    {
        auto lifetime = get_strong();
        if (m_settlementRunning) co_return;
        m_settlementRunning = true;

        struct ResetFlag
        {
            bool& flag;
            ~ResetFlag() { flag = false; }
        } reset{ m_settlementRunning };

        try
        {
            apartment_context uiContext;
            co_await resume_after(std::chrono::milliseconds(initialPass ? 150 : 450));
            co_await uiContext;

            if (m_core.IsGameRunning()) co_return;

            std::optional<gas::CurrentProbe> previous;
            std::optional<gas::CurrentProbe> settled;

            for (int i = 0; i < 10; ++i)
            {
                auto probe = m_core.ProbeCurrent();
                auto state = ClassifyProbe(probe, false);

                if (state.matchKind != gas::CurrentMatchKind::Inconsistent && previous &&
                    previous->uid == probe.uid &&
                    IdentitySnapshotEquals(previous->snapshot, probe.snapshot))
                {
                    settled = std::move(probe);
                    break;
                }

                previous = std::move(probe);
                co_await resume_after(std::chrono::milliseconds(200));
                co_await uiContext;
                if (m_core.IsGameRunning()) co_return;
            }

            if (!settled)
            {
                RefreshUi(true, false);
                SetStatus(L"登录状态仍在变化，本次未自动更新。可稍后使用“更多 → 刷新状态”。");
                co_return;
            }

            auto finalState = ClassifyProbe(*settled, false);
            if (finalState.matchKind == gas::CurrentMatchKind::UniqueUidCredentialChanged &&
                finalState.accountIndex >= 0)
            {
                auto account = m_core.Accounts()[static_cast<size_t>(finalState.accountIndex)];
                if (!BackupCredential(account))
                {
                    RefreshUi(true, false);
                    SetStatus(L"无法创建上一版登录凭据备份，本次未自动更新。");
                    co_return;
                }

                auto result = m_core.UpdateAccount(finalState.accountIndex, settled->snapshot);
                RefreshUi(true, false);
                if (result.success)
                    SetStatus(L"“" + account.name + L"”的登录信息已自动更新。");
                else
                    SetStatus(L"自动更新登录信息失败，已保留上一版凭据。");
                co_return;
            }

            RefreshUi(true, false);
            if (finalState.matchKind == gas::CurrentMatchKind::AmbiguousUid)
                SetStatus(L"当前 UID 对应多条已保存记录，本次未自动更新。");
            else if (finalState.matchKind == gas::CurrentMatchKind::Inconsistent)
                SetStatus(L"UID 与登录凭据不一致，本次未自动处理。");
            else if (finalState.matchKind == gas::CurrentMatchKind::None && finalState.hasAdl && !finalState.uid.empty())
                SetStatus(L"检测到未保存账号，可在当前账号区域添加。");
            else if (!initialPass && finalState.matchKind == gas::CurrentMatchKind::ExactCredential)
                SetStatus(L"账号状态已同步。");
            else if (initialPass)
                SetStatus(L"");
        }
        catch (...)
        {
            RefreshUi(true, false);
            SetStatus(L"自动同步登录状态时发生异常，本次未修改已保存账号。");
        }
    }

    void MainWindow::OnAddClick(IInspectable const&, RoutedEventArgs const&) { AddCurrentAsync(); }
    void MainWindow::OnUpdateClick(IInspectable const&, RoutedEventArgs const&) { UpdateSelectedAsync(); }
    void MainWindow::OnRenameClick(IInspectable const&, RoutedEventArgs const&) { RenameSelectedAsync(); }
    void MainWindow::OnDeleteClick(IInspectable const&, RoutedEventArgs const&) { DeleteSelectedAsync(); }
    void MainWindow::OnSwitchClick(IInspectable const&, RoutedEventArgs const&) { SwitchSelectedAsync(false); }
    void MainWindow::OnSwitchLaunchClick(IInspectable const&, RoutedEventArgs const&) { SwitchSelectedAsync(true); }
    void MainWindow::OnRestoreClick(IInspectable const&, RoutedEventArgs const&) { RestoreAsync(); }

    fire_and_forget MainWindow::AddCurrentAsync()
    {
        auto lifetime = get_strong();
        std::wstring failure;
        try
        {
            if (m_core.IsGameRunning())
            {
                co_await ShowMessageAsync(L"无法添加", L"原神正在运行。请完全退出游戏后再保存当前登录态。");
                co_return;
            }

            auto probe = m_core.ProbeCurrent();
            auto state = ClassifyProbe(probe, false);
            if (state.matchKind == gas::CurrentMatchKind::Inconsistent)
            {
                co_await ShowMessageAsync(L"登录状态尚未确认", L"当前 UID 与登录凭据暂时不一致，请稍后刷新后再添加。");
                co_return;
            }
            if (!probe.snapshot.adl.exists || probe.snapshot.adl.data.empty())
            {
                co_await ShowMessageAsync(L"无法添加", L"当前注册表中没有可用的原神登录态。");
                co_return;
            }
            if (probe.exactAccountIndex >= 0)
            {
                m_selectedIndex = probe.exactAccountIndex;
                RefreshUi(true, false);
                co_await ShowMessageAsync(L"已经保存", L"当前登录态已经精确匹配一个已保存账号。");
                co_return;
            }

            std::wstring uid = probe.uid;
            if (uid.empty())
            {
                auto input = co_await PromptTextAsync(
                    L"输入 UID",
                    L"当前注册表没有可解析的 UID。请输入这个账号的 UID，仅用于本工具账号标识：",
                    L"", L"例如：123456789");
                uid = Trim(input.c_str());
                if (!gas::AppCore::IsNumericUid(uid))
                {
                    if (!uid.empty()) co_await ShowMessageAsync(L"UID 无效", L"UID 需要由 6–12 位数字组成。");
                    co_return;
                }
            }

            if (probe.uidMatches.size() == 1)
            {
                int existingIndex = probe.uidMatches.front();
                auto existing = m_core.Accounts()[static_cast<size_t>(existingIndex)];
                ContentDialog dialog;
                dialog.XamlRoot(RootGrid().XamlRoot());
                dialog.Title(box_value(L"检测到相同 UID"));
                TextBlock text;
                text.Text(H(L"UID " + uid + L" 已保存为“" + existing.name +
                    L"”，当前登录凭据与保存快照不同。\n\n可以更新现有记录，也可以另存一条新记录。"));
                text.TextWrapping(TextWrapping::Wrap);
                dialog.Content(text);
                dialog.PrimaryButtonText(L"更新现有记录");
                dialog.SecondaryButtonText(L"另存一条");
                dialog.CloseButtonText(L"取消");
                auto choice = co_await dialog.ShowAsync();
                if (choice == ContentDialogResult::Primary)
                {
                    if (!BackupCredential(existing))
                    {
                        co_await ShowMessageAsync(L"更新失败", L"无法创建上一版登录凭据备份，已停止更新。");
                        co_return;
                    }
                    auto result = m_core.UpdateAccount(existingIndex, probe.snapshot);
                    m_selectedIndex = existingIndex;
                    RefreshUi(true, false);
                    co_await ShowMessageAsync(result.success ? L"更新完成" : L"更新失败", result.message);
                    co_return;
                }
                if (choice != ContentDialogResult::Secondary) co_return;
            }
            else if (probe.uidMatches.size() > 1 &&
                !(co_await ConfirmAsync(
                    L"存在多条相同 UID 记录",
                    L"当前 UID 在已保存账号中有多条记录。继续后会把当前登录态另存为新记录。",
                    L"继续另存")))
            {
                co_return;
            }

            auto nameInput = co_await PromptTextAsync(
                L"添加当前账号",
                L"当前 UID：" + uid + L"\n请输入账号昵称：",
                L"", L"例如：主账号");
            auto name = Trim(nameInput.c_str());
            if (name.empty()) co_return;

            m_selectedIndex = m_core.SaveNewAccount(name, uid, probe.snapshot);
            RefreshUi(true, false);
            SetStatus(L"账号已添加。");
        }
        catch (...)
        {
            failure = L"保存当前账号时发生异常。请确认游戏已经完全退出，并检查程序目录写入权限。";
        }
        if (!failure.empty()) co_await ShowMessageAsync(L"添加失败", failure);
    }

    fire_and_forget MainWindow::UpdateSelectedAsync()
    {
        auto lifetime = get_strong();
        if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_core.Accounts().size())) co_return;
        if (m_core.IsGameRunning())
        {
            co_await ShowMessageAsync(L"无法更新", L"原神正在运行。登录信息会在游戏退出后自动同步；如需手动更新，请先完全退出游戏。");
            co_return;
        }

        auto account = m_core.Accounts()[static_cast<size_t>(m_selectedIndex)];
        if (!(co_await ConfirmAsync(
            L"更新登录态",
            L"确定用当前注册表登录态更新“" + account.name + L"”吗？\nUID：" + account.uid,
            L"更新"))) co_return;

        try
        {
            auto probe = m_core.ProbeCurrent();
            auto state = ClassifyProbe(probe, false);
            if (state.matchKind == gas::CurrentMatchKind::Inconsistent)
            {
                co_await ShowMessageAsync(L"更新失败", L"当前 UID 与登录凭据不一致，已停止更新。");
                co_return;
            }
            if (!BackupCredential(account))
            {
                co_await ShowMessageAsync(L"更新失败", L"无法创建上一版登录凭据备份，已停止更新。");
                co_return;
            }
            auto result = m_core.UpdateAccount(m_selectedIndex, probe.snapshot);
            RefreshUi(true, false);
            co_await ShowMessageAsync(result.success ? L"更新完成" : L"更新失败", result.message);
        }
        catch (...)
        {
            co_await ShowMessageAsync(L"更新失败", L"无法读取当前注册表登录态。");
        }
    }

    fire_and_forget MainWindow::RenameSelectedAsync()
    {
        auto lifetime = get_strong();
        if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_core.Accounts().size())) co_return;
        auto oldName = m_core.Accounts()[static_cast<size_t>(m_selectedIndex)].name;
        auto input = co_await PromptTextAsync(L"重命名账号", L"请输入新的账号昵称：", oldName, L"");
        auto name = Trim(input.c_str());
        if (name.empty() || name == oldName) co_return;
        auto result = m_core.RenameAccount(m_selectedIndex, name);
        RefreshUi(true, false);
        co_await ShowMessageAsync(result.success ? L"重命名完成" : L"重命名失败", result.message);
    }

    fire_and_forget MainWindow::DeleteSelectedAsync()
    {
        auto lifetime = get_strong();
        if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_core.Accounts().size())) co_return;
        auto account = m_core.Accounts()[static_cast<size_t>(m_selectedIndex)];
        if (!(co_await ConfirmAsync(
            L"删除账号",
            L"确定删除“" + account.name + L"”吗？\n\n只会删除本工具保存的本地快照。",
            L"删除"))) co_return;

        auto result = m_core.DeleteAccount(m_selectedIndex);
        if (result.success) RemoveCredentialBackup(account);
        m_selectedIndex = -1;
        RefreshUi(true, false);
        co_await ShowMessageAsync(result.success ? L"删除完成" : L"删除失败", result.message);
    }

    fire_and_forget MainWindow::SwitchSelectedAsync(bool launchAfter)
    {
        auto lifetime = get_strong();
        if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_core.Accounts().size())) co_return;
        if (m_core.IsGameRunning())
        {
            co_await ShowMessageAsync(L"无法切换", L"原神正在运行。请完全退出原神后再切换账号。");
            co_return;
        }

        int targetIndex = m_selectedIndex;
        auto account = m_core.Accounts()[static_cast<size_t>(targetIndex)];
        gas::RegistrySnapshot before{};
        bool haveBefore = false;
        try
        {
            before = m_core.ProbeCurrent().snapshot;
            haveBefore = true;
        }
        catch (...) {}

        auto result = m_core.SwitchAccount(targetIndex);
        if (!result.success)
        {
            std::wstring rollbackText;
            if (haveBefore)
            {
                try
                {
                    auto after = m_core.ProbeCurrent().snapshot;
                    if (!IdentitySnapshotEquals(before, after))
                    {
                        auto rollback = m_core.RestoreLastSnapshot();
                        rollbackText = rollback.success
                            ? L"\n\n检测到注册表发生部分变化，已自动恢复切换前状态。"
                            : L"\n\n检测到注册表发生部分变化，自动恢复失败。可在“更多”中再次尝试恢复。";
                    }
                }
                catch (...) {}
            }
            RefreshUi(true, false);
            co_await ShowMessageAsync(L"切换失败", result.message + rollbackText);
            co_return;
        }

        RefreshUi(true, false);
        if (!launchAfter)
        {
            SetStatus(L"已切换到“" + account.name + L"”。");
            co_return;
        }

        auto verifyProbe = m_core.ProbeCurrent();
        auto verifyState = ClassifyProbe(verifyProbe, false);
        if (verifyState.accountIndex != targetIndex || verifyState.matchKind != gas::CurrentMatchKind::ExactCredential)
        {
            co_await ShowMessageAsync(L"状态复核失败", L"注册表写入完成后没有精确匹配目标账号，因此没有启动游戏。");
            co_return;
        }

        auto launch = m_core.LaunchGame();
        if (!launch.success)
            co_await ShowMessageAsync(L"启动失败", launch.message);
        else
            SetStatus(L"已切换并启动“" + account.name + L"”。");
    }

    fire_and_forget MainWindow::RestoreAsync()
    {
        auto lifetime = get_strong();
        if (!(co_await ConfirmAsync(
            L"恢复上一次状态",
            L"确定恢复最近一次账号切换前保存的注册表快照吗？该功能主要用于切换异常后的人工恢复。",
            L"恢复"))) co_return;

        auto result = m_core.RestoreLastSnapshot();
        RefreshUi(true, false);
        co_await ShowMessageAsync(result.success ? L"恢复完成" : L"恢复失败", result.message);
    }

    void MainWindow::OnSelectGamePathClick(IInspectable const&, RoutedEventArgs const&)
    {
        auto path = ChooseGameExecutable();
        if (path.empty()) return;
        auto result = m_core.SetManualGameExecutable(path);
        SetStatus(result.success ? L"原神路径已保存。" : result.message);
        if (!result.success) ShowMessage(L"路径无效", result.message);
    }

    void MainWindow::OnOpenDataDirectoryClick(IInspectable const&, RoutedEventArgs const&)
    {
        OpenPath(m_core.DataDirectory());
    }

    void MainWindow::OnOpenLogClick(IInspectable const&, RoutedEventArgs const&)
    {
        OpenPath(m_core.LogFile());
    }

    void MainWindow::OpenPath(std::filesystem::path const& path)
    {
        if (path.empty()) return;
        auto text = path.wstring();
        auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(WindowHandle(), L"open", text.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32) SetStatus(L"无法打开指定路径。");
    }

    std::wstring MainWindow::ChooseGameExecutable()
    {
        wchar_t fileBuffer[32768] = L"YuanShen.exe";
        static wchar_t filter[] = L"原神国服 (YuanShen.exe)\0YuanShen.exe\0可执行文件 (*.exe)\0*.exe\0\0";
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = WindowHandle();
        dialog.lpstrFilter = filter;
        dialog.lpstrFile = fileBuffer;
        dialog.nMaxFile = static_cast<DWORD>(std::size(fileBuffer));
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
        if (!GetOpenFileNameW(&dialog)) return {};
        return fileBuffer;
    }

    winrt::Windows::Foundation::IAsyncOperation<hstring> MainWindow::PromptTextAsync(
        std::wstring const& title,
        std::wstring const& message,
        std::wstring const& initial,
        std::wstring const& placeholder)
    {
        StackPanel panel;
        panel.Spacing(10);
        TextBlock description;
        description.Text(H(message));
        description.TextWrapping(TextWrapping::Wrap);
        panel.Children().Append(description);
        TextBox input;
        input.Text(H(initial));
        input.PlaceholderText(H(placeholder));
        panel.Children().Append(input);

        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(H(title)));
        dialog.Content(panel);
        dialog.PrimaryButtonText(L"确定");
        dialog.CloseButtonText(L"取消");
        dialog.DefaultButton(ContentDialogButton::Primary);
        auto result = co_await dialog.ShowAsync();
        if (result != ContentDialogResult::Primary) co_return hstring{};
        co_return input.Text();
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> MainWindow::ConfirmAsync(
        std::wstring const& title,
        std::wstring const& message,
        std::wstring const& primary)
    {
        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(H(title)));
        TextBlock text;
        text.Text(H(message));
        text.TextWrapping(TextWrapping::Wrap);
        dialog.Content(text);
        dialog.PrimaryButtonText(H(primary));
        dialog.CloseButtonText(L"取消");
        dialog.DefaultButton(ContentDialogButton::Primary);
        auto result = co_await dialog.ShowAsync();
        co_return result == ContentDialogResult::Primary;
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::ShowMessageAsync(
        std::wstring const& title,
        std::wstring const& message)
    {
        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(H(title)));
        TextBlock text;
        text.Text(H(message));
        text.TextWrapping(TextWrapping::Wrap);
        dialog.Content(text);
        dialog.CloseButtonText(L"确定");
        co_await dialog.ShowAsync();
    }

    void MainWindow::ShowMessage(std::wstring const& title, std::wstring const& message)
    {
        auto action = ShowMessageAsync(title, message);
        (void)action;
    }
}
