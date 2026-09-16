// ============================================================================
// WorldFixture — deterministic in-memory UE world for resolver/collector
// tests. Builds a complete, valid read-only chain plus configurable players.
// ============================================================================
#pragma once
#include "fake_memory.h"
#include "fake_names.h"

#include "Offsets.hpp"
#include "nova/Projection.hpp"
#include "nova/UnrealTypes.hpp"

#include <vector>

namespace novatest {

using nova::FQuat;
using nova::FRotator;
using nova::FTransform;
using nova::FVector;

class WorldFixture {
public:
	WorldFixture();

	// ---- Fixture API -------------------------------------------------------
	uintptr_t AddPlayer(int32_t teamId, float health, float maxHealth,
	                    const char* className, const FVector& rootPosition,
	                    bool withSkeleton = true, const char* playerName = "Player");
	uintptr_t AddDrone(const char* className, const FVector& rootPosition);

	void SetCamera(const FVector& location, const FRotator& rotation, float fov);
	void ClearCamera();

	// Points the controller's PlayerState at a state that is not in the
	// roster, so the world cannot be proven.
	void MakeWorldUnproven();

	// Creates a second complete world whose roster does (or does not) contain
	// the local PlayerState, then points ULevel::OwningWorld at it. Used to
	// exercise the rescue/re-anchor path.
	uintptr_t CreateAlternateWorld(bool containsLocalPlayerState);

	[[nodiscard]] uintptr_t moduleBase() const { return memory.moduleBase(); }
	[[nodiscard]] uintptr_t dataSectionBase() const { return dataSectionBase_; }
	[[nodiscard]] uintptr_t gworldSlot() const { return gworldSlot_; }
	[[nodiscard]] uintptr_t world() const { return world_; }
	[[nodiscard]] uintptr_t gameInstance() const { return gameInstance_; }
	[[nodiscard]] uintptr_t localPlayer() const { return localPlayer_; }
	[[nodiscard]] uintptr_t playerController() const { return playerController_; }
	[[nodiscard]] uintptr_t level() const { return level_; }
	[[nodiscard]] uintptr_t cameraManager() const { return cameraManager_; }
	[[nodiscard]] uintptr_t gameState() const { return gameState_; }
	[[nodiscard]] uintptr_t localPlayerState() const { return localPlayerState_; }
	[[nodiscard]] uintptr_t localPawn() const { return localPawn_; }
	[[nodiscard]] uintptr_t rosterArray() const { return rosterArray_; }
	[[nodiscard]] const std::vector<uintptr_t>& rosterEntries() const { return roster_; }

	FakeMemory memory;
	FakeNamePool names{ memory };

	uint32_t classCharacter = 0;
	uint32_t classDrone = 0;
	uint32_t classPerk = 0;

private:
	uintptr_t BuildPawn(const char* className, const FVector& rootPosition, float health,
	                    float maxHealth, bool withSkeleton);
	uintptr_t BuildPlayerState(int32_t teamId, uintptr_t pawn, const char* playerName);
	void BuildRoster(const std::vector<uintptr_t>& entries);
	uintptr_t BuildSkeletonAsset();
	uintptr_t BuildSkeletonMesh(uintptr_t pawn, uintptr_t root, const FVector& rootPosition);

	uintptr_t dataSectionBase_ = 0;
	uintptr_t gworldSlot_ = 0;
	uintptr_t world_ = 0;
	uintptr_t gameInstance_ = 0;
	uintptr_t localPlayersArray_ = 0;
	uintptr_t localPlayer_ = 0;
	uintptr_t playerController_ = 0;
	uintptr_t level_ = 0;
	uintptr_t cameraManager_ = 0;
	uintptr_t gameState_ = 0;
	uintptr_t localPlayerState_ = 0;
	uintptr_t localPawn_ = 0;
	uintptr_t rosterArray_ = 0;
	std::vector<uintptr_t> roster_;

	uintptr_t skeletonAsset_ = 0;
	uintptr_t skeletonBoneInfo_ = 0;
	uintptr_t skeletonPose_ = 0;
	int skeletonBoneCount_ = 0;
	uintptr_t strayState_ = 0;
};

void WriteTransform(FakeMemory& memory, uintptr_t address, const FVector& translation);

} // namespace novatest
