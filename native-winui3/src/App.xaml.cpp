#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include <microsoft.ui.xaml.window.h>
#include <cmath>

namespace
{
    void PrepareInitialWindow(Microsoft::UI::Xaml::Window const& window)
    {
        HWND hwnd{};
        window.as<::IWindowNative>()->get_WindowHandle(&hwnd);
        if (!hwnd) return;

        auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        style &= ~(static_cast<LONG_PTR>(WS_THICKFRAME) | static_cast<LONG_PTR>(WS_MAXIMIZEBOX));
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);

        UINT dpi = GetDpiForWindow(hwnd);
        double scale = static_cast<double>(dpi) / 96.0;
        RECT rect{ 0, 0,
            static_cast<LONG>(std::lround(320.0 * scale)),
            static_cast<LONG>(std::lround(480.0 * scale)) };
        auto exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

        if (AdjustWindowRectExForDpi(&rect, static_cast<DWORD>(style), FALSE,
            static_cast<DWORD>(exStyle), dpi))
        {
            SetWindowPos(hwnd, nullptr, 0, 0,
                rect.right - rect.left,
                rect.bottom - rect.top,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
    }
}

namespace winrt::GenshinAccountSwitcher::implementation
{
    App::App()
    {
        InitializeComponent();
    }

    void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
    {
        m_window = winrt::make<MainWindow>();
        m_window.Title(L"原神账号管理");
        PrepareInitialWindow(m_window);
        m_window.Activate();
    }
}
