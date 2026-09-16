// ============================================================================
// Platform — small Win32 helpers: NOVA data paths, animation preferences,
// edge-triggered key polling and UTF-8 conversion.
// ============================================================================
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace nova_host::platform {

[[nodiscard]] std::filesystem::path LocalAppDataDirectory();
[[nodiscard]] std::filesystem::path SettingsPath();
[[nodiscard]] std::filesystem::path LogDirectory();

// Windows client-area animation preference (SPI_GETCLIENTAREAANIMATION).
[[nodiscard]] bool AnimationsEnabled();

// Edge-triggered key press (true once per press).
[[nodiscard]] bool ConsumeKeyPress(int virtualKey);

[[nodiscard]] std::string ToUtf8(const std::wstring& text);
[[nodiscard]] std::wstring ToWide(const std::string& text);

[[nodiscard]] uint64_t MonotonicMilliseconds();

} // namespace nova_host::platform
