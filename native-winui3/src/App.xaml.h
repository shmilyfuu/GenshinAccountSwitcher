#pragma once
#include "App.xaml.g.h"

namespace winrt::GenshinAccountSwitcher::implementation
{
    struct App : AppT<App>
    {
        App();
        winrt::fire_and_forget OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);
        void OnRedirectedActivation(IInspectable const&, Microsoft::Windows::AppLifecycle::AppActivationArguments const&);

    private:
        Microsoft::UI::Xaml::Window m_window{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        winrt::event_token m_redirectedToken{};
    };
}
