#pragma once

#include <windows.h>

namespace gas::diagnostics
{
    void TraceStartup(char const* stage) noexcept;
    void TraceHresult(char const* stage, HRESULT code, wchar_t const* message) noexcept;
    // Call inside a catch block to record the active exception without
    // changing the caller's existing error handling.
    void TraceCurrentException(char const* stage) noexcept;
}
