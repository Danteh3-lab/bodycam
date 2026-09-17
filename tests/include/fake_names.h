// ============================================================================
// FakeNamePool — deterministic FNamePool fixture.
// Builds a pool with a single block; names are appended as narrow entries.
// ============================================================================
#pragma once
#include "fake_memory.h"

#include "Offsets.hpp"

#include <cstring>
#include <string>

namespace mythostest {

class FakeNamePool {
public:
	explicit FakeNamePool(FakeMemory& memory) : memory_(memory) {
		base_ = memory_.Allocate(0x1000);
		block_ = memory_.Allocate(0x20000);
		memory_.WritePointer(base_ + Offsets::NamePool::BlocksOffset, block_);
		AddName("None"); // index 0 must decode to "None"
	}

	uint32_t AddName(const char* name) {
		const size_t length = std::strlen(name);
		const size_t entryOffset = cursor_;
		const uint16_t header = static_cast<uint16_t>(length << 6);
		memory_.WriteValue<uint16_t>(block_ + entryOffset, header);
		memory_.Write(block_ + entryOffset + 2, name, length);
		cursor_ += (2 + length + 1) & ~static_cast<size_t>(1);
		return static_cast<uint32_t>(entryOffset / 2);
	}

	uint32_t AddWideName(const wchar_t* name) {
		const size_t length = std::wcslen(name);
		const size_t entryOffset = cursor_;
		const uint16_t header = static_cast<uint16_t>((length << 6) | 1);
		memory_.WriteValue<uint16_t>(block_ + entryOffset, header);
		memory_.Write(block_ + entryOffset + 2, name, length * sizeof(wchar_t));
		cursor_ += (2 + length * 2 + 1) & ~static_cast<size_t>(1);
		return static_cast<uint32_t>(entryOffset / 2);
	}

	[[nodiscard]] uintptr_t address() const { return base_; }

private:
	FakeMemory& memory_;
	uintptr_t base_ = 0;
	uintptr_t block_ = 0;
	size_t cursor_ = 0;
};

} // namespace mythostest
