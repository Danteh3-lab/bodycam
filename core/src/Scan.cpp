#include "mythos/Scan.hpp"

#include "Offsets.hpp"

namespace mythos {

ModuleScanner::ModuleScanner(const ReadOnlyMemory& memory, bool executableSections)
	: memory_(memory), executableSections_(executableSections) {
	Refresh();
}

void ModuleScanner::Refresh() {
	sectionCount_ = memory_.sections(executableSections_, sections_, kMaxSections);
}

bool ModuleScanner::ReadChunk(uintptr_t address, size_t size, std::vector<uint8_t>& out) const {
	if (size == 0) return false;
	if (out.size() < size) out.resize(size);
	return memory_.read(address, out.data(), size);
}

ModuleScanner::StepResult ModuleScanner::Step(Cursor& cursor, size_t maxBytes, ChunkFn fn, void* context) const {
	if (cursor.finished) return StepResult::Exhausted;
	if (fn == nullptr) {
		cursor.finished = true;
		return StepResult::Exhausted;
	}

	const size_t chunkBudget = Offsets::Scan::ChunkBytes + Offsets::Scan::ChunkOverlap;
	std::vector<uint8_t> buffer;

	size_t bytesThisStep = 0;
	while (cursor.sectionIndex < sectionCount_) {
		const SectionRange& section = sections_[cursor.sectionIndex];
		if (cursor.offset >= section.size) {
			++cursor.sectionIndex;
			cursor.offset = 0;
			continue;
		}

		if (bytesThisStep >= maxBytes && bytesThisStep > 0) {
			return StepResult::BudgetExhausted;
		}

		const size_t remaining = section.size - cursor.offset;
		const size_t size = remaining > chunkBudget ? chunkBudget : remaining;
		const uintptr_t address = section.start + cursor.offset;

		if (ReadChunk(address, size, buffer)) {
			if (fn(context, address, buffer.data(), size)) {
				cursor.finished = true;
				return StepResult::Found;
			}
		}
		cursor.offset += size;
		bytesThisStep += size;
	}

	cursor.finished = true;
	return StepResult::Exhausted;
}

} // namespace mythos
