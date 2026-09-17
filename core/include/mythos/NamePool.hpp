// ============================================================================
// NamePool — FNamePool resolution and FName decoding (read-only).
// ============================================================================
#pragma once
#include "mythos/ReadOnlyMemory.hpp"

#include <cstddef>
#include <cstdint>

namespace mythos {

class NamePool {
public:
	explicit NamePool(const ReadOnlyMemory& memory) : memory_(memory) {}

	void Reset() { pool_ = 0; }
	bool Attach(uintptr_t poolAddress);

	[[nodiscard]] bool ready() const { return pool_ != 0; }
	[[nodiscard]] uintptr_t poolAddress() const { return pool_; }

	// Decodes a comparison index into UTF-8. Handles both narrow and wide
	// entries and enforces every bound from Offsets::NamePool.
	[[nodiscard]] bool Resolve(uint32_t comparisonIndex, char* out, size_t outSize) const;

	// Resolves an FName stored at `nameAddress` (UObject::Name).
	[[nodiscard]] bool ReadName(uintptr_t nameAddress, char* out, size_t outSize) const;

	// UObject helpers: object name and class name.
	[[nodiscard]] bool ReadObjectName(uintptr_t object, char* out, size_t outSize) const;
	[[nodiscard]] bool ReadClassName(uintptr_t object, char* out, size_t outSize) const;

	// Validates a candidate pool address without committing to it.
	[[nodiscard]] static bool IsPlausible(const ReadOnlyMemory& memory, uintptr_t poolAddress);
	// Stricter form: entry 0 must decode to "None".
	[[nodiscard]] static bool IsCertain(const ReadOnlyMemory& memory, uintptr_t poolAddress);

private:
	[[nodiscard]] bool ResolveIn(uintptr_t poolAddress, uint32_t comparisonIndex,
	                             char* out, size_t outSize) const;

	const ReadOnlyMemory& memory_;
	uintptr_t pool_ = 0;
};

// Case-insensitive substring search used for class/bone-name classification.
[[nodiscard]] bool ContainsCaseInsensitive(const char* haystack, const char* needle);

// Matches the FNamePool signature inside one scanned chunk. Returns the pool
// address or 0. Used by the resolver's bounded fallback scan.
[[nodiscard]] uintptr_t MatchNamePoolSignature(const ReadOnlyMemory& memory,
                                               const uint8_t* data, size_t size, uintptr_t address);

} // namespace mythos
