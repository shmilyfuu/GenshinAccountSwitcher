#pragma once
#include "MainWindow.g.h"
#include "NativeCore.h"

namespace winrt::GenshinAccountSwitcher::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void OnWindowActivated(IInspectable const&, Microsoft::UI::Xaml::WindowActivatedEventArgs const&);
        void OnRefreshClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAccountSelectionChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnAddClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUpdateClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRenameClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnDeleteClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSwitchClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSwitchLaunchClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRestoreClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSelectGamePathClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        struct AccountVisual
        {
            Microsoft::UI::Xaml::Controls::ListViewItem item{ nullptr };
            Microsoft::UI::Xaml::Controls::Border card{ nullptr };
            Microsoft::UI::Xaml::Controls::Border accent{ nullptr };
            Microsoft::UI::Xaml::Controls::Border badge{ nullptr };
            Microsoft::UI::Xaml::Controls::TextBlock badgeText{ nullptr };
        };

        gas::AppCore m_core;
        gas::CurrentState m_currentState;
        std::vector<AccountVisual> m_visuals;
        int m_selectedIndex{ -1 };
        bool m_initialized{ false };
        bool m_firstActivation{ true };
        bool m_rebuildingAccounts{ false };

        void ResizeWindow();
        void RefreshUi(bool rebuildAccounts = true);
        void RebuildAccounts();
        AccountVisual BuildAccountVisual(int index);
        void UpdateAccountVisuals();
        void UpdateButtonStates();
        void SetStatus(std::wstring const& text);
        HWND WindowHandle() const;
        std::wstring ChooseGameExecutable();

        winrt::fire_and_forget AddCurrentAsync();
        winrt::fire_and_forget UpdateSelectedAsync();
        winrt::fire_and_forget RenameSelectedAsync();
        winrt::fire_and_forget DeleteSelectedAsync();
        winrt::fire_and_forget SwitchSelectedAsync(bool launchAfter);
        winrt::fire_and_forget RestoreAsync();
        void ShowMessage(std::wstring const& title, std::wstring const& message);

        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> PromptTextAsync(
            std::wstring const& title,
            std::wstring const& message,
            std::wstring const& initial = L"",
            std::wstring const& placeholder = L"");
        winrt::Windows::Foundation::IAsyncOperation<bool> ConfirmAsync(
            std::wstring const& title,
            std::wstring const& message,
            std::wstring const& primary = L"确定");
        winrt::Windows::Foundation::IAsyncAction ShowMessageAsync(
            std::wstring const& title,
            std::wstring const& message);
    };
}

namespace winrt::GenshinAccountSwitcher::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
