#include "BuildIdentity.hpp"

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace mythos_host {
namespace {

std::wstring ModulePath() {
	std::vector<wchar_t> buffer(MAX_PATH);
	for (;;) {
		const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
		                                        static_cast<DWORD>(buffer.size()));
		if (length == 0) return {};
		if (length < buffer.size()) return std::wstring(buffer.data(), length);
		buffer.resize(buffer.size() * 2);
	}
}

std::string FileVersionString(const std::wstring& path) {
	DWORD dummy = 0;
	const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &dummy);
	if (size == 0) return {};

	std::vector<uint8_t> data(size);
	if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};

	VS_FIXEDFILEINFO* fixed = nullptr;
	UINT fixedLength = 0;
	if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fixed), &fixedLength) ||
	    fixed == nullptr || fixedLength < sizeof(VS_FIXEDFILEINFO)) {
		return {};
	}
	if (fixed->dwSignature != 0xFEEF04BDu) return {};

	char buffer[64] = {};
	std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u",
	              HIWORD(fixed->dwFileVersionMS), LOWORD(fixed->dwFileVersionMS),
	              HIWORD(fixed->dwFileVersionLS), LOWORD(fixed->dwFileVersionLS));
	return buffer;
}

Offsets::ImageIdentity ReadImageIdentity() {
	Offsets::ImageIdentity identity;
	const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
	if (base == nullptr) return identity;

	IMAGE_DOS_HEADER dos{};
	std::memcpy(&dos, base, sizeof(dos));
	if (dos.e_magic != IMAGE_DOS_SIGNATURE) return identity;
	if (dos.e_lfanew <= 0 || dos.e_lfanew > 0x1000) return identity;

	IMAGE_NT_HEADERS64 nt{};
	std::memcpy(&nt, base + dos.e_lfanew, sizeof(nt));
	if (nt.Signature != IMAGE_NT_SIGNATURE) return identity;

	identity.sizeOfImage = nt.OptionalHeader.SizeOfImage;
	identity.timeDateStamp = nt.FileHeader.TimeDateStamp;
	identity.checkSum = nt.OptionalHeader.CheckSum;
	identity.valid = true;
	return identity;
}

std::string JoinProfileIdentity() {
	const Offsets::Profile& profile = Offsets::kActiveProfile;
	std::string text = profile.name;
	text += " | app=";
	text += std::to_string(profile.steamAppId);
	text += " build=";
	text += std::to_string(profile.steamBuild);
	return text;
}

} // namespace

bool BuildIdentity::knownMismatch() const {
	const Offsets::Profile& profile = Offsets::kActiveProfile;

	if (Offsets::ImageIdentityConflictsWithProfile(image, profile)) return true;

	// Legacy FileVersion pin (none of the current Steam builds ship one).
	const char* pinned = profile.knownFileVersion;
	if (pinned == nullptr || *pinned == '\0') return false;
	if (fileVersion.empty()) return false; // cannot discover: inject and gate
	return fileVersion != pinned;
}

BuildIdentity QueryBuildIdentity() {
	BuildIdentity identity;
	identity.modulePath = ModulePath();
	identity.fileVersion = FileVersionString(identity.modulePath);
	identity.image = ReadImageIdentity();

	HANDLE file = CreateFileW(identity.modulePath.c_str(), GENERIC_READ,
	                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	                          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file != INVALID_HANDLE_VALUE) {
		LARGE_INTEGER size{};
		if (GetFileSizeEx(file, &size)) identity.moduleSize = static_cast<uint64_t>(size.QuadPart);
		CloseHandle(file);
	}

	identity.fingerprint = BuildFingerprintString(identity);
	return identity;
}

std::string BuildFingerprintString(const BuildIdentity& identity) {
	char header[128] = {};
	std::snprintf(header, sizeof(header), " | pe=0x%08X/0x%08X/0x%08X",
	              identity.image.sizeOfImage, identity.image.timeDateStamp,
	              identity.image.checkSum);

	std::string text = JoinProfileIdentity();
	text += " | file_version=";
	text += identity.fileVersion.empty() ? "unknown" : identity.fileVersion;
	text += header;
	text += " | image_bytes=";
	text += std::to_string(identity.moduleSize);
	return text;
}

} // namespace mythos_host
