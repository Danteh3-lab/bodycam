// ============================================================================
// SkeletonCache — per-asset reference-skeleton hierarchy, read-only.
//
// The hierarchy (parents, head bone, core-bone filter) comes from the model's
// own USkeletalMesh RefSkeleton, never from hardcoded indices. Entries are
// cached per asset pointer and cleared on map transitions.
// ============================================================================
#pragma once
#include "nova/Diagnostics.hpp"
#include "nova/NamePool.hpp"
#include "nova/ReadOnlyMemory.hpp"
#include "nova/UnrealTypes.hpp"

#include "Offsets.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace nova {

struct SkeletonInfo {
	std::vector<int32_t> parents;    // index -> parent index (-1 for root)
	std::vector<uint8_t> keep;       // core-bone filter
	std::vector<int32_t> drawParent; // nearest kept ancestor, -1 when none
	int  head = -1;                  // head bone identified from the reference pose
	int  coreCount = 0;
	bool named = false;
	bool valid = false;
};

class SkeletonCache {
public:
	SkeletonCache(const ReadOnlyMemory& memory, const NamePool& names);

	// Returns the cached (or freshly built) skeleton for a skeletal mesh
	// component, resolving leader-pose components first. nullptr when the
	// hierarchy cannot be validated.
	[[nodiscard]] const SkeletonInfo* GetForMeshComponent(uintptr_t meshComponent, BoneCounters& counters);
	[[nodiscard]] const SkeletonInfo* GetForAsset(uintptr_t asset, BoneCounters& counters);

	// Follows USkinnedMeshComponent::LeaderPoseComponent (leader pose meshes
	// store their pose on another component).
	[[nodiscard]] static uintptr_t ResolveBoneMesh(const ReadOnlyMemory& memory, uintptr_t meshComponent);

	void Clear();

	[[nodiscard]] size_t size() const { return cache_.size(); }
	[[nodiscard]] int namedCount() const;
	[[nodiscard]] int totalCoreBones() const;
	[[nodiscard]] int refSkeletonOffset() const { return refSkeletonOffset_; }
	[[nodiscard]] bool refSkeletonFound() const { return refSkeletonOffset_ >= 0; }

private:
	bool FindBoneInfoArray(uintptr_t asset, std::vector<int32_t>& parents, int& foundOffset);
	bool ValidateBoneInfo(uintptr_t arrayAddress, std::vector<int32_t>& parents);
	int  FindHeadBone(uintptr_t boneInfoArray, const std::vector<int32_t>& parents);
	void BuildBoneFilter(uintptr_t boneInfoArray, SkeletonInfo& info);

	const ReadOnlyMemory& memory_;
	const NamePool& names_;
	std::unordered_map<uintptr_t, SkeletonInfo> cache_;
	int refSkeletonOffset_ = -1;
};

// True when the name looks like a drawable body bone (not a twist/finger/IK
// helper). Shared with tests.
[[nodiscard]] bool IsCoreBoneName(const char* name);

// True when a mesh component transform is close enough to the pawn root to
// belong to it. Returns true (permissive) when the root cannot be read.
[[nodiscard]] bool MeshBelongsToPawn(const ReadOnlyMemory& memory, uintptr_t pawn,
                                      const FTransform& componentToWorld);

} // namespace nova
