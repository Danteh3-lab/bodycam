// ============================================================================
// ModuleScanner — bounded, chunked PE-section scanner.
//
// All scanning in the runtime is driven through this class so every fallback
// has an explicit byte budget and can be suspended/resumed across ticks. It
// never blocks the render loop and never runs unbounded work on one call.
// ============================================================================
#pragma once
#include "mythos/ReadOnlyMemory.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mythos {

class ModuleScanner {
public:
	// Callback over one chunk. Return true to stop scanning (found/abort).
	using ChunkFn = bool (*)(void* context, uintptr_t address, const uint8_t* data, size_t size);

	struct Cursor {
		int    sectionIndex = 0;
		size_t offset = 0;
		bool   finished = false;
	};

	enum class StepResult {
		Found,            // the callback asked to stop
		Exhausted,        // no more sections to scan
		BudgetExhausted,  // budget spent, call again with a larger budget
	};

	ModuleScanner(const ReadOnlyMemory& memory, bool executableSections);

	// Selects the section set again (e.g. after the module becomes available).
	void Refresh();

	[[nodiscard]] bool available() const { return sectionCount_ > 0; }
	[[nodiscard]] int sectionCount() const { return sectionCount_; }

	// Scans at most `maxBytes` from the cursor. A chunk never exceeds
	// Offsets::Scan::ChunkBytes + overlap; a budget smaller than one chunk
	// still performs exactly one chunk so progress is always possible.
	StepResult Step(Cursor& cursor, size_t maxBytes, ChunkFn fn, void* context) const;

private:
	[[nodiscard]] bool ReadChunk(uintptr_t address, size_t size, std::vector<uint8_t>& out) const;

	const ReadOnlyMemory& memory_;
	bool executableSections_;
	SectionRange sections_[kMaxSections];
	int sectionCount_ = 0;
};

} // namespace mythos
