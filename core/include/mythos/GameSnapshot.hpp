// ============================================================================
// GameSnapshot — immutable, pointer-free view of one world sample.
//
// The worker converts game memory into these values; the render loop only
// ever consumes them. Invalid snapshots render nothing rather than falling
// back to stale pointers.
// ============================================================================
#pragma once
#include "mythos/Diagnostics.hpp"
#include "mythos/Projection.hpp"
#include "mythos/UnrealTypes.hpp"
#include "mythos/WorldResolver.hpp"

#include "Offsets.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace mythos {

enum class PlayerKind : uint8_t {
	Unknown = 0,
	Player = 1,
	Drone = 2,
};

// One bone of a sampled pose. `parent` is the precomputed draw link (-1 when
// the bone should not be connected), `core` marks body bones.
struct BonePoint {
	FVector world;
	int32_t parent = -1;
	uint8_t core = 0;
};

struct PlayerSnapshot {
	// Deliberately value-only: no raw game pointers live in a snapshot.
	PlayerKind kind = PlayerKind::Unknown;
	bool       isSelf = false;

	bool       hasTeam = false;
	int32_t    teamId = -1;
	bool       sameTeam = false;

	bool       hasHealth = false;
	float      health = 0.0f;
	float      maxHealth = 100.0f;
	bool       dead = false;

	// Engine visibility query result. Fail-open: stays true when no probe is
	// attached or the probe could not answer.
	bool       visible = true;

	bool       hasName = false;
	char       name[Offsets::Limits::MaxNameLen] = {};

	bool       hasRoot = false;
	FVector    root;

	bool       hasCapsule = false;
	FVector    capsuleTop;
	FVector    capsuleBottom;
	double     capsuleRadius = 34.0;
	double     capsuleHalfHeight = 88.0;

	bool       hasPose = false;
	bool       poseNamed = false;
	int        headBone = -1;
	std::vector<BonePoint> bones;

	double     distanceMeters = 0.0;

	[[nodiscard]] bool isDrone() const { return kind == PlayerKind::Drone; }
};

struct GameSnapshot {
	uint64_t    sequence = 0;
	uint64_t    capturedAtMs = 0;
	bool        valid = false;
	ResolveStage stage = ResolveStage::NoModule;
	CameraView  camera;
	std::vector<PlayerSnapshot> players;
	EntityCounters counters;
};

using GameSnapshotPtr = std::shared_ptr<const GameSnapshot>;

} // namespace mythos
