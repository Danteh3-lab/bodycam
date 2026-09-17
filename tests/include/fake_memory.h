// ============================================================================
// FakeMemory — deterministic read-only memory fixture for tests.
//
// Implements mythos::ReadOnlyMemory over plain byte regions. The write helpers
// below are test-only fixture code; the production interface remains
// read-only. Reads crossing a region boundary fail, which lets tests exercise
// the guarded-read contract without touching a real process.
// ============================================================================
#pragma once
#include "mythos/ReadOnlyMemory.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace mythostest {

class FakeMemory final : public mythos::ReadOnlyMemory {
public:
	explicit FakeMemory(uintptr_t moduleBase = 0x140000000ull,
	                    size_t moduleSize = 0x0A000000ull)
		: moduleBase_(moduleBase),
		  moduleSize_(moduleSize),
		  // Heap lives 1 GiB above the module so RIP-relative 32-bit offsets
		  // in fabricated signatures stay representable, like the real game.
		  nextHeap_(moduleBase + 0x40000000ull) {}

	[[nodiscard]] mythos::ModuleInfo module() const override {
		mythos::ModuleInfo info;
		info.base = moduleBase_;
		info.size = moduleSize_;
		info.name = L"Bodycam-Win64-Shipping.exe";
		return info;
	}

	[[nodiscard]] bool read(uintptr_t address, void* out, size_t size) const override {
		if (out == nullptr || size == 0) return false;
		if (!mythos::IsPlausibleRange(address, size)) return false;

		for (const Region& region : regions_) {
			if (address < region.base) continue;
			const size_t offset = static_cast<size_t>(address - region.base);
			if (offset + size > region.bytes.size()) continue;
			std::memcpy(out, region.bytes.data() + offset, size);
			return true;
		}
		return false;
	}

	int sections(bool executable, mythos::SectionRange* out, int maxOut) const override {
		if (out == nullptr || maxOut <= 0) return 0;
		const std::vector<mythos::SectionRange>& source = executable ? execSections_ : dataSections_;
		int count = 0;
		for (const mythos::SectionRange& section : source) {
			if (count >= maxOut) break;
			out[count++] = section;
		}
		return count;
	}

	// ---- Test-only fixture API --------------------------------------------
	uintptr_t AddRegion(uintptr_t address, size_t size) {
		Region region;
		region.base = address;
		region.bytes.assign(size, 0);
		regions_.push_back(std::move(region));
		return address;
	}

	uintptr_t Allocate(size_t size) {
		nextHeap_ = (nextHeap_ + 0x0F) & ~static_cast<uintptr_t>(0x0F);
		const uintptr_t address = nextHeap_;
		AddRegion(address, size);
		nextHeap_ += size + 0x100; // gap so overruns are not silently shared
		return address;
	}

	void AddSection(uintptr_t start, size_t size, bool executable, bool writable) {
		mythos::SectionRange section;
		section.start = start;
		section.size = size;
		if (executable) section.characteristics |= mythos::SectionRange::kImageScnMemExecute;
		if (writable) section.characteristics |= mythos::SectionRange::kImageScnMemWrite;
		if (executable) {
			execSections_.push_back(section);
		} else {
			dataSections_.push_back(section);
		}
	}

	bool Write(uintptr_t address, const void* data, size_t size) {
		for (Region& region : regions_) {
			if (address < region.base) continue;
			const size_t offset = static_cast<size_t>(address - region.base);
			if (offset + size > region.bytes.size()) continue;
			std::memcpy(region.bytes.data() + offset, data, size);
			return true;
		}
		return false;
	}

	template <typename T>
	bool WriteValue(uintptr_t address, const T& value) {
		static_assert(std::is_trivially_copyable_v<T>);
		return Write(address, &value, sizeof(T));
	}

	template <typename T>
	bool WriteArray(uintptr_t address, const T* values, size_t count) {
		static_assert(std::is_trivially_copyable_v<T>);
		return Write(address, values, count * sizeof(T));
	}

	bool WriteUInt8(uintptr_t address, uint8_t value) { return WriteValue(address, value); }
	bool WriteInt32(uintptr_t address, int32_t value) { return WriteValue(address, value); }
	bool WriteFloat(uintptr_t address, float value) { return WriteValue(address, value); }
	bool WriteDouble(uintptr_t address, double value) { return WriteValue(address, value); }
	bool WritePointer(uintptr_t address, uintptr_t value) { return WriteValue(address, value); }

	[[nodiscard]] uintptr_t moduleBase() const { return moduleBase_; }

private:
	struct Region {
		uintptr_t base = 0;
		std::vector<uint8_t> bytes;
	};

	std::vector<Region> regions_;
	std::vector<mythos::SectionRange> execSections_;
	std::vector<mythos::SectionRange> dataSections_;
	uintptr_t moduleBase_ = 0;
	size_t moduleSize_ = 0;
	uintptr_t nextHeap_ = 0;
};

} // namespace mythostest
