// ============================================================================
// Aim — pure aim-assist math and target selection.
//
// Everything here is a pure function over an immutable GameSnapshot: no game
// memory, no engine calls, no rendering dependency. The quarantined DLL
// AimController applies the resulting step through the engine.
// ============================================================================
#pragma once
#include "nova/GameSnapshot.hpp"
#include "nova/Projection.hpp"
#include "nova/UnrealTypes.hpp"

#include <cstddef>

namespace nova {

// View rotation (degrees) pointing from `source` to `target`, matching the
// legacy convention: pitch = atan2(dz, horizontal distance), yaw = atan2(dy, dx).
[[nodiscard]] FRotator CalcAngle(const FVector& source, const FVector& target);

// Wraps an angle difference into [-180, 180].
[[nodiscard]] double NormalizeAngle(double angle);

// Moves `current` toward `target` by 1/smoothing of the remaining error and
// clamps each axis to +/- maxStep degrees. Roll stays 0.
[[nodiscard]] FRotator ComputeAimStep(const FRotator& current, const FRotator& target,
                                      double smoothing, double maxStep);

// Immediate blend of the full error; used by the legacy ControlRotation method.
[[nodiscard]] FRotator SmoothRotation(const FRotator& current, const FRotator& target,
                                      float smoothing);

struct AimTarget {
	std::size_t playerIndex = 0;
	FVector     world;
	Vec2d       screen;
	double      crosshairPixels = 0.0;
};

struct AimSelectionSettings {
	double fovPixels = 150.0; // radius around the crosshair, screen pixels
	int    boneMode = 0;      // 0 = head bone, otherwise mid-height
	bool   visibleOnly = false;
	bool   ignoreTeam = true;
};

// Picks the player closest to the crosshair inside the FOV circle. Mirrors the
// legacy selection: self/drones/dead are skipped, `ignoreTeam` drops
// teammates, `visibleOnly` drops occluded players. Returns false when no
// player qualifies.
[[nodiscard]] bool SelectAimTarget(const GameSnapshot& snapshot,
                                   const ProjectionSettings& projection,
                                   float screenWidth,
                                   float screenHeight,
                                   const AimSelectionSettings& settings,
                                   AimTarget& out);

} // namespace nova
