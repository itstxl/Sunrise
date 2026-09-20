#pragma once

#include <cstddef>
#include <span>
#include <sunrise/realm/control_protocol.h>
#include <vector>

namespace sunrise::realm::windows {

/** Loads a host-control secret from SUNRISE_REALM_HOST_TOKEN. */
[[nodiscard]] bool load_host_token(std::vector<std::byte>& output) noexcept;

/** Fills a frame authentication tag with HMAC-SHA256 over its canonical wire form. */
[[nodiscard]] bool sign_frame(control::Frame& frame, std::span<const std::byte> key) noexcept;

/** Verifies a frame tag in constant time. */
[[nodiscard]] bool verify_frame(const control::Frame& frame,
                                std::span<const std::byte> key) noexcept;

/** Fills an opaque identifier using the operating system CSPRNG. */
[[nodiscard]] bool random_identifier(Identifier& output) noexcept;

/** Hashes one generated host artifact without retaining its content in memory. */
[[nodiscard]] bool sha256_file(const wchar_t* path, BuildFingerprint& output) noexcept;

} // namespace sunrise::realm::windows
