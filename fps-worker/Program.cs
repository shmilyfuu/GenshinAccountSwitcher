using System.Diagnostics;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;

namespace GenshinFpsWorker;

internal static class Program
{
    private static async Task<int> Main(string[] args)
    {
        if (args.Length != 2 || args[0] != "--parent" ||
            !int.TryParse(args[1], out var parentId) || parentId <= 0)
            return 2;

        using var singleInstance = new Mutex(true,
            @"Local\GenshinAccountSwitcher.FpsWorker", out var firstInstance);
        if (!firstInstance) return 3;

        try
        {
            using var parent = Process.GetProcessById(parentId);
            var dataDirectory = Path.GetFullPath(
                Path.Combine(AppContext.BaseDirectory, "..", "data"));
            var configPath = Path.Combine(dataDirectory, "fps-settings.json");
            var statusPath = Path.Combine(dataDirectory, "fps-worker-status.json");

            var worker = new FpsWorker(configPath, statusPath);
            await worker.RunAsync(parent);
            return 0;
        }
        catch
        {
            return 1;
        }
    }
}

internal sealed class FpsWorker(string configPath, string statusPath)
{
    private readonly string _configPath = configPath;
    private readonly string _statusPath = statusPath;
    private string _lastState = "";

    private (bool Enabled, int Fps) ReadSettings()
    {
        try
        {
            using var json = JsonDocument.Parse(File.ReadAllText(_configPath));
            var root = json.RootElement;
            var enabled = root.TryGetProperty("enabled", out var value) &&
                value.ValueKind == JsonValueKind.True;
            var fps = root.TryGetProperty("fps", out value) &&
                value.TryGetInt32(out var result) ? result : 120;
            return (enabled, Math.Clamp(fps, 1, 1000));
        }
        catch
        {
            return (false, 120);
        }
    }

    private void PublishStatus(string state)
    {
        if (_lastState == state) return;
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_statusPath)!);
            var temporary = _statusPath + ".tmp";
            File.WriteAllText(temporary, JsonSerializer.Serialize(new { state }));
            File.Move(temporary, _statusPath, overwrite: true);
            _lastState = state;
        }
        catch
        {
            // The parent also checks whether this worker process is alive.
        }
    }

    private static bool OriginalUnlockerRunning()
    {
        foreach (var name in new[] { "unlockfps_nc", "unlockfps_nc_signed" })
        {
            foreach (var process in Process.GetProcessesByName(name))
            {
                using (process)
                {
                    try
                    {
                        if (!process.HasExited) return true;
                    }
                    catch { }
                }
            }
        }
        return false;
    }

    private static Process? FindGameProcess()
    {
        foreach (var process in Process.GetProcessesByName("YuanShen"))
        {
            try
            {
                if (!process.HasExited) return process;
            }
            catch { }
            process.Dispose();
        }
        return null;
    }

    public async Task RunAsync(Process parent)
    {
        Process? game = null;
        FpsSession? session = null;
        int suppressedGameId = 0;

        try
        {
            while (!parent.HasExited)
            {
                var (enabled, fps) = ReadSettings();

                if (game != null)
                {
                    bool exited;
                    try { exited = game.HasExited; }
                    catch { exited = true; }

                    if (exited)
                    {
                        session?.Dispose();
                        session = null;
                        game.Dispose();
                        game = null;
                        suppressedGameId = 0;
                    }
                    else if (session != null)
                    {
                        // Disabling the toggle during play takes effect next launch.
                        if (enabled) session.UpdateFps(fps);
                        PublishStatus("active");
                    }
                }

                if (game == null)
                {
                    if (!enabled)
                        PublishStatus("disabled");
                    else if (OriginalUnlockerRunning())
                        PublishStatus("conflict");
                    else
                    {
                        using var candidate = FindGameProcess();
                        if (candidate == null)
                            PublishStatus("waiting");
                        else if (candidate.Id == suppressedGameId)
                            PublishStatus("error");
                        else
                        {
                            var window = Native.FindGameWindow(candidate.Id);
                            if (window == IntPtr.Zero)
                                PublishStatus("connecting");
                            else
                            {
                                PublishStatus("connecting");
                                var next = new FpsSession();
                                if (next.Connect(window, fps, () => parent.HasExited))
                                {
                                    game = Process.GetProcessById(candidate.Id);
                                    session = next;
                                    PublishStatus("active");
                                }
                                else
                                {
                                    next.Dispose();
                                    suppressedGameId = candidate.Id;
                                    PublishStatus("error");
                                }
                            }
                        }
                    }
                }

                await Task.Delay(300);
            }
        }
        finally
        {
            session?.Dispose();
            game?.Dispose();
        }
    }
}

internal sealed class FpsSession : IDisposable
{
    private const string SharedMemoryName =
        @"Global\2DE95FDC-6AB7-4593-BFE6-760DD4AB422B";

    private MemoryMappedFile? _memory;
    private MemoryMappedViewAccessor? _view;
    private nint _module;
    private nint _hook;

    public bool Connect(nint window, int fps, Func<bool> parentExited)
    {
        try
        {
            var dll = Path.Combine(AppContext.BaseDirectory, "UnlockerStub.dll");
            if (!File.Exists(dll)) return false;

            _memory = MemoryMappedFile.CreateOrOpen(
                SharedMemoryName, 4096, MemoryMappedFileAccess.ReadWrite);
            _view = _memory.CreateViewAccessor(0, 4096, MemoryMappedFileAccess.ReadWrite);
            _view.Write(0, (byte)0);
            UpdateFps(fps);

            _module = Native.LoadLibraryW(dll);
            if (_module == IntPtr.Zero) return false;

            var hookProc = Native.GetProcAddress(_module, "WndProc");
            var thread = Native.GetWindowThreadProcessId(window, out _);
            if (hookProc == IntPtr.Zero || thread == 0) return false;

            _hook = Native.SetWindowsHookExW(3, hookProc, _module, thread);
            if (_hook == IntPtr.Zero || !Native.PostThreadMessageW(
                thread, 0, IntPtr.Zero, IntPtr.Zero)) return false;

            for (var count = 0; count < 100 && !parentExited(); count++)
            {
                var state = _view.ReadByte(0);
                if (state == 2) return true;
                if (state == 1) return false;
                Thread.Sleep(200);
            }
            return false;
        }
        catch
        {
            return false;
        }
    }

    public void UpdateFps(int fps)
    {
        if (_view == null) return;
        // Shared layout matches UnlockerStub: status (0), fps (4),
        // power-saving (8), mobile UI (9).
        _view.Write(4, Math.Clamp(fps, 1, 1000));
        _view.Write(8, (byte)0);
        _view.Write(9, (byte)0);
    }

    public void Dispose()
    {
        if (_hook != IntPtr.Zero)
        {
            Native.UnhookWindowsHookEx(_hook);
            _hook = IntPtr.Zero;
        }
        if (_module != IntPtr.Zero)
        {
            Native.FreeLibrary(_module);
            _module = IntPtr.Zero;
        }
        _view?.Dispose();
        _view = null;
        _memory?.Dispose();
        _memory = null;
    }
}

internal static class Native
{
    private delegate bool EnumWindowsProc(nint hwnd, nint lparam);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool EnumWindows(EnumWindowsProc callback, nint lparam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassNameW(nint hwnd, StringBuilder className, int maxCount);

    [DllImport("user32.dll")]
    internal static extern uint GetWindowThreadProcessId(nint hwnd, out uint processId);

    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(nint hwnd);

    [DllImport("user32.dll", SetLastError = true)]
    internal static extern nint SetWindowsHookExW(int hookType, nint procedure, nint module, uint threadId);

    [DllImport("user32.dll", SetLastError = true)]
    internal static extern bool UnhookWindowsHookEx(nint hook);

    [DllImport("user32.dll", SetLastError = true)]
    internal static extern bool PostThreadMessageW(uint threadId, uint message, nint wparam, nint lparam);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    internal static extern nint LoadLibraryW(string path);

    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
    internal static extern nint GetProcAddress(nint module, string name);

    [DllImport("kernel32.dll", SetLastError = true)]
    internal static extern bool FreeLibrary(nint module);

    internal static nint FindGameWindow(int processId)
    {
        nint result = IntPtr.Zero;
        EnumWindows((hwnd, _) =>
        {
            GetWindowThreadProcessId(hwnd, out var pid);
            if (pid != processId || !IsWindowVisible(hwnd)) return true;
            var className = new StringBuilder(128);
            GetClassNameW(hwnd, className, className.Capacity);
            if (className.ToString() != "UnityWndClass") return true;
            result = hwnd;
            return false;
        }, IntPtr.Zero);
        return result;
    }
}
