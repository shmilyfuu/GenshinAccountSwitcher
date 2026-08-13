#pragma once
#include "MainWindow.g.h"
#include "NativeCore.h"

namespace winrt::GenshinAccountSwitcher::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void OnWindowActivated(IInspectable const&, Microsoft::UI::Xaml::WindowActivatedEventArgs const&);
        void OnRootLoaded(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
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
        void OnOpenDataDirectoryClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnOpenLogClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        struct AccountVisual
        {
            Microsoft::UI::Xaml::Controls::ListViewItem item{ nullptr };
            Microsoft::UI::Xaml::Controls::Border card{ nullptr };
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
        bool m_gameWasRunning{ false };
        bool m_settlementRunning{ false };
        int m_runtimePollTicks{ 0 };
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_monitorTimer{ nullptr };

        bool m_enhancementsInitialized{ false };
        bool m_settingUnifiedStatus{ false };
        std::int64_t m_statusTextCallbackToken{ 0 };
        ULONGLONG m_statusOverrideUntil{ 0 };
        HANDLE m_trackedGameProcess{ nullptr };
        HANDLE m_gameExitWait{ nullptr };
        DWORD m_trackedGamePid{ 0 };
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_processDiscoveryTimer{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_statusTimer{ nullptr };

        void ResizeWindow();
        void RefreshUi(bool rebuildAccounts = true, bool showRefreshStatus = false);
        gas::CurrentState ClassifyProbe(gas::CurrentProbe const& probe, bool gameRunning);
        void RebuildAccounts();
        AccountVisual BuildAccountVisual(int index);
        void UpdateAccountVisuals();
        void UpdateButtonStates();
        void SetStatus(std::wstring const& text);
        void StartMonitoring();
        void OnMonitorTick(Microsoft::UI::Dispatching::DispatcherQueueTimer const&, IInspectable const&);
        HWND WindowHandle() const;
        std::wstring ChooseGameExecutable();
        void OpenPath(std::filesystem::path const& path);
        bool BackupCredential(gas::AccountProfile const& account);
        void RemoveCredentialBackup(gas::AccountProfile const& account) noexcept;
        static bool IdentitySnapshotEquals(gas::RegistrySnapshot const& a, gas::RegistrySnapshot const& b) noexcept;

        void ConfigureFixedWindow();
        void ApplyWindowIcon();
        void NormalizeAccountRows();
        void StartEnhancedMonitoring();
        void DiscoverCurrentSessionGame();
        void TrackGameProcess(DWORD pid);
        void ReleaseTrackedGameProcess();
        void OnTrackedGameExited();
        void OnProcessDiscoveryTick(Microsoft::UI::Dispatching::DispatcherQueueTimer const&, IInspectable const&);
        void OnStatusTimerTick(Microsoft::UI::Dispatching::DispatcherQueueTimer const&, IInspectable const&);
        void OnAccountsLayoutUpdated(IInspectable const&, IInspectable const&);
        void OnStatusTextPropertyChanged(Microsoft::UI::Xaml::DependencyObject const&, Microsoft::UI::Xaml::DependencyProperty const&);
        void RefreshUnifiedStatus(bool force = false);
        std::wstring BuildBaseStatusText() const;
        void SetUnifiedStatusText(std::wstring const& text);
        static DWORD FindCurrentSessionGameProcessId();
        static VOID CALLBACK GameExitWaitCallback(PVOID context, BOOLEAN timedOut);
        static LRESULT CALLBACK WindowSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR referenceData);

        winrt::fire_and_forget SettleAndReconcileAsync(bool initialPass);
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
