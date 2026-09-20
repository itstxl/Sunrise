#include "frame_authentication.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <limits>
#include <string>

namespace sunrise::realm::windows {
namespace {

constexpr wchar_t kHostTokenVariable[] = L"SUNRISE_REALM_HOST_TOKEN";

[[nodiscard]] bool
hmac_sha256(std::span<const std::byte> key,
            std::span<const std::byte> message,
            std::array<std::byte, control::kAuthenticationTagSize>& output) noexcept {
    if (key.empty() || key.size() > (std::numeric_limits<ULONG>::max)()
        || message.size() > (std::numeric_limits<ULONG>::max)()) {
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    std::vector<std::byte> object;
    bool success = false;
    if (BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG)
        < 0) {
        return false;
    }
    ULONG objectSize{};
    ULONG returned{};
    if (BCryptGetProperty(algorithm,
                          BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&objectSize),
                          sizeof(objectSize),
                          &returned,
                          0)
        >= 0) {
        try {
            object.resize(objectSize);
        } catch (...) {
            object.clear();
        }
    }
    if (!object.empty()
        && BCryptCreateHash(algorithm,
                            &hash,
                            reinterpret_cast<PUCHAR>(object.data()),
                            objectSize,
                            const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(key.data())),
                            static_cast<ULONG>(key.size()),
                            0)
               >= 0
        && BCryptHashData(hash,
                          const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(message.data())),
                          static_cast<ULONG>(message.size()),
                          0)
               >= 0
        && BCryptFinishHash(
               hash, reinterpret_cast<PUCHAR>(output.data()), static_cast<ULONG>(output.size()), 0)
               >= 0) {
        success = true;
    }
    if (hash != nullptr) {
        (void)BCryptDestroyHash(hash);
    }
    (void)BCryptCloseAlgorithmProvider(algorithm, 0);
    std::fill(object.begin(), object.end(), std::byte{});
    return success;
}

} // namespace

bool load_host_token(std::vector<std::byte>& output) noexcept {
    output.clear();
    const DWORD required = GetEnvironmentVariableW(kHostTokenVariable, nullptr, 0);
    if (required <= 1 || required > 4096) {
        return false;
    }
    try {
        std::wstring wide(required, L'\0');
        const DWORD written = GetEnvironmentVariableW(kHostTokenVariable, wide.data(), required);
        if (written == 0 || written >= required) {
            return false;
        }
        wide.resize(written);
        const int utf8Size = WideCharToMultiByte(CP_UTF8,
                                                 WC_ERR_INVALID_CHARS,
                                                 wide.data(),
                                                 static_cast<int>(wide.size()),
                                                 nullptr,
                                                 0,
                                                 nullptr,
                                                 nullptr);
        if (utf8Size < 32) {
            SecureZeroMemory(wide.data(), wide.size() * sizeof(wchar_t));
            return false;
        }
        output.resize(static_cast<std::size_t>(utf8Size));
        if (WideCharToMultiByte(CP_UTF8,
                                WC_ERR_INVALID_CHARS,
                                wide.data(),
                                static_cast<int>(wide.size()),
                                reinterpret_cast<char*>(output.data()),
                                utf8Size,
                                nullptr,
                                nullptr)
            != utf8Size) {
            output.clear();
            SecureZeroMemory(wide.data(), wide.size() * sizeof(wchar_t));
            return false;
        }
        SecureZeroMemory(wide.data(), wide.size() * sizeof(wchar_t));
        return true;
    } catch (...) {
        output.clear();
        return false;
    }
}

bool sign_frame(control::Frame& frame, std::span<const std::byte> key) noexcept {
    frame.authenticationTag.fill(std::byte{});
    std::vector<std::byte> wire;
    if (!control::encode_frame(frame, wire)) {
        return false;
    }
    return hmac_sha256(key, wire, frame.authenticationTag);
}

bool verify_frame(const control::Frame& frame, std::span<const std::byte> key) noexcept {
    control::Frame unsignedFrame{};
    std::vector<std::byte> wire;
    std::array<std::byte, control::kAuthenticationTagSize> expected{};
    try {
        unsignedFrame = frame;
    } catch (...) {
        return false;
    }
    unsignedFrame.authenticationTag.fill(std::byte{});
    if (!control::encode_frame(unsignedFrame, wire) || !hmac_sha256(key, wire, expected)) {
        return false;
    }
    unsigned char difference{};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        difference |=
            std::to_integer<unsigned char>(expected[index] ^ frame.authenticationTag[index]);
    }
    return difference == 0;
}

bool random_identifier(Identifier& output) noexcept {
    return BCryptGenRandom(nullptr,
                           reinterpret_cast<PUCHAR>(output.bytes.data()),
                           static_cast<ULONG>(output.bytes.size()),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG)
           >= 0;
}

bool sha256_file(const wchar_t* path, BuildFingerprint& output) noexcept {
    output = {};
    if (path == nullptr || path[0] == L'\0') {
        return false;
    }
    const HANDLE file = CreateFileW(path,
                                    GENERIC_READ,
                                    FILE_SHARE_READ,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    std::vector<std::byte> object;
    bool success = false;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0) {
        ULONG objectSize{};
        ULONG returned{};
        if (BCryptGetProperty(algorithm,
                              BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&objectSize),
                              sizeof(objectSize),
                              &returned,
                              0)
            >= 0) {
            try {
                object.resize(objectSize);
            } catch (...) {
                object.clear();
            }
        }
        if (!object.empty()
            && BCryptCreateHash(algorithm,
                                &hash,
                                reinterpret_cast<PUCHAR>(object.data()),
                                objectSize,
                                nullptr,
                                0,
                                0)
                   >= 0) {
            std::array<std::byte, 64 * 1024> buffer{};
            DWORD read{};
            bool reading = true;
            while (
                reading
                && ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)
                       != FALSE) {
                if (read == 0) {
                    reading = false;
                    success = BCryptFinishHash(hash,
                                               reinterpret_cast<PUCHAR>(output.bytes.data()),
                                               static_cast<ULONG>(output.bytes.size()),
                                               0)
                              >= 0;
                } else if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), read, 0)
                           < 0) {
                    break;
                }
            }
            SecureZeroMemory(buffer.data(), buffer.size());
        }
    }
    if (hash != nullptr) {
        (void)BCryptDestroyHash(hash);
    }
    if (algorithm != nullptr) {
        (void)BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    (void)CloseHandle(file);
    std::fill(object.begin(), object.end(), std::byte{});
    if (!success) {
        output = {};
    }
    return success;
}

} // namespace sunrise::realm::windows
