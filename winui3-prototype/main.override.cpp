#include <windows.h>
#undef GetCurrentTime

#include <winrt/base.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.Graphics.h>

#include <array>
#include <string>
#include <vector>

using namespace winrt;
using namespace Microsoft::UI;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Shapes;
using namespace Windows::UI;
using namespace Windows::UI::Text;

namespace
{
    SolidColorBrush Brush(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
    {
        return SolidColorBrush(Color{ a, r, g, b });
    }

    Thickness T(double all) { return Thickness{ all, all, all, all }; }
    Thickness T(double l, double t, double r, double b) { return Thickness{ l, t, r, b }; }

    TextBlock MakeText(hstring const& text, double size = 14.0, FontWeight weight = FontWeights::Normal())
    {
        TextBlock tb;
        tb.Text(text);
        tb.FontSize(size);
        tb.FontWeight(weight);
        tb.VerticalAlignment(VerticalAlignment::Center);
        return tb;
    }

    struct AccountRow
    {
        hstring name;
        hstring uid;
        bool current;
    };

    class PrototypeApp : public ApplicationT<PrototypeApp>
    {
    public:
        PrototypeApp()
        {
            ResourceDictionary resources;
            resources.MergedDictionaries().Append(XamlControlsResources());
            Resources(resources);
        }

        void OnLaunched(LaunchActivatedEventArgs const&)
        {
            m_window = Window();
            m_window.Title(L"原神账号切换器 · WinUI 3 UI 原型");

            try
            {
                m_window.SystemBackdrop(MicaBackdrop());
            }
            catch (...) {}

            m_window.Content(BuildRoot());
            m_window.Activate();

            try
            {
                m_window.AppWindow().Resize(Windows::Graphics::SizeInt32{ 760, 650 });
            }
            catch (...) {}
        }

    private:
        UIElement BuildRoot()
        {
            Grid root;
            root.Padding(T(28, 20, 28, 24));
            root.RowDefinitions().Append(RowDefinition());
            root.Background(Brush(0x22, 0xFF, 0xFF, 0xFF));

            ScrollViewer scroll;
            scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
            scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);

            StackPanel page;
            page.Spacing(14);

            Grid header;
            header.ColumnDefinitions().Append(ColumnDefinition());
            auto right = ColumnDefinition();
            right.Width(GridLengthHelper::Auto());
            header.ColumnDefinitions().Append(right);

            StackPanel titleBox;
            titleBox.Spacing(2);
            auto title = MakeText(L"原神账号切换器", 25, FontWeights::SemiBold());
            titleBox.Children().Append(title);
            auto subtitle = MakeText(L"WinUI 3 · 原生 C++ UI 测试版", 12);
            subtitle.Opacity(0.62);
            titleBox.Children().Append(subtitle);
            header.Children().Append(titleBox);

            Border protoBadge;
            protoBadge.Background(Brush(0x18, 0x00, 0x78, 0xD4));
            protoBadge.BorderBrush(Brush(0x55, 0x00, 0x78, 0xD4));
            protoBadge.BorderThickness(T(1));
            protoBadge.CornerRadius(CornerRadius{ 8 });
            protoBadge.Padding(T(10, 5, 10, 5));
            auto protoText = MakeText(L"UI 原型", 12, FontWeights::SemiBold());
            protoText.Foreground(Brush(0xFF, 0x00, 0x63, 0xB1));
            protoBadge.Child(protoText);
            Grid::SetColumn(protoBadge, 1);
            protoBadge.VerticalAlignment(VerticalAlignment::Top);
            header.Children().Append(protoBadge);
            page.Children().Append(header);

            Border currentCard;
            currentCard.CornerRadius(CornerRadius{ 12 });
            currentCard.Padding(T(16, 13, 16, 13));
            currentCard.Background(Brush(0xA8, 0xFF, 0xFF, 0xFF));
            currentCard.BorderBrush(Brush(0x35, 0x00, 0x00, 0x00));
            currentCard.BorderThickness(T(1));

            Grid currentGrid;
            currentGrid.ColumnDefinitions().Append(ColumnDefinition());
            auto currentRight = ColumnDefinition();
            currentRight.Width(GridLengthHelper::Auto());
            currentGrid.ColumnDefinitions().Append(currentRight);

            StackPanel currentLeft;
            currentLeft.Spacing(3);
            auto currentLabel = MakeText(L"当前注册表账号", 12);
            currentLabel.Opacity(0.58);
            currentLeft.Children().Append(currentLabel);
            StackPanel currentLine;
            currentLine.Orientation(Orientation::Horizontal);
            currentLine.Spacing(10);
            auto currentName = MakeText(L"主账号", 17, FontWeights::SemiBold());
            currentLine.Children().Append(currentName);
            auto uid = MakeText(L"UID 123456789", 13);
            uid.Opacity(0.66);
            currentLine.Children().Append(uid);
            currentLeft.Children().Append(currentLine);
            currentGrid.Children().Append(currentLeft);

            Border currentBadge;
            currentBadge.CornerRadius(CornerRadius{ 10 });
            currentBadge.Background(Brush(0x22, 0x10, 0x8A, 0x43));
            currentBadge.Padding(T(10, 5, 10, 5));
            auto currentBadgeText = MakeText(L"已保存 · 凭据一致", 12, FontWeights::SemiBold());
            currentBadgeText.Foreground(Brush(0xFF, 0x0A, 0x6A, 0x35));
            currentBadge.Child(currentBadgeText);
            Grid::SetColumn(currentBadge, 1);
            currentBadge.VerticalAlignment(VerticalAlignment::Center);
            currentGrid.Children().Append(currentBadge);
            currentCard.Child(currentGrid);
            page.Children().Append(currentCard);

            Grid listHeader;
            listHeader.ColumnDefinitions().Append(ColumnDefinition());
            auto countCol = ColumnDefinition();
            countCol.Width(GridLengthHelper::Auto());
            listHeader.ColumnDefinitions().Append(countCol);
            auto savedTitle = MakeText(L"已保存账号", 15, FontWeights::SemiBold());
            listHeader.Children().Append(savedTitle);
            auto savedCount = MakeText(L"6 个", 12);
            savedCount.Opacity(0.55);
            Grid::SetColumn(savedCount, 1);
            listHeader.Children().Append(savedCount);
            page.Children().Append(listHeader);

            Border listPanel;
            listPanel.CornerRadius(CornerRadius{ 12 });
            listPanel.Background(Brush(0xA0, 0xFF, 0xFF, 0xFF));
            listPanel.BorderBrush(Brush(0x30, 0x00, 0x00, 0x00));
            listPanel.BorderThickness(T(1));
            listPanel.Padding(T(8));

            m_accounts = {
                {L"主账号", L"123456789", true},
                {L"深渊小号", L"234567890", false},
                {L"探索号", L"345678901", false},
                {L"测试账号", L"456789012", false},
                {L"好友联机", L"567890123", false},
                {L"备用账号", L"678901234", false},
            };

            m_accountStack = StackPanel();
            m_accountStack.Spacing(4);
            for (int i = 0; i < static_cast<int>(m_accounts.size()); ++i)
            {
                auto card = BuildAccountCard(i);
                m_accountButtons.push_back(card);
                m_accountStack.Children().Append(card);
            }
            listPanel.Child(m_accountStack);
            page.Children().Append(listPanel);

            Grid actions;
            actions.ColumnSpacing(10);
            auto half1 = ColumnDefinition();
            half1.Width(GridLength{ 1, GridUnitType::Star });
            auto half2 = ColumnDefinition();
            half2.Width(GridLength{ 1, GridUnitType::Star });
            actions.ColumnDefinitions().Append(half1);
            actions.ColumnDefinitions().Append(half2);

            Button switchButton;
            switchButton.Content(box_value(L"切换账号"));
            switchButton.Height(42);
            switchButton.HorizontalAlignment(HorizontalAlignment::Stretch);
            switchButton.HorizontalContentAlignment(HorizontalAlignment::Center);
            switchButton.Click([this](auto&&, auto&&) { ShowInfo(L"UI 原型：未执行真实账号切换。", false); });
            actions.Children().Append(switchButton);

            Button launchButton;
            launchButton.Content(box_value(L"切换并启动"));
            launchButton.Height(42);
            launchButton.HorizontalAlignment(HorizontalAlignment::Stretch);
            launchButton.HorizontalContentAlignment(HorizontalAlignment::Center);
            launchButton.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
            launchButton.Click([this](auto&&, auto&&) { ShowInfo(L"UI 原型：这里将来会执行切换并启动原神。", false); });
            Grid::SetColumn(launchButton, 1);
            actions.Children().Append(launchButton);
            page.Children().Append(actions);

            StackPanel secondary;
            secondary.Orientation(Orientation::Horizontal);
            secondary.Spacing(8);
            const std::array<hstring, 4> labels{ L"添加当前账号", L"更新", L"重命名", L"删除" };
            for (auto const& label : labels)
            {
                Button b;
                b.Content(box_value(label));
                b.MinWidth(86);
                b.Height(34);
                b.Click([this, label](auto&&, auto&&)
                {
                    ShowInfo(L"UI 原型：已点击“" + label + L"”。", false);
                });
                secondary.Children().Append(b);
            }
            page.Children().Append(secondary);

            Border settings;
            settings.CornerRadius(CornerRadius{ 12 });
            settings.Padding(T(14, 12, 14, 12));
            settings.Background(Brush(0x70, 0xFF, 0xFF, 0xFF));
            settings.BorderBrush(Brush(0x24, 0x00, 0x00, 0x00));
            settings.BorderThickness(T(1));

            Grid pathGrid;
            pathGrid.ColumnSpacing(10);
            auto labelCol = ColumnDefinition();
            labelCol.Width(GridLengthHelper::Auto());
            auto pathCol = ColumnDefinition();
            pathCol.Width(GridLength{ 1, GridUnitType::Star });
            auto browseCol = ColumnDefinition();
            browseCol.Width(GridLengthHelper::Auto());
            pathGrid.ColumnDefinitions().Append(labelCol);
            pathGrid.ColumnDefinitions().Append(pathCol);
            pathGrid.ColumnDefinitions().Append(browseCol);

            auto pathLabel = MakeText(L"游戏路径", 13, FontWeights::SemiBold());
            pathGrid.Children().Append(pathLabel);

            TextBox pathBox;
            pathBox.Text(L"D:\\Games\\Genshin Impact\\Genshin Impact Game\\YuanShen.exe");
            pathBox.IsReadOnly(true);
            pathBox.Height(34);
            Grid::SetColumn(pathBox, 1);
            pathGrid.Children().Append(pathBox);

            Button browse;
            browse.Content(box_value(L"浏览"));
            browse.Height(34);
            browse.Click([this](auto&&, auto&&) { ShowInfo(L"UI 原型：路径选择器未接入。", false); });
            Grid::SetColumn(browse, 2);
            pathGrid.Children().Append(browse);
            settings.Child(pathGrid);
            page.Children().Append(settings);

            m_info = InfoBar();
            m_info.IsOpen(true);
            m_info.IsClosable(false);
            m_info.Severity(InfoBarSeverity::Informational);
            m_info.Title(L"测试说明");
            m_info.Message(L"这是纯 UI 原型，不读取、不修改注册表，也不会启动原神。请选择不同账号观察选中效果。 ");
            page.Children().Append(m_info);

            auto footer = MakeText(L"Windows App SDK 1.8 · Framework-dependent · x64 · Unpackaged", 11);
            footer.Opacity(0.44);
            footer.HorizontalAlignment(HorizontalAlignment::Center);
            page.Children().Append(footer);

            scroll.Content(page);
            root.Children().Append(scroll);
            return root;
        }

        Button BuildAccountCard(int index)
        {
            auto const& account = m_accounts[index];
            Button card;
            card.Height(50);
            card.HorizontalAlignment(HorizontalAlignment::Stretch);
            card.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            card.Padding(T(0));
            card.CornerRadius(CornerRadius{ 8 });
            card.Tag(box_value(index));

            Grid grid;
            auto accent = ColumnDefinition();
            accent.Width(GridLength{ 4, GridUnitType::Pixel });
            auto nameCol = ColumnDefinition();
            nameCol.Width(GridLength{ 1, GridUnitType::Star });
            auto uidCol = ColumnDefinition();
            uidCol.Width(GridLength{ 130, GridUnitType::Pixel });
            auto tagCol = ColumnDefinition();
            tagCol.Width(GridLength{ 94, GridUnitType::Pixel });
            grid.ColumnDefinitions().Append(accent);
            grid.ColumnDefinitions().Append(nameCol);
            grid.ColumnDefinitions().Append(uidCol);
            grid.ColumnDefinitions().Append(tagCol);

            winrt::Microsoft::UI::Xaml::Shapes::Rectangle bar;
            bar.RadiusX(2);
            bar.RadiusY(2);
            bar.Margin(T(0, 8, 0, 8));
            bar.Fill(index == m_selected ? Brush(0xFF, 0x00, 0x78, 0xD4) : Brush(0x00, 0, 0, 0));
            grid.Children().Append(bar);

            auto name = MakeText(account.name, 14, index == m_selected ? FontWeights::SemiBold() : FontWeights::Normal());
            name.Margin(T(12, 0, 8, 0));
            Grid::SetColumn(name, 1);
            grid.Children().Append(name);

            auto uid = MakeText(account.uid, 12);
            uid.Opacity(0.60);
            Grid::SetColumn(uid, 2);
            grid.Children().Append(uid);

            Border state;
            state.CornerRadius(CornerRadius{ 8 });
            state.Padding(T(8, 3, 8, 3));
            state.HorizontalAlignment(HorizontalAlignment::Left);
            auto stateText = MakeText(index == m_selected ? L"已选中" : (account.current ? L"当前账号" : L""), 11, FontWeights::SemiBold());
            if (index == m_selected)
            {
                state.Background(Brush(0x20, 0x00, 0x78, 0xD4));
                stateText.Foreground(Brush(0xFF, 0x00, 0x63, 0xB1));
            }
            else if (account.current)
            {
                state.Background(Brush(0x18, 0x10, 0x8A, 0x43));
                stateText.Foreground(Brush(0xFF, 0x0A, 0x6A, 0x35));
            }
            state.Child(stateText);
            Grid::SetColumn(state, 3);
            state.VerticalAlignment(VerticalAlignment::Center);
            grid.Children().Append(state);

            card.Content(grid);
            ApplyCardVisual(card, index == m_selected);
            card.Click([this, index](auto&&, auto&&)
            {
                m_selected = index;
                RebuildAccounts();
                ShowInfo(L"已选择：" + m_accounts[index].name + L" · UID " + m_accounts[index].uid, true);
            });
            return card;
        }

        void ApplyCardVisual(Button const& card, bool selected)
        {
            if (selected)
            {
                card.Background(Brush(0x26, 0x00, 0x78, 0xD4));
                card.BorderBrush(Brush(0xC0, 0x00, 0x78, 0xD4));
                card.BorderThickness(T(1.5));
            }
            else
            {
                card.Background(Brush(0x35, 0xFF, 0xFF, 0xFF));
                card.BorderBrush(Brush(0x18, 0x00, 0x00, 0x00));
                card.BorderThickness(T(1));
            }
        }

        void RebuildAccounts()
        {
            m_accountStack.Children().Clear();
            m_accountButtons.clear();
            for (int i = 0; i < static_cast<int>(m_accounts.size()); ++i)
            {
                auto card = BuildAccountCard(i);
                m_accountButtons.push_back(card);
                m_accountStack.Children().Append(card);
            }
        }

        void ShowInfo(hstring const& message, bool success)
        {
            if (!m_info) return;
            m_info.Title(success ? L"选择已更新" : L"UI 原型");
            m_info.Message(message);
            m_info.Severity(success ? InfoBarSeverity::Success : InfoBarSeverity::Informational);
            m_info.IsOpen(true);
        }

        Window m_window{ nullptr };
        StackPanel m_accountStack{ nullptr };
        InfoBar m_info{ nullptr };
        std::vector<AccountRow> m_accounts;
        std::vector<Button> m_accountButtons;
        int m_selected{ 0 };
    };
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    init_apartment(apartment_type::single_threaded);
    Microsoft::UI::Xaml::Application::Start([](auto&&)
    {
        make<PrototypeApp>();
    });
    return 0;
}
