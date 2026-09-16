#include "world_fixture.h"

#include <cstring>

namespace novatest {
namespace {

constexpr int kBoneCount = 9;

const char* const kBoneNames[kBoneCount] = {
	"pelvis", "spine_01", "spine_02", "neck", "head",
	"thigh_l", "calf_l", "foot_l", "upperarm_l",
};

const int32_t kBoneParents[kBoneCount] = {
	-1, 0, 1, 2, 3, 0, 5, 6, 2,
};

// T-pose heights: the head bone is the highest, arms sit at shoulder height.
const double kBoneHeights[kBoneCount] = {
	0.0, 10.0, 20.0, 30.0, 40.0, -5.0, -25.0, -45.0, 25.0,
};

void WriteFString(FakeMemory& memory, uintptr_t address, const char* text) {
	const size_t length = std::strlen(text);
	const uintptr_t data = memory.Allocate(64);
	std::vector<wchar_t> wide(length + 1);
	for (size_t i = 0; i < length; ++i) wide[i] = static_cast<wchar_t>(text[i]);
	wide[length] = L'\0';
	memory.Write(data, wide.data(), wide.size() * sizeof(wchar_t));

	memory.WritePointer(address + 0x00, data);
	memory.WriteInt32(address + 0x08, static_cast<int32_t>(length + 1));
	memory.WriteInt32(address + 0x0C, static_cast<int32_t>(length + 1));
}

void WriteTArray(FakeMemory& memory, uintptr_t address, uintptr_t data, int32_t count) {
	memory.WritePointer(address + 0x00, data);
	memory.WriteInt32(address + 0x08, count);
	memory.WriteInt32(address + 0x0C, count);
}

} // namespace

void WriteTransform(FakeMemory& memory, uintptr_t address, const FVector& translation) {
	FTransform transform;
	transform.rotation = FQuat{ 0.0, 0.0, 0.0, 1.0 };
	transform.translation = translation;
	transform.scale = FVector{ 1.0, 1.0, 1.0 };
	memory.WriteValue<FTransform>(address, transform);
}

WorldFixture::WorldFixture() {
	// Module sections: a code section and a data section that covers the
	// known GWorld RVA.
	const uintptr_t codeStart = moduleBase() + 0x1000;
	memory.AddRegion(codeStart, 0x2000);
	memory.AddSection(codeStart, 0x2000, true, false);

	dataSectionBase_ = moduleBase() + (Offsets::Globals::GWorld & ~static_cast<uintptr_t>(0xFFF));
	memory.AddRegion(dataSectionBase_, 0x2000);
	memory.AddSection(dataSectionBase_, 0x2000, false, true);

	gworldSlot_ = moduleBase() + Offsets::Globals::GWorld;

	// Class names and the default name pool.
	classCharacter = names.AddName("BP_Character_C");
	classDrone = names.AddName("Drone");
	classPerk = names.AddName("Perk");

	// World -> GameInstance / GameState.
	world_ = memory.Allocate(0x300);
	gameInstance_ = memory.Allocate(0x100);
	gameState_ = memory.Allocate(0x400);
	memory.WritePointer(world_ + Offsets::GameInstance, gameInstance_);
	memory.WritePointer(world_ + Offsets::GameState, gameState_);

	// GameInstance -> LocalPlayers[0].
	localPlayersArray_ = memory.Allocate(0x10);
	memory.WritePointer(localPlayersArray_, 0); // filled after localPlayer_ exists
	WriteTArray(memory, gameInstance_ + Offsets::LocalPlayer, localPlayersArray_, 1);

	// LocalPlayer -> PlayerController.
	localPlayer_ = memory.Allocate(0x100);
	memory.WritePointer(localPlayersArray_, localPlayer_);
	playerController_ = memory.Allocate(0x400);
	memory.WritePointer(localPlayer_ + Offsets::LPPlayerController, playerController_);
	memory.WriteUInt8(localPlayer_ + Offsets::AspectAxisConstraint,
	                  static_cast<uint8_t>(nova::AspectAxis::MaintainXFOV));

	// PlayerController -> CameraManager / PlayerState / Pawn, plus the level
	// used for the outer-world rescue.
	level_ = memory.Allocate(0x100);
	cameraManager_ = memory.Allocate(0x2000);
	memory.WritePointer(playerController_ + Offsets::CameraManager, cameraManager_);
	memory.WritePointer(playerController_ + Offsets::UObject::Outer, level_);
	memory.WritePointer(level_ + Offsets::LevelOwningWorld, world_);

	// Local player pawn (also the acknowledged pawn).
	localPlayerState_ = BuildPlayerState(0, 0, "Local");
	localPawn_ = BuildPawn("BP_Character_C", FVector{ 200.0, 0.0, 100.0 }, 100.0f, 100.0f, true);
	memory.WritePointer(localPlayerState_ + Offsets::PSPawn, localPawn_);
	memory.WritePointer(playerController_ + Offsets::ACPlayerState, localPlayerState_);
	memory.WritePointer(playerController_ + Offsets::AcknowledgedPawn, localPawn_);

	// Camera: current POV tier is valid.
	SetCamera(FVector{ 0.0, 0.0, 100.0 }, FRotator{ 0.0, 0.0, 0.0 }, 90.0f);

	// Roster starts with the local player state.
	roster_.push_back(localPlayerState_);
	BuildRoster(roster_);

	// Anchor slot -> world.
	memory.WritePointer(gworldSlot_, world_);

	// Reference skeleton shared by all pawns.
	skeletonAsset_ = BuildSkeletonAsset();
}

uintptr_t WorldFixture::BuildSkeletonAsset() {
	skeletonAsset_ = memory.Allocate(0x800);

	skeletonBoneCount_ = kBoneCount;

	const size_t infoStride = Offsets::MeshBoneInfoStride;
	skeletonBoneInfo_ = memory.Allocate(static_cast<size_t>(kBoneCount) * infoStride);
	for (int i = 0; i < kBoneCount; ++i) {
		const uint32_t nameIndex = names.AddName(kBoneNames[i]);
		const uintptr_t entry = skeletonBoneInfo_ + static_cast<uintptr_t>(i) * infoStride;
		memory.WriteValue<uint32_t>(entry, nameIndex);
		memory.WriteInt32(entry + Offsets::MeshBoneInfoParent, kBoneParents[i]);
	}

	skeletonPose_ = memory.Allocate(static_cast<size_t>(kBoneCount) * sizeof(FTransform));
	for (int i = 0; i < kBoneCount; ++i) {
		WriteTransform(memory, skeletonPose_ + static_cast<uintptr_t>(i) * sizeof(FTransform),
		               FVector{ 0.0, 0.0, kBoneHeights[i] });
	}

	const uintptr_t boneInfoArray = skeletonAsset_ + Offsets::RefSkelRawBoneInfo;
	WriteTArray(memory, boneInfoArray, skeletonBoneInfo_, kBoneCount);
	WriteTArray(memory, boneInfoArray + Offsets::RefSkelRawBonePose, skeletonPose_, kBoneCount);
	return skeletonAsset_;
}

uintptr_t WorldFixture::BuildPlayerState(int32_t teamId, uintptr_t pawn, const char* playerName) {
	const uintptr_t playerState = memory.Allocate(0x400);
	memory.WritePointer(playerState + Offsets::PSPawn, pawn);
	memory.WriteInt32(playerState + Offsets::PSTeamId, teamId);
	WriteFString(memory, playerState + Offsets::PSName, playerName);
	return playerState;
}

uintptr_t WorldFixture::BuildSkeletonMesh(uintptr_t pawn, uintptr_t root, const FVector& rootPosition) {
	const uintptr_t mesh = memory.Allocate(0x700);
	memory.WritePointer(pawn + Offsets::SkeletalMeshComponent, mesh);
	memory.WritePointer(mesh + Offsets::SkinnedAsset, skeletonAsset_);
	memory.WritePointer(mesh + Offsets::LeaderPoseComponent, 0);
	memory.WriteInt32(mesh + Offsets::BoneBufferIndex, 0);

	const uintptr_t poseArray = memory.Allocate(static_cast<size_t>(kBoneCount) * sizeof(FTransform));
	memory.WritePointer(mesh + Offsets::ActiveBoneArray, poseArray);
	memory.WriteInt32(mesh + Offsets::ActiveBoneArray + 0x08, kBoneCount);
	memory.WriteInt32(mesh + Offsets::ActiveBoneArray + 0x0C, kBoneCount);

	for (int i = 0; i < kBoneCount; ++i) {
		WriteTransform(memory, poseArray + static_cast<uintptr_t>(i) * sizeof(FTransform),
		               FVector{ 0.0, 0.0, kBoneHeights[i] });
	}

	WriteTransform(memory, mesh + Offsets::ComponentToWorld, rootPosition);
	(void)root;
	return mesh;
}

uintptr_t WorldFixture::BuildPawn(const char* className, const FVector& rootPosition, float health,
                                  float maxHealth, bool withSkeleton) {
	const uintptr_t pawn = memory.Allocate(0x700);

	// UObject: class pointer -> class object whose Name is the class FName.
	const uintptr_t classObject = memory.Allocate(0x40);
	uint32_t nameIndex = 0;
	if (std::strcmp(className, "Drone") == 0) {
		nameIndex = classDrone;
	} else if (std::strcmp(className, "Perk") == 0) {
		nameIndex = classPerk;
	} else {
		nameIndex = classCharacter;
	}
	memory.WriteValue<uint32_t>(classObject + Offsets::UObject::Name, nameIndex);
	memory.WritePointer(pawn + Offsets::UObject::Class, classObject);

	// Bodycam attribute set with direct health floats.
	const uintptr_t attributes = memory.Allocate(0x100);
	memory.WritePointer(pawn + Offsets::BCCharacterSet, attributes);
	memory.WriteFloat(attributes + Offsets::HealthCurrent, health);
	memory.WriteFloat(attributes + Offsets::MaxHealthCurrent, maxHealth);

	// Root component with a component-to-world translation. C2WTranslation is
	// the inline FVector at ComponentToWorld + 0x20.
	const uintptr_t root = memory.Allocate(0x300);
	memory.WritePointer(pawn + Offsets::RootComponent, root);
	WriteTransform(memory, root + Offsets::ComponentToWorld,
	               FVector{ rootPosition.x, rootPosition.y, rootPosition.z });

	// Capsule dimensions.
	const uintptr_t capsule = memory.Allocate(0x600);
	memory.WritePointer(pawn + Offsets::CapsuleComponent, capsule);
	memory.WriteFloat(capsule + Offsets::CapsuleHalfHeight, 88.0f);
	memory.WriteFloat(capsule + Offsets::CapsuleRadius, 34.0f);

	if (withSkeleton) {
		(void)BuildSkeletonMesh(pawn, root, rootPosition);
	}
	return pawn;
}

uintptr_t WorldFixture::AddPlayer(int32_t teamId, float health, float maxHealth,
                                  const char* className, const FVector& rootPosition,
                                  bool withSkeleton, const char* playerName) {
	const uintptr_t pawn = BuildPawn(className, rootPosition, health, maxHealth, withSkeleton);
	const uintptr_t playerState = BuildPlayerState(teamId, pawn, playerName);
	roster_.push_back(playerState);
	BuildRoster(roster_);
	return playerState;
}

uintptr_t WorldFixture::AddDrone(const char* className, const FVector& rootPosition) {
	return AddPlayer(-1, 0.0f, 0.0f, className, rootPosition, false, "Drone");
}

void WorldFixture::BuildRoster(const std::vector<uintptr_t>& entries) {
	if (rosterArray_ == 0) {
		rosterArray_ = memory.Allocate(0x400);
	}
	memory.WriteArray<uintptr_t>(rosterArray_, roster_.data(), roster_.size());
	WriteTArray(memory, gameState_ + Offsets::PlayerArray, rosterArray_,
	            static_cast<int32_t>(roster_.size()));
	(void)entries;
}

void WorldFixture::SetCamera(const FVector& location, const FRotator& rotation, float fov) {
	const uintptr_t pov = cameraManager_ + Offsets::POVInfo;
	memory.WriteValue<double>(pov + Offsets::MviLocation + 0x00, location.x);
	memory.WriteValue<double>(pov + Offsets::MviLocation + 0x08, location.y);
	memory.WriteValue<double>(pov + Offsets::MviLocation + 0x10, location.z);
	memory.WriteValue<double>(pov + Offsets::MviRotation + 0x00, rotation.pitch);
	memory.WriteValue<double>(pov + Offsets::MviRotation + 0x08, rotation.yaw);
	memory.WriteValue<double>(pov + Offsets::MviRotation + 0x10, rotation.roll);
	memory.WriteFloat(pov + Offsets::MviFOV, fov);
	memory.WriteFloat(pov + Offsets::MviAspectRatio, 1.7777f);
	memory.WriteValue<uint32_t>(pov + Offsets::MviFlags, 0u);
}

void WorldFixture::ClearCamera() {
	memory.WriteFloat(cameraManager_ + Offsets::POVInfo + Offsets::MviFOV, 0.0f);
	memory.WriteValue<double>(cameraManager_ + Offsets::POVInfo + Offsets::MviLocation + 0x00, 0.0);
	memory.WriteValue<double>(cameraManager_ + Offsets::POVInfo + Offsets::MviLocation + 0x08, 0.0);
	memory.WriteValue<double>(cameraManager_ + Offsets::POVInfo + Offsets::MviLocation + 0x10, 0.0);
	memory.WriteFloat(cameraManager_ + Offsets::POVInfoLast + Offsets::MviFOV, 0.0f);
}

void WorldFixture::MakeWorldUnproven() {
	// PlayerState points at a state that is not in the current roster and the
	// outer world is the same world, so the rescue path cannot help either.
	strayState_ = BuildPlayerState(0, localPawn_, "Stray");
	memory.WritePointer(playerController_ + Offsets::ACPlayerState, strayState_);
	memory.WritePointer(level_ + Offsets::LevelOwningWorld, world_);
}

uintptr_t WorldFixture::CreateAlternateWorld(bool containsLocalPlayerState) {
	const uintptr_t alternateWorld = memory.Allocate(0x300);
	const uintptr_t alternateGameState = memory.Allocate(0x400);
	memory.WritePointer(alternateWorld + Offsets::GameInstance, gameInstance_);
	memory.WritePointer(alternateWorld + Offsets::GameState, alternateGameState);

	const uintptr_t alternateRoster = memory.Allocate(0x100);
	const uintptr_t residentState =
		(containsLocalPlayerState && strayState_ != 0) ? strayState_ : localPlayerState_;
	memory.WritePointer(alternateRoster, residentState);
	WriteTArray(memory, alternateGameState + Offsets::PlayerArray, alternateRoster, 1);

	memory.WritePointer(level_ + Offsets::LevelOwningWorld, alternateWorld);
	return alternateWorld;
}

} // namespace novatest
