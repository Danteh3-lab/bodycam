// ============================================================================
// ReadOnlyMemory — the only memory interface the runtime is allowed to use.
//
// It exposes guarded reads, module metadata and PE-section enumeration.
// There is deliberately no write, patch, protection-change or function-call
// API: the read-only contract is enforced by construction and audited by the
// static contract test.
// ============================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace nova {

// User-space plausibility window. Kernel addresses and low guard pages are
// never dereferenced even if a game pointer is corrupt.
inline constexpr uintptr_t kMinUserAddress = 0x10000ull;
inline constexpr uintptr_t kMaxUserAddress = 0x00007FFFFFFFFFFFull;

// True when [address, address+size) stays inside the user-space window and
// does not wrap around the address space.
[[nodiscard]] bool IsPlausibleRange(uintptr_t address, size_t size);

// True when a pointer value itself is a plausible user-space address.
[[nodiscard]] inline bool IsPlausiblePointer(uintptr_t address) {
	return address >= kMinUserAddress && address <= kMaxUserAddress;
}

struct ModuleInfo {
	uintptr_t    base = 0;
	size_t       size = 0;
	std::wstring name;

	[[nodiscard]] bool valid() const { return base != 0 && size != 0; }
};

struct SectionRange {
	uintptr_t start = 0;
	size_t    size = 0;
	uint32_t  characteristics = 0;

	[[nodiscard]] bool executable() const { return (characteristics & kImageScnMemExecute) != 0; }
	[[nodiscard]] bool writable() const { return (characteristics & kImageScnMemWrite) != 0; }

	static constexpr uint32_t kImageScnMemExecute = 0x20000000u;
	static constexpr uint32_t kImageScnMemWrite = 0x80000000u;
};

inline constexpr int kMaxSections = 64;

class ReadOnlyMemory {
public:
	virtual ~ReadOnlyMemory() = default;

	// Module under observation (the game image). May be invalid before the
	// module is discoverable.
	[[nodiscard]] virtual ModuleInfo module() const = 0;

	// Guarded copy of `size` bytes from `address`. Returns false instead of
	// faulting when the range is unmapped, implausible, or too short.
	[[nodiscard]] virtual bool read(uintptr_t address, void* out, size_t size) const = 0;

	// Enumerates PE sections. `executable == true` selects code sections;
	// `executable == false` selects non-executable writable data sections.
	// Returns the number of entries written.
	virtual int sections(bool executable, SectionRange* out, int maxOut) const = 0;

	// ---- Convenience readers (all funnel through read()) -------------------
	template <typename T>
	[[nodiscard]] bool readValue(uintptr_t address, T& out) const {
		static_assert(std::is_trivially_copyable_v<T>, "readValue requires a trivially copyable type");
		out = T{};
		return read(address, &out, sizeof(T));
	}

	template <typename T>
	[[nodiscard]] bool readRaw(uintptr_t address, T& out) const {
		static_assert(std::is_trivially_copyable_v<T>, "readRaw requires a trivially copyable type");
		return read(address, &out, sizeof(T));
	}

	[[nodiscard]] bool readPointer(uintptr_t address, uintptr_t& out) const {
		out = 0;
		if (!readValue<uintptr_t>(address, out)) return false;
		if (!IsPlausiblePointer(out)) { out = 0; return false; }
		return true;
	}

	[[nodiscard]] bool readBytes(uintptr_t address, void* out, size_t size) const {
		if (out == nullptr || size == 0) return false;
		return read(address, out, size);
	}

	[[nodiscard]] bool readUtf16String(uintptr_t address, size_t charCount, std::wstring& out) const {
		out.clear();
		if (charCount == 0 || charCount > 4096) return false;
		std::wstring buffer(charCount, L'\0');
		if (!read(address, buffer.data(), charCount * sizeof(wchar_t))) return false;
		out.swap(buffer);
		return true;
	}
};

// Bounds-checked view over a UE TArray (data + count + capacity).
// `ok` is only set when every bound passed validation; an empty but valid
// array has ok == true and count == 0.
struct ArrayView {
	uintptr_t data = 0;
	int32_t   count = 0;
	int32_t   capacity = 0;
	bool      ok = false;

	[[nodiscard]] bool valid() const { return ok; }
};

// Reads a TArray header at `address`, validating every bound. Returns an
// invalid view (count == 0) when the container cannot be trusted.
[[nodiscard]] ArrayView ReadArrayView(const ReadOnlyMemory& memory, uintptr_t address, int maxCount);

// Reads one element pointer out of an ArrayView. Returns false on bad bounds.
[[nodiscard]] bool ReadArrayElement(const ReadOnlyMemory& memory, const ArrayView& view,
                                    int index, uintptr_t& out);

} // namespace nova
