// ============================================================================
// VisibilityProbe — read-only interface the snapshot collector uses to ask
// "is this pawn visible from the camera?".
//
// The implementation lives in the quarantined DLL interaction module
// (VisCheck), which is the only place allowed to call engine functions. The
// core never sees that code, so nova_core stays free of engine interaction.
// ============================================================================
#pragma once
#include "nova/UnrealTypes.hpp"

#include <cstdint>

namespace nova {

class VisibilityProbe {
public:
	virtual ~VisibilityProbe() = default;

	// True once the probe can answer. While inactive the collector leaves
	// PlayerSnapshot::visible at its fail-open default.
	[[nodiscard]] virtual bool active() const = 0;

	// Must fail open: return true when the query cannot run or faults.
	[[nodiscard]] virtual bool IsVisible(uintptr_t pawn,
	                                     const FVector& cameraLocation) const = 0;
};

} // namespace nova
