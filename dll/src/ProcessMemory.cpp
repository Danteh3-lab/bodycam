#include "ProcessMemory.hpp"

#include <cstring>

namespace mythos_host {
namespace {

__declspec(noinline) bool GuardedCopy(void* destination, const void* source, size_t size) {
	__try {
		std::memcpy(destination, source, size);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

} // namespace

bool ProcessMemory::Attach(const wchar_t* moduleName) {
	moduleHandle_ = GetModuleHandleW(moduleName);
	if (moduleHandle_ == nullptr) return false;

	const auto* base = reinterpret_cast<const uint8_t*>(moduleHandle_);
	IMAGE_DOS_HEADER dos{};
	if (!GuardedCopy(&dos, base, sizeof(dos))) return false;
	if (dos.e_magic != IMAGE_DOS_SIGNATURE) return false;
	if (dos.e_lfanew <= 0 || dos.e_lfanew > 0x1000) return false;

	IMAGE_NT_HEADERS64 nt{};
	if (!GuardedCopy(&nt, base + dos.e_lfanew, sizeof(nt))) return false;
	if (nt.Signature != IMAGE_NT_SIGNATURE) return false;

	info_.base = reinterpret_cast<uintptr_t>(base);
	info_.size = nt.OptionalHeader.SizeOfImage;
	info_.name = moduleName;
	return info_.valid();
}

mythos::ModuleInfo ProcessMemory::module() const {
	return info_;
}

bool ProcessMemory::read(uintptr_t address, void* out, size_t size) const {
	if (out == nullptr || size == 0) return false;
	if (!mythos::IsPlausibleRange(address, size)) return false;
	return GuardedCopy(out, reinterpret_cast<const void*>(address), size);
}

int ProcessMemory::sections(bool executable, mythos::SectionRange* out, int maxOut) const {
	if (out == nullptr || maxOut <= 0) return 0;
	if (!info_.valid()) return 0;

	IMAGE_DOS_HEADER dos{};
	if (!read(info_.base, &dos, sizeof(dos))) return 0;
	if (dos.e_magic != IMAGE_DOS_SIGNATURE) return 0;
	if (dos.e_lfanew <= 0 || dos.e_lfanew > 0x1000) return 0;

	IMAGE_NT_HEADERS64 nt{};
	if (!read(info_.base + dos.e_lfanew, &nt, sizeof(nt))) return 0;
	if (nt.Signature != IMAGE_NT_SIGNATURE) return 0;

	const int sectionCount = nt.FileHeader.NumberOfSections;
	if (sectionCount <= 0 || sectionCount > 96) return 0;
	const WORD optionalSize = nt.FileHeader.SizeOfOptionalHeader;
	if (optionalSize < sizeof(IMAGE_OPTIONAL_HEADER64) || optionalSize > 0x400) return 0;

	const uintptr_t sectionBase =
		info_.base + dos.e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER) + optionalSize;

	int written = 0;
	for (int i = 0; i < sectionCount && written < maxOut; ++i) {
		IMAGE_SECTION_HEADER header{};
		if (!read(sectionBase + static_cast<uintptr_t>(i) * sizeof(IMAGE_SECTION_HEADER),
		          &header, sizeof(header))) {
			continue;
		}

		const bool isExecutable = (header.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
		const bool isWritable = (header.Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
		if (executable) {
			if (!isExecutable) continue;
		} else {
			if (isExecutable || !isWritable) continue;
		}

		const size_t size = header.Misc.VirtualSize;
		if (size == 0 || size > 0x10000000) continue;

		mythos::SectionRange& range = out[written];
		range.start = info_.base + header.VirtualAddress;
		range.size = size;
		range.characteristics = header.Characteristics;
		++written;
	}
	return written;
}

} // namespace mythos_host
