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

    void ReserveSavedAccountSlots(Microsoft::UI::Xaml::Window const& window)
    {
        using namespace Microsoft::UI::Xaml;
        using namespace Microsoft::UI::Xaml::Controls;

        auto projected = window.as<winrt::GenshinAccountSwitcher::MainWindow>();
        auto self = winrt::get_self<winrt::GenshinAccountSwitcher::implementation::MainWindow>(projected);
        auto list = self->AccountsList();

        for (auto const& value : list.Items())
        {
            auto item = value.try_as<ListViewItem>();
            if (!item) continue;
            auto card = item.Content().try_as<Border>();
            if (!card) continue;
            auto row = card.Child().try_as<Grid>();
            if (!row) continue;

            auto children = row.Children();
            if (children.Size() >= 2)
            {
                if (auto uid = children.GetAt(1).try_as<TextBlock>())
                {
                    uid.Width(92);
                    uid.HorizontalAlignment(HorizontalAlignment::Right);
                    uid.TextAlignment(TextAlignment::Right);
                    uid.TextTrimming(TextTrimming::None);
                }
            }

            if (children.Size() == 3)
            {
                Border placeholder;
                placeholder.Width(64);
                placeholder.Height(1);
                placeholder.Opacity(0.0);
                placeholder.IsHitTestVisible(false);
                Grid::SetColumn(placeholder, 2);
                row.Children().Append(placeholder);
            }
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
        ReserveSavedAccountSlots(m_window);
        m_window.Activate();
    }
}
