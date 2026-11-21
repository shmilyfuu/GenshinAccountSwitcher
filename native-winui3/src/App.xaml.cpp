#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "StartupDiagnostics.h"
#include <microsoft.ui.xaml.window.h>
#include <cmath>

namespace
{
    void PrepareInitialWindow(winrt::Microsoft::UI::Xaml::Window const& window)
    {
        gas::diagnostics::TraceStartup("PrepareWindow.handle.begin");
        HWND hwnd{};
        window.as<::IWindowNative>()->get_WindowHandle(&hwnd);
        if (!hwnd)
        {
            gas::diagnostics::TraceStartup("PrepareWindow.handle.missing");
            return;
        }
        gas::diagnostics::TraceStartup("PrepareWindow.handle.complete");

        auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        style &= ~(static_cast<LONG_PTR>(WS_THICKFRAME) | static_cast<LONG_PTR>(WS_MAXIMIZEBOX));
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);

        UINT dpi = GetDpiForWindow(hwnd);
        double scale = static_cast<double>(dpi) / 96.0;
        RECT rect{ 0, 0,
            static_cast<LONG>(std::lround(320.0 * scale)),
            static_cast<LONG>(std::lround(640.0 * scale)) };
        auto exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

        gas::diagnostics::TraceStartup("PrepareWindow.adjust.begin");
        if (AdjustWindowRectExForDpi(&rect, static_cast<DWORD>(style), FALSE,
            static_cast<DWORD>(exStyle), dpi))
        {
            SetWindowPos(hwnd, nullptr, 0, 0,
                rect.right - rect.left,
                rect.bottom - rect.top,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        gas::diagnostics::TraceStartup("PrepareWindow.complete");
    }

    winrt::GenshinAccountSwitcher::implementation::MainWindow* GetMainWindowImpl(
        winrt::Microsoft::UI::Xaml::Window const& window)
    {
        auto projected = window.as<winrt::GenshinAccountSwitcher::MainWindow>();
        return winrt::get_self<winrt::GenshinAccountSwitcher::implementation::MainWindow>(projected);
    }

    void PrewarmInitialXaml(winrt::Microsoft::UI::Xaml::Window const& window)
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using winrt::Windows::Foundation::Rect;
        using winrt::Windows::Foundation::Size;

        gas::diagnostics::TraceStartup("Prewarm.root-loaded.begin");
        auto self = GetMainWindowImpl(window);
        self->OnRootLoaded(nullptr, RoutedEventArgs{});
        gas::diagnostics::TraceStartup("Prewarm.root-loaded.complete");

        if (auto root = window.Content().try_as<FrameworkElement>())
        {
            gas::diagnostics::TraceStartup("Prewarm.measure.begin");
            root.Measure(Size{ 320.0f, 640.0f });
            gas::diagnostics::TraceStartup("Prewarm.measure.complete");
            gas::diagnostics::TraceStartup("Prewarm.arrange.begin");
            root.Arrange(Rect{ 0.0f, 0.0f, 320.0f, 640.0f });
            gas::diagnostics::TraceStartup("Prewarm.arrange.complete");
            gas::diagnostics::TraceStartup("Prewarm.update-layout.begin");
            root.UpdateLayout();
            gas::diagnostics::TraceStartup("Prewarm.update-layout.complete");
        }
    }
}

namespace winrt::GenshinAccountSwitcher::implementation
{
    App::App()
    {
        gas::diagnostics::TraceStartup("Process.start");
        gas::diagnostics::TraceStartup("App.xaml-initialize.begin");
        try
        {
            InitializeComponent();
            gas::diagnostics::TraceStartup("App.xaml-initialize.complete");
            UnhandledException([](winrt::Windows::Foundation::IInspectable const&,
                Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& args)
            {
                gas::diagnostics::TraceHresult("Xaml.unhandled-exception",
                    static_cast<HRESULT>(args.Exception()), args.Message().c_str());
                // Handled remains false. The normal WinUI error behavior is retained.
            });
            gas::diagnostics::TraceStartup("App.xaml-unhandled-handler.registered");
        }
        catch (...)
        {
            gas::diagnostics::TraceCurrentException("App.xaml-initialize.exception");
            throw;
        }
    }

    winrt::fire_and_forget App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
    {
        gas::diagnostics::TraceStartup("OnLaunched.begin");
        try
        {
            using Microsoft::Windows::AppLifecycle::AppInstance;
            gas::diagnostics::TraceStartup("OnLaunched.single-instance.begin");
            auto mainInstance = AppInstance::FindOrRegisterForKey(L"shmilyfuu.GenshinAccountSwitcher");
            gas::diagnostics::TraceStartup(mainInstance.IsCurrent()
                ? "OnLaunched.primary-instance" : "OnLaunched.redirected-instance");
            if (!mainInstance.IsCurrent())
            {
                auto activation = AppInstance::GetCurrent().GetActivatedEventArgs();
                gas::diagnostics::TraceStartup("OnLaunched.redirect.begin");
                co_await mainInstance.RedirectActivationToAsync(activation);
                gas::diagnostics::TraceStartup("OnLaunched.redirect.complete");
                ::ExitProcess(0);
                co_return;
            }

            gas::diagnostics::TraceStartup("OnLaunched.dispatcher.begin");
            m_dispatcher = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
            m_redirectedToken = mainInstance.Activated({ this, &App::OnRedirectedActivation });
            gas::diagnostics::TraceStartup("OnLaunched.dispatcher.complete");

            gas::diagnostics::TraceStartup("OnLaunched.main-window.begin");
            m_window = winrt::make<MainWindow>();
            gas::diagnostics::TraceStartup("OnLaunched.main-window.complete");
            m_window.Title(L"原神账号管理");

            gas::diagnostics::TraceStartup("OnLaunched.prepare-window.begin");
            PrepareInitialWindow(m_window);
            gas::diagnostics::TraceStartup("OnLaunched.prepare-window.complete");

            gas::diagnostics::TraceStartup("OnLaunched.prewarm.begin");
            PrewarmInitialXaml(m_window);
            gas::diagnostics::TraceStartup("OnLaunched.prewarm.complete");

            gas::diagnostics::TraceStartup("OnLaunched.activate.begin");
            m_window.Activate();
            gas::diagnostics::TraceStartup("OnLaunched.activate.complete");
        }
        catch (...)
        {
            gas::diagnostics::TraceCurrentException("OnLaunched.exception");
            throw;
        }
    }

    void App::OnRedirectedActivation(
        IInspectable const&, Microsoft::Windows::AppLifecycle::AppActivationArguments const&)
    {
        gas::diagnostics::TraceStartup("OnRedirectedActivation.begin");
        auto queue = m_dispatcher;
        if (!queue) return;
        queue.TryEnqueue([this]()
        {
            if (m_window)
                GetMainWindowImpl(m_window)->RestoreFromTray();
        });
    }
}
