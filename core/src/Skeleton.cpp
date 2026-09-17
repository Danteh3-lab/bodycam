#include "mythos/Skeleton.hpp"

#include "mythos/UnrealTypes.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mythos {
namespace {

constexpr size_t kMaxCachedSkeletons = 64;

} // namespace

bool MeshBelongsToPawn(const ReadOnlyMemory& memory, uintptr_t pawn, const FTransform& componentToWorld) {
	uintptr_t root = 0;
	if (!memory.readPointer(pawn + Offsets::RootComponent, root)) return true;

	FVector rootPosition;
	if (!memory.readRaw<FVector>(root + Offsets::C2WTranslation, rootPosition)) return true;
	if (!rootPosition.finite()) return true;

	const double dx = componentToWorld.translation.x - rootPosition.x;
	const double dy = componentToWorld.translation.y - rootPosition.y;
	const double dz = componentToWorld.translation.z - rootPosition.z;
	const double squared = dx * dx + dy * dy + dz * dz;
	if (!std::isfinite(squared)) return false;
	return squared <= kMaxMeshOffsetCm * kMaxMeshOffsetCm;
}

bool IsCoreBoneName(const char* name) {
	if (name == nullptr || *name == '\0') return false;

	static const char* const kSkip[] = {
		"twist", "index", "middle", "ring", "pinky", "thumb", "finger",
		"ik_", "weapon", "attach", "socket", "prop", "camera", "root", "armor",
		"corrective", "adjust", "helper", "aim", "offset", "clothing",
		"cloth", "physics", "vb ", "interaction", "center_of_mass"
	};
	for (const char* token : kSkip) {
		if (ContainsCaseInsensitive(name, token)) return false;
	}

	static const char* const kCore[] = {
		"pelvis", "spine", "neck", "head", "clavicle",
		"upperarm", "lowerarm", "hand", "thigh", "calf", "foot", "ball", "toe",
		"shoulder", "elbow", "wrist", "hip", "knee", "ankle", "chest", "leg", "arm"
	};
	for (const char* token : kCore) {
		if (ContainsCaseInsensitive(name, token)) return true;
	}
	return false;
}

SkeletonCache::SkeletonCache(const ReadOnlyMemory& memory, const NamePool& names)
	: memory_(memory), names_(names) {}

uintptr_t SkeletonCache::ResolveBoneMesh(const ReadOnlyMemory& memory, uintptr_t meshComponent) {
	uintptr_t leader = 0;
	if (memory.readPointer(meshComponent + Offsets::LeaderPoseComponent, leader)) return leader;
	return meshComponent;
}

void SkeletonCache::Clear() {
	cache_.clear();
}

int SkeletonCache::namedCount() const {
	int count = 0;
	for (const auto& entry : cache_) {
		if (entry.second.valid && entry.second.named) ++count;
	}
	return count;
}

int SkeletonCache::totalCoreBones() const {
	int count = 0;
	for (const auto& entry : cache_) {
		if (entry.second.valid) count += entry.second.coreCount;
	}
	return count;
}

bool SkeletonCache::ValidateBoneInfo(uintptr_t arrayAddress, std::vector<int32_t>& parents) {
	parents.clear();

	const ArrayView view = ReadArrayView(memory_, arrayAddress, Offsets::Limits::MaxBones);
	if (!view.valid() || view.count <= 8) return false;
	if (!IsPlausibleRange(view.data, static_cast<size_t>(view.count) * Offsets::MeshBoneInfoStride)) {
		return false;
	}

	std::vector<uint8_t> buffer(static_cast<size_t>(view.count) * Offsets::MeshBoneInfoStride);
	if (!memory_.read(view.data, buffer.data(), buffer.size())) return false;

	parents.resize(static_cast<size_t>(view.count));
	for (int32_t i = 0; i < view.count; ++i) {
		int32_t parent = 0;
		std::memcpy(&parent,
		            buffer.data() + static_cast<size_t>(i) * Offsets::MeshBoneInfoStride +
		            Offsets::MeshBoneInfoParent,
		            sizeof(parent));
		if (i == 0) {
			if (parent != -1) return false;
		} else if (parent < 0 || parent >= i) {
			return false;
		}
		parents[static_cast<size_t>(i)] = parent;
	}
	return true;
}

bool SkeletonCache::FindBoneInfoArray(uintptr_t asset, std::vector<int32_t>& parents, int& foundOffset) {
	if (refSkeletonOffset_ >= 0 && ValidateBoneInfo(asset + refSkeletonOffset_, parents)) {
		foundOffset = refSkeletonOffset_;
		return true;
	}

	for (int offset = 0; offset <= static_cast<int>(Offsets::RefSkelScanMax); offset += 8) {
		if (ValidateBoneInfo(asset + offset, parents)) {
			refSkeletonOffset_ = offset;
			foundOffset = offset;
			return true;
		}
	}
	return false;
}

int SkeletonCache::FindHeadBone(uintptr_t boneInfoArray, const std::vector<int32_t>& parents) {
	const uintptr_t poseArray = boneInfoArray + Offsets::RefSkelRawBonePose;
	const ArrayView poses = ReadArrayView(memory_, poseArray, Offsets::Limits::MaxBones);
	if (!poses.valid() || poses.count <= 1) return -1;

	const int count = (std::min)(poses.count, static_cast<int32_t>(parents.size()));
	if (count <= 1) return -1;
	if (!IsPlausibleRange(poses.data, static_cast<size_t>(count) * sizeof(FTransform))) return -1;

	std::vector<FTransform> local(static_cast<size_t>(count));
	if (!memory_.read(poses.data, local.data(), local.size() * sizeof(FTransform))) return -1;

	std::vector<FTransform> world(static_cast<size_t>(count));
	for (int i = 0; i < count; ++i) {
		const int parent = parents[static_cast<size_t>(i)];
		if (i == 0 || parent < 0 || parent >= i) {
			world[static_cast<size_t>(i)] = local[static_cast<size_t>(i)];
		} else {
			FTransform& target = world[static_cast<size_t>(i)];
			const FTransform& parentTransform = world[static_cast<size_t>(parent)];
			target.translation = parentTransform.transformPosition(local[static_cast<size_t>(i)].translation);
			target.rotation = QuatMultiply(parentTransform.rotation, local[static_cast<size_t>(i)].rotation);
			target.scale = FVector{
				parentTransform.scale.x * local[i].scale.x,
				parentTransform.scale.y * local[i].scale.y,
				parentTransform.scale.z * local[i].scale.z,
			};
		}
	}

	int best = -1;
	double bestZ = -1e300;
	for (int i = 0; i < count; ++i) {
		if (world[static_cast<size_t>(i)].translation.z > bestZ) {
			bestZ = world[static_cast<size_t>(i)].translation.z;
			best = i;
		}
	}
	return best;
}

void SkeletonCache::BuildBoneFilter(uintptr_t boneInfoArray, SkeletonInfo& info) {
	const int count = static_cast<int>(info.parents.size());
	info.keep.assign(static_cast<size_t>(count), 0);
	info.drawParent.assign(static_cast<size_t>(count), -1);
	info.coreCount = 0;
	info.named = false;
	if (count <= 0) return;
	if (!names_.ready()) return;

	const ArrayView view = ReadArrayView(memory_, boneInfoArray, Offsets::Limits::MaxBones);
	if (!view.valid() || view.count <= 0) return;

	const int limit = (std::min)(view.count, count);
	int named = 0;
	int headByName = -1;
	char name[128] = {};

	for (int i = 0; i < limit; ++i) {
		const uintptr_t infoAddress =
			view.data + static_cast<uintptr_t>(i) * Offsets::MeshBoneInfoStride;
		if (!names_.ReadName(infoAddress, name, sizeof(name))) continue;
		++named;
		if (IsCoreBoneName(name)) {
			info.keep[static_cast<size_t>(i)] = 1;
			++info.coreCount;
		}
		if (headByName < 0 && _stricmp(name, "head") == 0) headByName = i;
	}

	if (named < limit / 2 || info.coreCount < 4) {
		info.keep.assign(static_cast<size_t>(count), 0);
		info.drawParent.assign(static_cast<size_t>(count), -1);
		info.coreCount = 0;
		info.named = false;
		return;
	}

	info.named = true;
	if (headByName >= 0) info.head = headByName;

	for (int i = 0; i < count; ++i) {
		if (info.keep[static_cast<size_t>(i)] == 0) continue;
		int parent = info.parents[static_cast<size_t>(i)];
		int guard = 0;
		while (parent >= 0 && parent < count && info.keep[static_cast<size_t>(parent)] == 0 &&
		       ++guard < Offsets::Limits::MaxParents) {
			parent = info.parents[static_cast<size_t>(parent)];
		}
		info.drawParent[static_cast<size_t>(i)] =
			(parent >= 0 && parent < count && info.keep[static_cast<size_t>(parent)] != 0) ? parent : -1;
	}
}

const SkeletonInfo* SkeletonCache::GetForAsset(uintptr_t asset, BoneCounters& counters) {
	if (!IsPlausiblePointer(asset)) {
		++counters.noAsset;
		return nullptr;
	}

	auto it = cache_.find(asset);
	if (it != cache_.end() && it->second.valid) {
		// A skeleton discovered before the name pool was ready is retried a
		// bounded number of times so classification eventually succeeds.
		if (!it->second.named && names_.ready()) {
			uintptr_t boneInfoArray = 0;
			std::vector<int32_t> parents;
			int offset = -1;
			if (FindBoneInfoArray(asset, parents, offset)) {
				boneInfoArray = asset + static_cast<uintptr_t>(offset);
				BuildBoneFilter(boneInfoArray, it->second);
			}
		}
		return &it->second;
	}

	SkeletonInfo info;
	int offset = -1;
	if (!FindBoneInfoArray(asset, info.parents, offset)) {
		++counters.noHierarchy;
		return nullptr;
	}

	const uintptr_t boneInfoArray = asset + static_cast<uintptr_t>(offset);
	info.head = FindHeadBone(boneInfoArray, info.parents);
	BuildBoneFilter(boneInfoArray, info);
	info.valid = true;

	if (cache_.size() > kMaxCachedSkeletons) cache_.clear();
	auto result = cache_.emplace(asset, std::move(info));
	return &result.first->second;
}

const SkeletonInfo* SkeletonCache::GetForMeshComponent(uintptr_t meshComponent, BoneCounters& counters) {
	if (!IsPlausiblePointer(meshComponent)) {
		++counters.noAsset;
		return nullptr;
	}

	uintptr_t asset = 0;
	if (!memory_.readPointer(meshComponent + Offsets::SkinnedAsset, asset)) {
		(void)memory_.readPointer(meshComponent + Offsets::SkeletalMeshAssetOld, asset);
	}
	return GetForAsset(asset, counters);
}

} // namespace mythos
