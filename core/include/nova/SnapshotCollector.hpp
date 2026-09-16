// ============================================================================
// SnapshotCollector — converts a validated world into immutable snapshots.
// Runs on the worker thread only; never touches the render loop.
// ============================================================================
#pragma once
#include "nova/Diagnostics.hpp"
#include "nova/GameSnapshot.hpp"
#include "nova/NamePool.hpp"
#include "nova/ReadOnlyMemory.hpp"
#include "nova/Skeleton.hpp"
#include "nova/VisibilityProbe.hpp"

#include <cstdint>
#include <unordered_map>

namespace nova {

struct CaptureSettings {
	bool   name = true;
	bool   health = true;
	bool   distance = true;
	bool   skeleton = false;
	bool   headDot = false;
	bool   boxFromBones = true;
	bool   showEnemy = true;
	bool   showTeam = false;
	bool   showDrones = true;
	bool   hideDead = true;
	bool   visibility = false; // run the attached VisibilityProbe per pawn
	double maxDistanceMeters = 300.0;

	[[nodiscard]] bool collectPose() const { return boxFromBones || skeleton || headDot; }
};

struct CollectionDiagnostics {
	EntityCounters entities;
	BoneCounters   bones;
	uint32_t       readFailures = 0;
};

class SnapshotCollector {
public:
	// `visibility` is optional; when null, PlayerSnapshot::visible stays true.
	SnapshotCollector(const ReadOnlyMemory& memory, const NamePool& names,
	                  const VisibilityProbe* visibility = nullptr);

	// Builds a complete immutable snapshot. `stage` is the resolver stage at
	// capture time, surfaced in the UI when the snapshot is invalid.
	[[nodiscard]] GameSnapshotPtr Capture(const WorldContext& world, ResolveStage stage,
	                                      const CaptureSettings& settings,
	                                      uint64_t sequence, uint64_t nowMs);

	// Camera-only helper (also used by the diagnostics view).
	[[nodiscard]] bool ReadCamera(const WorldContext& world, CameraView& out) const;

	// Clears skeleton and classification caches after map transitions.
	void ClearCaches();

	[[nodiscard]] const CollectionDiagnostics& diagnostics() const { return diagnostics_; }
	[[nodiscard]] const SkeletonCache& skeletons() const { return skeletons_; }

private:
	[[nodiscard]] bool ReadHealth(uintptr_t pawn, float& health, float& maxHealth) const;
	[[nodiscard]] bool ReadPlayerName(uintptr_t playerState, char* out, size_t outSize) const;
	[[nodiscard]] PlayerKind ClassifyPawn(uintptr_t pawn);
	[[nodiscard]] bool ReadCapsule(uintptr_t pawn, bool isDrone, PlayerSnapshot& player) const;
	[[nodiscard]] bool ReadPose(uintptr_t pawn, PlayerSnapshot& player);

	const ReadOnlyMemory& memory_;
	const NamePool&       names_;
	const VisibilityProbe* visibility_ = nullptr;
	SkeletonCache         skeletons_;
	std::unordered_map<uintptr_t, uint8_t> classKinds_;
	CollectionDiagnostics diagnostics_;
};

// Class-name based entity classification, shared with tests.
[[nodiscard]] PlayerKind ClassifyByClassName(const char* className);

} // namespace nova
