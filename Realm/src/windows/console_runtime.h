#pragma once

#include <string_view>

namespace sunrise::realm::windows {

/** Installs bounded console shutdown handling for a headless service. */
[[nodiscard]] bool initialize_console() noexcept;

/** @return True after Ctrl+C, console close, logoff, or shutdown. */
[[nodiscard]] bool shutdown_requested() noexcept;

/** Writes one UTF-8 line to the process console. */
void write_line(std::string_view line) noexcept;

} // namespace sunrise::realm::windows
