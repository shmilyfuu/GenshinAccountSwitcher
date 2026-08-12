#pragma once
#include "MainWindow.g.h"

namespace winrt::GenshinSwitcherXamlPrototype::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        void OnTestClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        void ResizeWindow();
    };
}

namespace winrt::GenshinSwitcherXamlPrototype::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
