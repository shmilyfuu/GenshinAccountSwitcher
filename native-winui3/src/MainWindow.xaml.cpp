#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <microsoft.ui.xaml.window.h>
#include <dwmapi.h>
#include <commdlg.h>

#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Comdlg32.lib")

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
            DataPathText().Text(H(m_core.DataDirectory().wstring()));
            RefreshUi(true);
        }
        catch (...)
        {
            m_initialized = false;
            CurrentAccountText().Text(L"初始化失败");
            CurrentUidText().Text(L"UID —");
            GamePathText().Text(L"—");
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
        SetWindowPos(hwnd, nullptr, 0, 0, static_cast<int>(800 * scale), static_cast<int>(720 * scale), SWP_NOMOVE | SWP_NOZORDER);
    }

    void MainWindow::OnWindowActivated(IInspectable const&, WindowActivatedEventArgs const& args)
    {
        if (args.WindowActivationState() == WindowActivationState::Deactivated) return;
        if (m_firstActivation) { m_firstActivation = false; return; }
        if (m_initialized) RefreshUi(false);
    }

    void MainWindow::OnRefreshClick(IInspectable const&, RoutedEventArgs const&) { if (m_initialized) RefreshUi(true); }
    void MainWindow::SetStatus(std::wstring const& text) { StatusText().Text(H(text)); }

    void MainWindow::RefreshUi(bool rebuildAccounts)
    {
        if (!m_initialized) return;
        try
        {
            m_currentState = m_core.DetectCurrentState();
            auto const& accounts = m_core.Accounts();
            if (m_currentState.uid.empty()) CurrentUidText().Text(L"UID —");
            else CurrentUidText().Text(H(L"UID " + m_currentState.uid));
            CredentialStateText().Text(L"");

            if (m_currentState.matchKind == gas::CurrentMatchKind::ExactCredential && m_currentState.accountIndex >= 0)
                CurrentAccountText().Text(H(accounts[static_cast<size_t>(m_currentState.accountIndex)].name));
            else if (m_currentState.matchKind == gas::CurrentMatchKind::UniqueUidCredentialChanged && m_currentState.accountIndex >= 0)
            {
                CurrentAccountText().Text(H(accounts[static_cast<size_t>(m_currentState.accountIndex)].name));
                CredentialStateText().Text(L"登录凭据已变化，可更新登录态");
            }
            else if (m_currentState.matchKind == gas::CurrentMatchKind::AmbiguousUid)
            {
                CurrentAccountText().Text(L"UID 已保存（存在多条记录）");
                CredentialStateText().Text(L"请选择对应记录后更新登录态");
            }
            else CurrentAccountText().Text(m_currentState.hasAdl ? L"未保存账号" : L"未检测到登录态");

            if (m_currentState.gameRunning)
            {
                GameStateText().Text(L"原神运行中");
                GameStateBadge().Background(MakeBrush(0xFF, 0x4A, 0x25, 0x25));
            }
            else
            {
                GameStateText().Text(L"游戏未运行");
                GameStateBadge().Background(MakeBrush(0xFF, 0x25, 0x37, 0x46));
            }
            if (m_currentState.gamePath.empty()) GamePathText().Text(L"未找到，请手动指定");
            else GamePathText().Text(H(m_currentState.gamePath));
            AccountCountText().Text(H(std::to_wstring(accounts.size()) + L" 个"));

            if (m_selectedIndex >= static_cast<int>(accounts.size())) m_selectedIndex = -1;
            if (m_selectedIndex < 0 && m_currentState.accountIndex >= 0) m_selectedIndex = m_currentState.accountIndex;
            if (rebuildAccounts) RebuildAccounts(); else UpdateAccountVisuals();
            UpdateButtonStates();
            SetStatus(m_currentState.matchKind == gas::CurrentMatchKind::UniqueUidCredentialChanged
                ? L"已按 UID 识别为已保存账号；当前凭据与保存快照不同。"
                : L"状态已刷新。");
        }
        catch (...) { SetStatus(L"刷新失败：无法读取当前注册表或本地账号数据。"); }
    }

    MainWindow::AccountVisual MainWindow::BuildAccountVisual(int index)
    {
        auto const& account = m_core.Accounts().at(static_cast<size_t>(index));
        AccountVisual v;
        v.item = ListViewItem();
        v.item.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        v.item.Padding(MakeThickness(0, 0, 0, 0));
        v.item.Margin(MakeThickness(0, 0, 0, 6));
        v.item.MinHeight(50);

        v.card = Border();
        v.card.CornerRadius(CornerRadius{ 8 });
        v.card.BorderThickness(Thickness{ 1 });
        v.card.Padding(MakeThickness(0, 0, 12, 0));

        Grid row; row.Height(50);
        ColumnDefinition c0; c0.Width(GridLength{ 4, GridUnitType::Pixel });
        ColumnDefinition c1; c1.Width(GridLength{ 1, GridUnitType::Star });
        ColumnDefinition c2; c2.Width(GridLength{ 150, GridUnitType::Pixel });
        ColumnDefinition c3; c3.Width(GridLength{ 132, GridUnitType::Pixel });
        row.ColumnDefinitions().Append(c0); row.ColumnDefinitions().Append(c1); row.ColumnDefinitions().Append(c2); row.ColumnDefinitions().Append(c3);

        v.accent = Border(); v.accent.CornerRadius(CornerRadius{ 2 }); v.accent.Margin(MakeThickness(0, 7, 0, 7)); row.Children().Append(v.accent);
        TextBlock name; name.Text(H(account.name)); name.FontSize(14); name.FontWeight(Windows::UI::Text::FontWeights::SemiBold()); name.VerticalAlignment(VerticalAlignment::Center); name.Margin(MakeThickness(12, 0, 8, 0)); Grid::SetColumn(name, 1); row.Children().Append(name);
        TextBlock uid; uid.Text(H(L"UID " + account.uid)); uid.FontSize(12); uid.Opacity(0.62); uid.VerticalAlignment(VerticalAlignment::Center); Grid::SetColumn(uid, 2); row.Children().Append(uid);
        v.badge = Border(); v.badge.CornerRadius(CornerRadius{ 9 }); v.badge.Padding(MakeThickness(9, 3, 9, 3)); v.badge.HorizontalAlignment(HorizontalAlignment::Left); v.badge.VerticalAlignment(VerticalAlignment::Center); v.badge.Visibility(Visibility::Collapsed);
        v.badgeText = TextBlock(); v.badgeText.FontSize(11); v.badgeText.FontWeight(Windows::UI::Text::FontWeights::SemiBold()); v.badge.Child(v.badgeText); Grid::SetColumn(v.badge, 3); row.Children().Append(v.badge);
        v.card.Child(row); v.item.Content(v.card);
        return v;
    }

    void MainWindow::RebuildAccounts()
    {
        m_rebuildingAccounts = true;
        AccountsList().Items().Clear(); m_visuals.clear();
        for (int i = 0; i < static_cast<int>(m_core.Accounts().size()); ++i)
        {
            auto v = BuildAccountVisual(i); AccountsList().Items().Append(v.item); m_visuals.push_back(std::move(v));
        }
        AccountsList().SelectedIndex(m_selectedIndex);
        m_rebuildingAccounts = false; UpdateAccountVisuals();
    }

    void MainWindow::UpdateAccountVisuals()
    {
        for (int i = 0; i < static_cast<int>(m_visuals.size()); ++i)
        {
            auto& v = m_visuals[static_cast<size_t>(i)];
            bool selected = i == m_selectedIndex;
            bool current = i == m_currentState.accountIndex && (m_currentState.matchKind == gas::CurrentMatchKind::ExactCredential || m_currentState.matchKind == gas::CurrentMatchKind::UniqueUidCredentialChanged);
            bool changed = current && m_currentState.matchKind == gas::CurrentMatchKind::UniqueUidCredentialChanged;
            v.card.Background(selected ? MakeBrush(0xFF, 0x36, 0x36, 0x36) : MakeBrush(0xFF, 0x29, 0x29, 0x29));
            v.card.BorderBrush(selected ? MakeBrush(0xFF, 0x68, 0x68, 0x68) : MakeBrush(0xFF, 0x3A, 0x3A, 0x3A));
            v.accent.Background(selected ? MakeBrush(0xFF, 0x60, 0xCD, 0xFF) : MakeBrush(0x00, 0, 0, 0));
            if (current)
            {
                v.badge.Visibility(Visibility::Visible); v.badge.Background(changed ? MakeBrush(0xFF, 0x4C, 0x3F, 0x20) : MakeBrush(0xFF, 0x1F, 0x45, 0x57)); v.badgeText.Text(changed ? L"当前 · 待更新" : L"当前账号");
            }
            else if (selected)
            {
                v.badge.Visibility(Visibility::Visible); v.badge.Background(MakeBrush(0xFF, 0x2A, 0x45, 0x58)); v.badgeText.Text(L"已选中");
            }
            else v.badge.Visibility(Visibility::Collapsed);
        }
    }

    void MainWindow::OnAccountSelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_rebuildingAccounts) return;
        m_selectedIndex = AccountsList().SelectedIndex(); UpdateAccountVisuals(); UpdateButtonStates();
    }

    void MainWindow::UpdateButtonStates()
    {
        bool selected = m_initialized && m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_core.Accounts().size());
        bool writable = m_initialized && !m_currentState.gameRunning;
        SwitchButton().IsEnabled(selected && writable); SwitchLaunchButton().IsEnabled(selected && writable); UpdateButton().IsEnabled(selected && writable);
        RenameButton().IsEnabled(selected); DeleteButton().IsEnabled(selected); AddButton().IsEnabled(writable); RestoreButton().IsEnabled(writable && m_core.RecoveryExists());
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
            auto probe = m_core.ProbeCurrent();
            if (m_core.IsGameRunning()) { co_await ShowMessageAsync(L"无法添加", L"原神正在运行，请完全退出游戏后再保存当前登录态。"); co_return; }
            if (!probe.snapshot.adl.exists || probe.snapshot.adl.data.empty()) { co_await ShowMessageAsync(L"无法添加", L"当前注册表中没有可用的原神登录态。"); co_return; }
            if (probe.exactAccountIndex >= 0)
            {
                m_selectedIndex = probe.exactAccountIndex; RefreshUi(true); co_await ShowMessageAsync(L"已经保存", L"当前登录态已经精确匹配一个已保存账号。"); co_return;
            }
            std::wstring uid = probe.uid;
            if (uid.empty())
            {
                auto input = co_await PromptTextAsync(L"输入 UID", L"当前注册表没有可解析的 UID。请输入这个账号的 UID，仅用于本工具账号标识：", L"", L"例如：123456789");
                uid = Trim(input.c_str());
                if (!gas::AppCore::IsNumericUid(uid)) { if (!uid.empty()) co_await ShowMessageAsync(L"UID 无效", L"UID 需要由 6–12 位数字组成。"); co_return; }
            }
            if (probe.uidMatches.size() == 1)
            {
                int existingIndex = probe.uidMatches.front(); auto existing = m_core.Accounts()[static_cast<size_t>(existingIndex)];
                ContentDialog dialog; dialog.XamlRoot(RootGrid().XamlRoot()); dialog.Title(box_value(L"检测到相同 UID"));
                TextBlock text; text.Text(H(L"UID " + uid + L" 已保存为“" + existing.name + L"”，当前登录凭据与保存快照不同。\n\n可以更新现有记录，也可以另存一条新记录。")); text.TextWrapping(TextWrapping::Wrap); dialog.Content(text);
                dialog.PrimaryButtonText(L"更新现有记录"); dialog.SecondaryButtonText(L"另存一条"); dialog.CloseButtonText(L"取消");
                auto choice = co_await dialog.ShowAsync();
                if (choice == ContentDialogResult::Primary)
                {
                    auto result = m_core.UpdateAccount(existingIndex, probe.snapshot); m_selectedIndex = existingIndex; RefreshUi(true); co_await ShowMessageAsync(result.success ? L"更新完成" : L"更新失败", result.message); co_return;
                }
                if (choice != ContentDialogResult::Secondary) co_return;
            }
            else if (probe.uidMatches.size() > 1 && !(co_await ConfirmAsync(L"存在多条相同 UID 记录", L"当前 UID 在已保存账号中有多条记录。继续后会把当前登录态另存为新记录。", L"继续另存"))) co_return;

            auto nameInput = co_await PromptTextAsync(L"添加当前账号", L"当前 UID：" + uid + L"\n请输入账号昵称：", L"", L"例如：主账号");
            auto name = Trim(nameInput.c_str()); if (name.empty()) co_return;
            m_selectedIndex = m_core.SaveNewAccount(name, uid, probe.snapshot); RefreshUi(true);
            co_await ShowMessageAsync(L"添加完成", L"账号已保存，登录凭据使用当前 Windows 用户的 DPAPI 加密。");
        }
        catch (...) { failure = L"保存当前账号时发生异常。请确认游戏已经完全退出，并检查程序目录写入权限。"; }
        if (!failure.empty()) co_await ShowMessageAsync(L"添加失败", failure);
    }

    fire_and_forget MainWindow::UpdateSelectedAsync()
    {
        auto lifetime = get_strong();
        if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_core.Accounts().size())) co_return;
        auto account = m_core.Accounts()[static_cast<size_t>(m_selectedIndex)];
        if (!(co_await ConfirmAsync(L"更新登录态", L"确定用当前注册表登录态更新“" + account.name + L"”吗？\nUID：" + account.uid, L"更新"))) co_return;
        std::wstring failure;
        try
        {
            auto result = m_core.UpdateAccount(m_selectedIndex, m_core.ProbeCurrent().snapshot); RefreshUi(true); co_await ShowMessageAsync(result.success ? L"更新完成" : L"更新失败", result.message);
        }
        catch (...) { failure = L"无法读取当前注册表登录态。"; }
        if (!failure.empty()) co_await ShowMessageAsync(L"更新失败", failure);
    }

    fire_and_forget MainWindow::RenameSelectedAsync()
    {
        auto lifetime = get_strong(); if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_core.Accounts().size())) co_return;
        auto oldName = m_core.Accounts()[static_cast<size_t>(m_selectedIndex)].name;
        auto input = co_await PromptTextAsync(L"重命名账号", L"请输入新的账号昵称：", oldName, L""); auto name = Trim(input.c_str()); if (name.empty() || name == oldName) co_return;
        auto result = m_core.RenameAccount(m_selectedIndex, name); RefreshUi(true); co_await ShowMessageAsync(result.success ? L"重命名完成" : L"重命名失败", result.message);
    }

    fire_and_forget MainWindow::DeleteSelectedAsync()
    {
        auto lifetime = get_strong(); if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_core.Accounts().size())) co_return;
        auto account = m_core.Accounts()[static_cast<size_t>(m_selectedIndex)];
        if (!(co_await ConfirmAsync(L"删除账号", L"确定删除“" + account.name + L"”吗？\n\n只会删除本工具保存的本地快照。", L"删除"))) co_return;
        auto result = m_core.DeleteAccount(m_selectedIndex); m_selectedIndex = -1; RefreshUi(true); co_await ShowMessageAsync(result.success ? L"删除完成" : L"删除失败", result.message);
    }

    fire_and_forget MainWindow::SwitchSelectedAsync(bool launchAfter)
    {
        auto lifetime = get_strong(); if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_core.Accounts().size())) co_return;
        int targetIndex = m_selectedIndex; auto account = m_core.Accounts()[static_cast<size_t>(targetIndex)];
        auto result = m_core.SwitchAccount(targetIndex); RefreshUi(true);
        if (!result.success) { co_await ShowMessageAsync(L"切换失败", result.message); co_return; }
        if (!launchAfter) { co_await ShowMessageAsync(L"切换完成", L"当前账号：" + account.name + L"\nUID：" + account.uid); co_return; }
        auto state = m_core.DetectCurrentState();
        if (state.accountIndex != targetIndex || state.matchKind != gas::CurrentMatchKind::ExactCredential) { co_await ShowMessageAsync(L"状态复核失败", L"注册表写入完成后没有精确匹配目标账号，因此没有启动游戏。"); co_return; }
        auto launch = m_core.LaunchGame(); if (!launch.success) co_await ShowMessageAsync(L"启动失败", launch.message); else SetStatus(L"已切换并启动：" + account.name);
    }

    fire_and_forget MainWindow::RestoreAsync()
    {
        auto lifetime = get_strong(); if (!(co_await ConfirmAsync(L"恢复上一次状态", L"确定恢复最近一次账号切换前保存的注册表快照吗？", L"恢复"))) co_return;
        auto result = m_core.RestoreLastSnapshot(); RefreshUi(true); co_await ShowMessageAsync(result.success ? L"恢复完成" : L"恢复失败", result.message);
    }

    void MainWindow::OnSelectGamePathClick(IInspectable const&, RoutedEventArgs const&)
    {
        auto path = ChooseGameExecutable(); if (path.empty()) return;
        auto result = m_core.SetManualGameExecutable(path); RefreshUi(false); ShowMessage(result.success ? L"路径已保存" : L"路径无效", result.message);
    }

    std::wstring MainWindow::ChooseGameExecutable()
    {
        wchar_t fileBuffer[32768] = L"YuanShen.exe";
        static wchar_t filter[] = L"原神国服 (YuanShen.exe)\0YuanShen.exe\0可执行文件 (*.exe)\0*.exe\0\0";
        OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = WindowHandle(); dialog.lpstrFilter = filter; dialog.lpstrFile = fileBuffer; dialog.nMaxFile = static_cast<DWORD>(std::size(fileBuffer)); dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
        if (!GetOpenFileNameW(&dialog)) return {}; return fileBuffer;
    }

    winrt::Windows::Foundation::IAsyncOperation<hstring> MainWindow::PromptTextAsync(std::wstring const& title, std::wstring const& message, std::wstring const& initial, std::wstring const& placeholder)
    {
        StackPanel panel; panel.Spacing(10);
        TextBlock description; description.Text(H(message)); description.TextWrapping(TextWrapping::Wrap); panel.Children().Append(description);
        TextBox input; input.Text(H(initial)); input.PlaceholderText(H(placeholder)); panel.Children().Append(input);
        ContentDialog dialog; dialog.XamlRoot(RootGrid().XamlRoot()); dialog.Title(box_value(H(title))); dialog.Content(panel); dialog.PrimaryButtonText(L"确定"); dialog.CloseButtonText(L"取消"); dialog.DefaultButton(ContentDialogButton::Primary);
        auto result = co_await dialog.ShowAsync(); if (result != ContentDialogResult::Primary) co_return hstring{}; co_return input.Text();
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> MainWindow::ConfirmAsync(std::wstring const& title, std::wstring const& message, std::wstring const& primary)
    {
        ContentDialog dialog; dialog.XamlRoot(RootGrid().XamlRoot()); dialog.Title(box_value(H(title)));
        TextBlock text; text.Text(H(message)); text.TextWrapping(TextWrapping::Wrap); dialog.Content(text); dialog.PrimaryButtonText(H(primary)); dialog.CloseButtonText(L"取消"); dialog.DefaultButton(ContentDialogButton::Primary);
        auto result = co_await dialog.ShowAsync(); co_return result == ContentDialogResult::Primary;
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::ShowMessageAsync(std::wstring const& title, std::wstring const& message)
    {
        ContentDialog dialog; dialog.XamlRoot(RootGrid().XamlRoot()); dialog.Title(box_value(H(title)));
        TextBlock text; text.Text(H(message)); text.TextWrapping(TextWrapping::Wrap); dialog.Content(text); dialog.CloseButtonText(L"确定"); co_await dialog.ShowAsync();
    }

    void MainWindow::ShowMessage(std::wstring const& title, std::wstring const& message)
    {
        auto action = ShowMessageAsync(title, message); (void)action;
    }
}
