#include "pch.h"
#include "StartupDiagnostics.h"

#include <exception>
#include <cstdio>
#include <cstring>

namespace
{
    // This logger intentionally has no XAML or application-core dependencies.
    // It must remain usable before MainWindow construction and during failure.
    void AppendLine(char const* stage, char const* detail = nullptr) noexcept
    {
        wchar_t executable[4096]{};
        DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        if (!length || length >= std::size(executable)) return;

        auto lastSeparator = wcsrchr(executable, L'\\');
        if (!lastSeparator) return;
        *lastSeparator = L'\0';

        wchar_t dataDirectory[4096]{};
        wchar_t logDirectory[4096]{};
        wchar_t logPath[4096]{};
        if (swprintf_s(dataDirectory, L"%s\\data", executable) < 0 ||
            swprintf_s(logDirectory, L"%s\\logs", dataDirectory) < 0 ||
            swprintf_s(logPath, L"%s\\startup.log", logDirectory) < 0)
            return;

        CreateDirectoryW(dataDirectory, nullptr);
        CreateDirectoryW(logDirectory, nullptr);

        HANDLE handle = CreateFileW(logPath, FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return;

        SYSTEMTIME now{};
        GetSystemTime(&now);

        char line[2048]{};
        const int written = snprintf(line, sizeof(line),
            "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ | pid=%lu tid=%lu tick=%llu | %s%s%s\r\n",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
            static_cast<unsigned long>(GetCurrentProcessId()),
            static_cast<unsigned long>(GetCurrentThreadId()),
            static_cast<unsigned long long>(GetTickCount64()),
            stage ? stage : "unknown", detail ? " | " : "", detail ? detail : "");

        if (written > 0)
        {
            DWORD bytes{};
            const auto count = static_cast<DWORD>((std::min)(static_cast<size_t>(written), sizeof(line) - 1));
            WriteFile(handle, line, count, &bytes, nullptr);
            FlushFileBuffers(handle);
        }
        CloseHandle(handle);
    }

    void TraceWideMessage(char const* stage, HRESULT code, wchar_t const* message) noexcept
    {
        char utf8[1200]{};
        if (message && *message)
        {
            const int converted = WideCharToMultiByte(CP_UTF8, 0, message, -1,
                utf8, static_cast<int>(sizeof(utf8)), nullptr, nullptr);
            if (converted <= 0) utf8[0] = '\0';
        }
        for (char* p = utf8; *p; ++p)
        {
            if (*p == '\n' || *p == '\r' || *p == '\t') *p = ' ';
        }

        char detail[1300]{};
        snprintf(detail, sizeof(detail), "HRESULT=0x%08lX message=%s",
            static_cast<unsigned long>(static_cast<unsigned int>(code)), utf8);
        AppendLine(stage, detail);
    }
}

namespace gas::diagnostics
{
    void TraceStartup(char const* stage) noexcept
    {
        AppendLine(stage);
    }

    void TraceCurrentException(char const* stage) noexcept
    {
        try
        {
            auto exception = std::current_exception();
            if (!exception)
            {
                AppendLine(stage, "no active C++ exception");
                return;
            }
            std::rethrow_exception(exception);
        }
        catch (winrt::hresult_error const& error)
        {
            TraceWideMessage(stage, static_cast<HRESULT>(error.code()),
                error.message().c_str());
        }
        catch (std::exception const& error)
        {
            AppendLine(stage, error.what());
        }
        catch (...)
        {
            AppendLine(stage, "unknown exception type");
        }
    }
}
