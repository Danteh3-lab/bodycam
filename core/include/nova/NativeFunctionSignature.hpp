// ============================================================================
// NativeFunctionSignature — pure byte matching for the verified engine
// function prologue. Lives in the read-only core so it can be unit-tested
// without touching game memory or engine interaction.
// ============================================================================
#pragma once
#include "Offsets.hpp"

#include <cstddef>
#include <cstdint>

namespace nova {

inline constexpr std::size_t kVerifiedNativePrologueSize =
	Offsets::Signatures::NativeFunctionPrologueSize;

// True only when the buffer is at least one full prologue and matches the
// verified build's bytes exactly. Null and short buffers are rejected.
[[nodiscard]] bool MatchesVerifiedNativePrologue(const uint8_t* bytes, std::size_t size);

} // namespace nova
