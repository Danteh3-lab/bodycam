#include "Platform.hpp"

#include <Windows.h>

#include <array>

namespace mythos_host::platform {

std::filesystem::path LocalAppDataDirectory() {
	wchar_t buffer[MAX_PATH] = {};
	const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
	if (length == 0 || length >= MAX_PATH) return std::filesystem::path(L"MYTHOS");
	return std::filesystem::path(buffer) / L"MYTHOS";
}

std::filesystem::path SettingsPath() {
	return LocalAppDataDirectory() / L"settings.json";
}

std::filesystem::path LegacySettingsPath() {
	wchar_t buffer[MAX_PATH] = {};
	const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
	if (length == 0 || length >= MAX_PATH) return std::filesystem::path(L"NOVA") / L"settings.json";
	return std::filesystem::path(buffer) / L"NOVA" / L"settings.json";
}

std::filesystem::path LogDirectory() {
	return LocalAppDataDirectory() / L"logs";
}

bool AnimationsEnabled() {
	BOOL enabled = TRUE;
	if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)) return true;
	return enabled != FALSE;
}

bool ConsumeKeyPress(int virtualKey) {
	static std::array<bool, 256> wasDown{};
	const int index = virtualKey & 0xFF;
	const bool down = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
	const bool pressed = down && !wasDown[static_cast<size_t>(index)];
	wasDown[static_cast<size_t>(index)] = down;
	return pressed;
}

std::string ToUtf8(const std::wstring& text) {
	if (text.empty()) return {};
	const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
	                                     nullptr, 0, nullptr, nullptr);
	if (size <= 0) return {};
	std::string result(static_cast<size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
	                    result.data(), size, nullptr, nullptr);
	return result;
}

std::wstring ToWide(const std::string& text) {
	if (text.empty()) return {};
	const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
	                                     nullptr, 0);
	if (size <= 0) return {};
	std::wstring result(static_cast<size_t>(size), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
	                    result.data(), size);
	return result;
}

uint64_t MonotonicMilliseconds() {
	return static_cast<uint64_t>(GetTickCount64());
}

} // namespace mythos_host::platform
