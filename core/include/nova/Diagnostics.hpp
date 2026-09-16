// ============================================================================
// Diagnostics — counters and state shared between the core and the UI.
// Plain data only; no player-identifying content ever enters these structs.
// ============================================================================
#pragma once
#include <cstdint>
#include <string>

namespace nova {

// Entity rejection counters collected while building a snapshot. Every entity
// the roster offers is accounted for exactly once.
struct EntityCounters {
	int roster = 0;
	int drawn = 0;
	int noPawn = 0;
	int self = 0;
	int teamFiltered = 0;
	int dead = 0;
	int noHealth = 0;
	int noPosition = 0;
	int tooFar = 0;
	int drones = 0;
	int droneFiltered = 0;
	int offScreen = 0;

	int rejected() const {
		return noPawn + self + teamFiltered + dead + noPosition + tooFar + droneFiltered;
	}
};

// Skeleton/mesh read diagnostics, mirroring the legacy bone counters.
struct BoneCounters {
	int noMesh = 0;
	int noPose = 0;
	int noAsset = 0;
	int noHierarchy = 0;
	int badMesh = 0;
	int skeletonsDrawn = 0;

	void reset() { *this = BoneCounters{}; }
};

// Read-failure counters for the resolver itself.
struct ReadStats {
	uint32_t failures = 0;
	uint32_t successes = 0;

	void reset() { failures = 0; successes = 0; }
};

} // namespace nova
