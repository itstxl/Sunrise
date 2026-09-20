#include "console_runtime.h"

#include <Windows.h>

#include <atomic>
#include <cstdio>

namespace sunrise::realm::windows {
namespace {

std::atomic_bool g_shutdownRequested{false};

BOOL WINAPI handle_console_signal(DWORD signal) {
    switch (signal) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_shutdownRequested.store(true, std::memory_order_release);
        return TRUE;
    default:
        return FALSE;
    }
}

} // namespace

bool initialize_console() noexcept {
    g_shutdownRequested.store(false, std::memory_order_release);
    return SetConsoleCtrlHandler(handle_console_signal, TRUE) != FALSE;
}

bool shutdown_requested() noexcept {
    return g_shutdownRequested.load(std::memory_order_acquire);
}

void write_line(std::string_view line) noexcept {
    (void)std::fwrite(line.data(), sizeof(char), line.size(), stdout);
    (void)std::fputc('\n', stdout);
    (void)std::fflush(stdout);
}

} // namespace sunrise::realm::windows
