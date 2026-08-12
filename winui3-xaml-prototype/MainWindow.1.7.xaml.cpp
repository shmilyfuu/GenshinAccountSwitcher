#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <microsoft.ui.xaml.window.h>

namespace winrt::GenshinSwitcherXamlPrototype::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();
        Title(L"原神账号切换器 · WinUI 3 启动测试");
        ResizeWindow();
    }

    void MainWindow::ResizeWindow()
    {
        HWND hwnd{};
        Microsoft::UI::Xaml::Window window = *this;
        window.as<::IWindowNative>()->get_WindowHandle(&hwnd);
        if (!hwnd) return;
        const UINT dpi = GetDpiForWindow(hwnd);
        const double scale = static_cast<double>(dpi) / 96.0;
        SetWindowPos(hwnd, nullptr, 0, 0,
            static_cast<int>(680 * scale), static_cast<int>(460 * scale),
            SWP_NOMOVE | SWP_NOZORDER);
    }

    void MainWindow::OnTestClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        StatusText().Text(L"按钮点击正常，WinUI 3 XAML 已运行。");
        TestButton().Content(box_value(L"测试通过"));
    }
}
