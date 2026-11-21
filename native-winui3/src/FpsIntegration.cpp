#include "pch.h"
#include "MainWindow.xaml.h"
#include "StartupDiagnostics.h"

#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

#pragma comment(lib, "Shell32.lib")

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Controls::Primitives;

namespace winrt::GenshinAccountSwitcher::implementation
{
    void MainWindow::InitializeFpsControls()
    {
        gas::diagnostics::TraceStartup("Fps.initialize.begin");
        m_fpsConfigPath = m_core.DataDirectory() / L"fps-settings.json";
        m_fpsStatusPath = m_core.DataDirectory() / L"fps-worker-status.json";

        gas::diagnostics::TraceStartup("Fps.config-read.begin");
        try
        {
            std::ifstream stream(m_fpsConfigPath, std::ios::binary);
            if (stream)
            {
                std::string bytes(std::istreambuf_iterator<char>{stream}, {});
                auto config = Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(bytes));
                m_fpsEnabled = config.GetNamedBoolean(L"enabled", false);
                m_fpsTarget = std::clamp(
                    static_cast<int>(std::lround(config.GetNamedNumber(L"fps", 120.0))),
                    1, 1000);
            }
        }
        catch (...)
        {
            gas::diagnostics::TraceCurrentException("Fps.config-read.exception");
            m_fpsEnabled = false;
            m_fpsTarget = 120;
        }
        gas::diagnostics::TraceStartup("Fps.config-read.complete");

        gas::diagnostics::TraceStartup("Fps.inputs-set.begin");
        UpdateFpsInputs();
        gas::diagnostics::TraceStartup("Fps.inputs-set.complete");

        // Attach handlers only after applying the saved values to the live
        // controls. Their first change notifications are initialization work.
        gas::diagnostics::TraceStartup("Fps.handlers-register.begin");
        FpsToggle().Toggled({ this, &MainWindow::OnFpsToggleChanged });
        FpsNumber().ValueChanged({ this, &MainWindow::OnFpsNumberChanged });
        FpsSlider().ValueChanged({ this, &MainWindow::OnFpsSliderChanged });
        gas::diagnostics::TraceStartup("Fps.handlers-register.complete");

        m_fpsControlsReady = true;
        if (m_fpsEnabled)
        {
            gas::diagnostics::TraceStartup("Fps.worker-launch.begin");
            EnsureFpsWorkerRunning();
            gas::diagnostics::TraceStartup("Fps.worker-launch.complete");
        }
        gas::diagnostics::TraceStartup("Fps.status-refresh.begin");
        RefreshFpsStatus();
        gas::diagnostics::TraceStartup("Fps.status-refresh.complete");
        gas::diagnostics::TraceStartup("Fps.initialize.complete");
    }

    void MainWindow::SaveFpsSettings()
    {
        m_fpsConfigWriteFailed = false;
        try
        {
            using namespace Windows::Data::Json;
            JsonObject object;
            object.Insert(L"enabled", JsonValue::CreateBooleanValue(m_fpsEnabled));
            object.Insert(L"fps", JsonValue::CreateNumberValue(static_cast<double>(m_fpsTarget)));
            auto bytes = winrt::to_string(object.Stringify());
            auto temporary = m_fpsConfigPath;
            temporary += L".tmp";

            {
                std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
                stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                stream.flush();
                if (!stream) throw std::runtime_error("fps-config-write");
            }

            if (!MoveFileExW(temporary.c_str(), m_fpsConfigPath.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("fps-config-replace");
        }
        catch (...)
        {
            m_fpsConfigWriteFailed = true;
        }
    }

    void MainWindow::UpdateFpsInputs()
    {
        m_syncingFpsControls = true;
        FpsToggle().IsOn(m_fpsEnabled);
        FpsToggleStateText().Text(m_fpsEnabled ? L"已启用" : L"已关闭");
        FpsNumber().Value(static_cast<double>(m_fpsTarget));
        FpsSlider().Value(static_cast<double>(m_fpsTarget));
        FpsNumber().IsEnabled(m_fpsEnabled);
        FpsSlider().IsEnabled(m_fpsEnabled);
        m_syncingFpsControls = false;
    }

    void MainWindow::OnFpsToggleChanged(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_fpsControlsReady || m_syncingFpsControls) return;
        m_fpsEnabled = FpsToggle().IsOn();
        UpdateFpsInputs();
        SaveFpsSettings();
        if (m_fpsEnabled) EnsureFpsWorkerRunning();
        RefreshFpsStatus();
    }

    void MainWindow::OnFpsNumberChanged(IInspectable const&, NumberBoxValueChangedEventArgs const& args)
    {
        if (!m_fpsControlsReady || m_syncingFpsControls || !std::isfinite(args.NewValue()))
            return;
        const auto value = std::clamp(static_cast<int>(std::lround(args.NewValue())), 1, 1000);
        if (value == m_fpsTarget) return;
        m_fpsTarget = value;
        UpdateFpsInputs();
        SaveFpsSettings();
        RefreshFpsStatus();
    }

    void MainWindow::OnFpsSliderChanged(IInspectable const&, RangeBaseValueChangedEventArgs const& args)
    {
        if (!m_fpsControlsReady || m_syncingFpsControls || !std::isfinite(args.NewValue()))
            return;
        const auto value = std::clamp(static_cast<int>(std::lround(args.NewValue())), 1, 1000);
        if (value == m_fpsTarget) return;
        m_fpsTarget = value;
        UpdateFpsInputs();
        SaveFpsSettings();
        RefreshFpsStatus();
    }

    void MainWindow::CloseFpsWorkerHandle()
    {
        if (!m_fpsWorkerProcess) return;
        CloseHandle(m_fpsWorkerProcess);
        m_fpsWorkerProcess = nullptr;
    }

    void MainWindow::EnsureFpsWorkerRunning()
    {
        if (!m_fpsEnabled || m_appExiting) return;
        if (m_fpsWorkerProcess)
        {
            if (WaitForSingleObject(m_fpsWorkerProcess, 0) == WAIT_TIMEOUT)
                return;
            CloseFpsWorkerHandle();
        }

        auto executable = m_core.BaseDirectory() / L"fps-worker" / L"GenshinFpsWorker.exe";
        if (!std::filesystem::exists(executable))
        {
            m_fpsWorkerLaunchFailed = true;
            return;
        }

        std::error_code ec;
        std::filesystem::remove(m_fpsStatusPath, ec);

        const auto arguments = L"--parent " + std::to_wstring(GetCurrentProcessId());
        const auto workingDirectory = executable.parent_path().wstring();

        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
        info.lpVerb = L"runas";
        info.lpFile = executable.c_str();
        info.lpParameters = arguments.c_str();
        info.lpDirectory = workingDirectory.c_str();
        info.nShow = SW_HIDE;
        info.hwnd = WindowHandle();

        m_fpsWorkerLaunchFailed = !ShellExecuteExW(&info) || !info.hProcess;
        if (!m_fpsWorkerLaunchFailed)
            m_fpsWorkerProcess = info.hProcess;
        else if (info.hProcess)
            CloseHandle(info.hProcess);
    }

    void MainWindow::RefreshFpsStatus()
    {
        if (!m_fpsControlsReady) return;

        if (m_fpsConfigWriteFailed)
        {
            FpsStatusText().Text(L"帧率设置保存失败，请检查数据目录权限");
            return;
        }

        if (!m_fpsEnabled)
        {
            FpsStatusText().Text(m_fpsWorkerProcess && m_core.IsGameRunning()
                ? L"已关闭，当前游戏退出后生效"
                : L"帧率解锁已关闭");
            return;
        }

        if (m_fpsWorkerLaunchFailed)
        {
            FpsStatusText().Text(L"帧率后台启动失败，可关闭开关后重试");
            return;
        }

        if (!m_fpsWorkerProcess)
        {
            FpsStatusText().Text(L"帧率后台尚未启动");
            return;
        }

        if (WaitForSingleObject(m_fpsWorkerProcess, 0) != WAIT_TIMEOUT)
        {
            FpsStatusText().Text(L"帧率后台已退出，可关闭开关后重试");
            return;
        }

        std::wstring status = L"帧率后台启动中…";
        try
        {
            std::ifstream stream(m_fpsStatusPath, std::ios::binary);
            if (stream)
            {
                std::string bytes(std::istreambuf_iterator<char>{stream}, {});
                auto result = Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(bytes));
                auto state = result.GetNamedString(L"state", L"");
                if (state == L"waiting") status = L"帧率后台已就绪，等待原神启动";
                else if (state == L"connecting") status = L"正在连接原神帧率模块…";
                else if (state == L"active")
                    status = L"帧率解锁已连接 · " + std::to_wstring(m_fpsTarget) + L" FPS";
                else if (state == L"conflict") status = L"检测到其他帧率解锁器，请先退出";
                else if (state == L"error") status = L"帧率模块连接失败，请检查游戏版本";
            }
        }
        catch (...) {}
        FpsStatusText().Text(status);
    }
}
