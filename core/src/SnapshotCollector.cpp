#include "mythos/SnapshotCollector.hpp"

#include "mythos/Skeleton.hpp"
#include "mythos/UnrealTypes.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mythos {
namespace {

constexpr size_t kMaxClassKinds = 256;
constexpr double kMaxBoneLinkCm = 60.0;
constexpr double kMaxCoordinateCm = 1.0e7;

void CopyNarrowString(const char* source, char* out, size_t outSize) {
	if (out == nullptr || outSize == 0) return;
	size_t i = 0;
	for (; i + 1 < outSize && source[i] != '\0'; ++i) out[i] = source[i];
	out[i] = '\0';
}

} // namespace

PlayerKind ClassifyByClassName(const char* className) {
	if (className == nullptr || *className == '\0') return PlayerKind::Unknown;
	if (ContainsCaseInsensitive(className, "drone") ||
	    ContainsCaseInsensitive(className, "perk")) {
		return PlayerKind::Drone;
	}
	if (ContainsCaseInsensitive(className, "character") ||
	    ContainsCaseInsensitive(className, "pawn")) {
		return PlayerKind::Player;
	}
	return PlayerKind::Unknown;
}

SnapshotCollector::SnapshotCollector(const ReadOnlyMemory& memory, const NamePool& names,
                                     const VisibilityProbe* visibility)
	: memory_(memory), names_(names), visibility_(visibility), skeletons_(memory, names) {}

void SnapshotCollector::ClearCaches() {
	skeletons_.Clear();
	classKinds_.clear();
}

bool SnapshotCollector::ReadCamera(const WorldContext& world, CameraView& out) const {
	out = CameraView{};
	const uintptr_t cameraManager = world.cameraManager;
	if (!IsPlausiblePointer(cameraManager)) return false;

	const uintptr_t povTiers[2] = {
		cameraManager + Offsets::POVInfo,
		cameraManager + Offsets::POVInfoLast,
	};

	for (int tier = 0; tier < 2; ++tier) {
		FVector location;
		FVector rotation;
		float fov = 0.0f;
		if (!memory_.readRaw<FVector>(povTiers[tier] + Offsets::MviLocation, location)) continue;
		if (!memory_.readRaw<FVector>(povTiers[tier] + Offsets::MviRotation, rotation)) continue;
		if (!memory_.readValue<float>(povTiers[tier] + Offsets::MviFOV, fov)) continue;
		if (!std::isfinite(fov) || fov < Offsets::Limits::FovMin || fov > Offsets::Limits::FovMax) continue;
		if (location.x == 0.0 && location.y == 0.0 && location.z == 0.0) continue;

		out.location = location;
		out.rotation = FRotator{ rotation.x, rotation.y, rotation.z };
		out.fov = fov;
		(void)memory_.readValue<float>(povTiers[tier] + Offsets::MviAspectRatio, out.aspectRatio);
		uint32_t flags = 0;
		(void)memory_.readValue<uint32_t>(povTiers[tier] + Offsets::MviFlags, flags);
		out.constrainAspect = (flags & 0x01u) != 0;
		out.axisConstraint = world.aspectAxis;
		out.usedFallbackFov = false;
		out.valid = true;
		return true;
	}

	// Both tiers failed to provide a sane FOV; keep position/rotation if they
	// are readable and let the renderer use the configured fallback FOV.
	FVector location;
	FVector rotation;
	if (memory_.readRaw<FVector>(povTiers[0] + Offsets::MviLocation, location) &&
	    memory_.readRaw<FVector>(povTiers[0] + Offsets::MviRotation, rotation)) {
		out.location = location;
		out.rotation = FRotator{ rotation.x, rotation.y, rotation.z };
		out.fov = 0.0f;
		out.axisConstraint = world.aspectAxis;
		out.usedFallbackFov = true;
		out.valid = true;
		return true;
	}
	return false;
}

bool SnapshotCollector::ReadHealth(uintptr_t pawn, float& health, float& maxHealth) const {
	health = 0.0f;
	maxHealth = 0.0f;

	uintptr_t attributes = 0;
	if (!memory_.readPointer(pawn + Offsets::BCCharacterSet, attributes)) return false;
	if (!memory_.readValue<float>(attributes + Offsets::HealthCurrent, health)) return false;
	(void)memory_.readValue<float>(attributes + Offsets::MaxHealthCurrent, maxHealth);

	if (!std::isfinite(health) || health < Offsets::Limits::MinHealth || health > Offsets::Limits::MaxHealth) {
		return false;
	}
	if (!std::isfinite(maxHealth) || maxHealth <= 0.0f || maxHealth > Offsets::Limits::MaxHealth) {
		maxHealth = 100.0f;
	}
	return true;
}

bool SnapshotCollector::ReadPlayerName(uintptr_t playerState, char* out, size_t outSize) const {
	if (out == nullptr || outSize == 0) return false;
	out[0] = '\0';

	uintptr_t data = 0;
	int32_t length = 0;
	if (!memory_.readPointer(playerState + Offsets::PSName, data)) return false;
	if (!memory_.readValue<int32_t>(playerState + Offsets::PSName + 0x08, length)) return false;
	if (length <= 1 || length > Offsets::Limits::MaxNameLen) return false;

	wchar_t buffer[Offsets::Limits::MaxNameLen + 1] = {};
	const int characters = length - 1;
	if (!memory_.readBytes(data, buffer, static_cast<size_t>(characters) * sizeof(wchar_t))) return false;
	buffer[characters] = L'\0';

	const int converted = WideCharToMultiByte(CP_UTF8, 0, buffer, characters,
	                                          out, static_cast<int>(outSize) - 1,
	                                          nullptr, nullptr);
	if (converted <= 0) {
		out[0] = '\0';
		return false;
	}
	out[converted] = '\0';
	return true;
}

PlayerKind SnapshotCollector::ClassifyPawn(uintptr_t pawn) {
	uintptr_t pawnClass = 0;
	if (!memory_.readPointer(pawn + Offsets::UObject::Class, pawnClass)) return PlayerKind::Unknown;

	auto it = classKinds_.find(pawnClass);
	if (it != classKinds_.end()) return static_cast<PlayerKind>(it->second);

	PlayerKind kind = PlayerKind::Unknown;
	char className[128] = {};
	if (names_.ReadName(pawnClass + Offsets::UObject::Name, className, sizeof(className)) &&
	    className[0] != '\0') {
		kind = ClassifyByClassName(className);
		if (classKinds_.size() > kMaxClassKinds) classKinds_.clear();
		classKinds_[pawnClass] = static_cast<uint8_t>(kind);
	}
	return kind;
}

bool SnapshotCollector::ReadCapsule(uintptr_t pawn, bool isDrone, PlayerSnapshot& player) const {
	if (!player.hasRoot) return false;

	double halfHeight = 88.0;
	double radius = 34.0;
	if (isDrone) {
		halfHeight = 30.0;
		radius = 30.0;
	} else {
		uintptr_t capsule = 0;
		if (memory_.readPointer(pawn + Offsets::CapsuleComponent, capsule)) {
			float values[2] = {};
			if (memory_.read(capsule + Offsets::CapsuleHalfHeight, values, sizeof(values))) {
				halfHeight = static_cast<double>(values[0]);
				radius = static_cast<double>(values[1]);
			}
		}
	}

	if (!std::isfinite(halfHeight) || halfHeight < 10.0 || halfHeight > 500.0) halfHeight = 88.0;
	if (!std::isfinite(radius) || radius < 5.0 || radius > 300.0) radius = 34.0;

	player.hasCapsule = true;
	player.capsuleHalfHeight = halfHeight;
	player.capsuleRadius = radius;
	player.capsuleTop = FVector{ player.root.x, player.root.y, player.root.z + halfHeight };
	player.capsuleBottom = FVector{ player.root.x, player.root.y, player.root.z - halfHeight };
	return true;
}

bool SnapshotCollector::ReadPose(uintptr_t pawn, PlayerSnapshot& player) {
	uintptr_t meshComponent = 0;
	if (!memory_.readPointer(pawn + Offsets::SkeletalMeshComponent, meshComponent)) {
		++diagnostics_.bones.noMesh;
		return false;
	}
	const uintptr_t boneMesh = SkeletonCache::ResolveBoneMesh(memory_, meshComponent);

	int bufferIndex = 0;
	(void)memory_.readValue<int32_t>(boneMesh + Offsets::BoneBufferIndex, bufferIndex);
	if (bufferIndex < 0 || bufferIndex > 1) bufferIndex = 0;

	uintptr_t poseData = 0;
	int32_t poseCount = 0;
	bool gotPose = false;
	for (int attempt = 0; attempt < 2 && !gotPose; ++attempt) {
		const int buffer = (bufferIndex + attempt) & 1;
		const uintptr_t arrayAddress =
			boneMesh + Offsets::ActiveBoneArray +
			static_cast<uintptr_t>(buffer) * Offsets::BoneArrayStride;
		const ArrayView view = ReadArrayView(memory_, arrayAddress, Offsets::Limits::MaxBones);
		if (view.valid() && view.count > 0) {
			poseData = view.data;
			poseCount = view.count;
			gotPose = true;
		}
	}
	if (!gotPose) {
		++diagnostics_.bones.noPose;
		return false;
	}

	const int count = (std::min)(poseCount, kMaxDrawBones);
	if (count <= 0) {
		++diagnostics_.bones.noPose;
		return false;
	}

	std::vector<FTransform> localPose(static_cast<size_t>(count));
	if (!memory_.read(poseData, localPose.data(), localPose.size() * sizeof(FTransform))) {
		++diagnostics_.bones.noPose;
		return false;
	}

	FTransform componentToWorld;
	if (!memory_.readRaw<FTransform>(meshComponent + Offsets::ComponentToWorld, componentToWorld)) {
		++diagnostics_.bones.noPose;
		return false;
	}
	if (!TransformLooksSane(componentToWorld) ||
	    !MeshBelongsToPawn(memory_, pawn, componentToWorld)) {
		++diagnostics_.bones.badMesh;
		return false;
	}

	uintptr_t asset = 0;
	if (!memory_.readPointer(boneMesh + Offsets::SkinnedAsset, asset)) {
		(void)memory_.readPointer(boneMesh + Offsets::SkeletalMeshAssetOld, asset);
	}
	const SkeletonInfo* skeleton = skeletons_.GetForAsset(asset, diagnostics_.bones);

	player.bones.clear();
	player.bones.reserve(static_cast<size_t>(count));
	for (int i = 0; i < count; ++i) {
		BonePoint point;
		point.world = componentToWorld.transformPosition(localPose[static_cast<size_t>(i)].translation);
		if (skeleton != nullptr && skeleton->valid &&
		    static_cast<size_t>(i) < skeleton->parents.size()) {
			if (skeleton->named) {
				if (static_cast<size_t>(i) < skeleton->drawParent.size()) {
					point.parent = skeleton->drawParent[static_cast<size_t>(i)];
				}
				if (static_cast<size_t>(i) < skeleton->keep.size()) {
					point.core = skeleton->keep[static_cast<size_t>(i)];
				}
			} else {
				const int parent = skeleton->parents[static_cast<size_t>(i)];
				if (parent >= 0 && parent < count) {
					const double link =
						point.world.distance(player.bones[static_cast<size_t>(parent)].world);
					if (link <= kMaxBoneLinkCm) point.parent = parent;
				}
			}
		}
		player.bones.push_back(point);
	}

	player.hasPose = true;
	player.poseNamed = skeleton != nullptr && skeleton->valid && skeleton->named;
	player.headBone = (skeleton != nullptr && skeleton->valid) ? skeleton->head : -1;
	return true;
}

GameSnapshotPtr SnapshotCollector::Capture(const WorldContext& world, ResolveStage stage,
                                           const CaptureSettings& settings,
                                           uint64_t sequence, uint64_t nowMs) {
	auto snapshot = std::make_shared<GameSnapshot>();
	snapshot->sequence = sequence;
	snapshot->capturedAtMs = nowMs;
	snapshot->stage = stage;

	diagnostics_ = CollectionDiagnostics{};

	if (!world.valid) {
		snapshot->valid = false;
		return snapshot;
	}

	CameraView camera;
	if (!ReadCamera(world, camera)) {
		snapshot->valid = false;
		return snapshot;
	}
	snapshot->camera = camera;

	const ArrayView roster{ world.playerArray, world.playerCount, world.playerCount, true };
	if (!roster.valid() || roster.count <= 0) {
		snapshot->valid = true; // An empty roster is a valid (empty) snapshot.
		return snapshot;
	}

	diagnostics_.entities.roster = roster.count;
	const double maxDistanceCm = settings.maxDistanceMeters * 100.0;

	for (int i = 0; i < roster.count; ++i) {
		uintptr_t playerState = 0;
		if (!ReadArrayElement(memory_, roster, i, playerState)) {
			++diagnostics_.entities.noPawn;
			continue;
		}

		uintptr_t pawn = 0;
		if (!memory_.readPointer(playerState + Offsets::PSPawn, pawn)) {
			++diagnostics_.entities.noPawn;
			continue;
		}
		if (world.acknowledgedPawn != 0 && pawn == world.acknowledgedPawn) {
			++diagnostics_.entities.self;
			continue;
		}

		PlayerSnapshot player;

		int32_t teamId = -1;
		player.hasTeam = memory_.readValue<int32_t>(playerState + Offsets::PSTeamId, teamId);
		if (player.hasTeam) player.teamId = teamId;
		player.sameTeam = world.localTeam >= 0 && player.hasTeam && player.teamId == world.localTeam;

		if (!settings.retainAimCandidates && player.sameTeam && !settings.showTeam) {
			++diagnostics_.entities.teamFiltered;
			continue;
		}
		if (!settings.retainAimCandidates && !player.sameTeam && !settings.showEnemy) {
			++diagnostics_.entities.teamFiltered;
			continue;
		}

		player.kind = ClassifyPawn(pawn);
		const bool isDrone = player.isDrone();

		float health = 0.0f;
		float maxHealth = 100.0f;
		const bool gotHealth = !isDrone && ReadHealth(pawn, health, maxHealth);
		if (gotHealth) {
			player.hasHealth = true;
			player.health = health;
			player.maxHealth = maxHealth;
			player.dead = health <= 0.0f;
		} else if (!isDrone) {
			++diagnostics_.entities.noHealth;
		}

		if (!settings.retainAimCandidates && settings.hideDead && player.hasHealth && player.dead) {
			++diagnostics_.entities.dead;
			continue;
		}

		if (isDrone) {
			++diagnostics_.entities.drones;
			if (!settings.retainAimCandidates && !settings.showDrones) {
				++diagnostics_.entities.droneFiltered;
				continue;
			}
		}

		uintptr_t root = 0;
		if (memory_.readPointer(pawn + Offsets::RootComponent, root)) {
			FVector rootPosition;
			if (memory_.readRaw<FVector>(root + Offsets::C2WTranslation, rootPosition) &&
			    rootPosition.finite() &&
			    std::fabs(rootPosition.x) <= kMaxCoordinateCm &&
			    std::fabs(rootPosition.y) <= kMaxCoordinateCm &&
			    std::fabs(rootPosition.z) <= kMaxCoordinateCm) {
				player.hasRoot = true;
				player.root = rootPosition;
			}
		}

		if (settings.collectPose() && !isDrone) {
			(void)ReadPose(pawn, player);
		}

		(void)ReadCapsule(pawn, isDrone, player);

		if (!player.hasRoot && !player.hasPose) {
			++diagnostics_.entities.noPosition;
			continue;
		}

		const FVector reference =
			(player.hasPose && !player.bones.empty()) ? player.bones[0].world : player.root;
		double distanceCm = camera.location.distance(reference);
		if (!(distanceCm > 0.0) || distanceCm > kMaxCoordinateCm) distanceCm = 0.0;
		player.distanceMeters = distanceCm / 100.0;

		if (!settings.retainAimCandidates && maxDistanceCm > 0.0 && distanceCm > maxDistanceCm) {
			++diagnostics_.entities.tooFar;
			continue;
		}

		if (settings.visibility && visibility_ != nullptr && visibility_->active()) {
			player.visible = visibility_->IsVisible(pawn, camera.location);
			if (!player.visible) ++diagnostics_.entities.occluded;
		}

		if (settings.name) {
			char buffer[Offsets::Limits::MaxNameLen] = {};
			if (ReadPlayerName(playerState, buffer, sizeof(buffer)) && buffer[0] != '\0') {
				CopyNarrowString(buffer, player.name, sizeof(player.name));
				player.hasName = true;
			}
		}

		++diagnostics_.entities.drawn;
		snapshot->players.push_back(std::move(player));
	}

	snapshot->counters = diagnostics_.entities;
	snapshot->valid = true;
	return snapshot;
}

} // namespace mythos
