// ============================================================================
// RuntimeDiagnostics — the read-only runtime's published health model.
// The control panel renders these values; no player data is included.
// ============================================================================
#pragma once
#include "mythos/Diagnostics.hpp"
#include "mythos/WorldResolver.hpp"

#include <cstdint>
#include <string>

namespace mythos {

enum class RuntimeState {
	Starting = 0,
	WaitingForWindow,
	Resolving,
	Ready,
	OffsetsInvalid,
	Stopping,
};

[[nodiscard]] const char* RuntimeStateName(RuntimeState state);

// Short user-facing sentence for the header health indicator.
[[nodiscard]] const char* RuntimeStateDescription(RuntimeState state);

struct RuntimeDiagnostics {
	std::string   buildFingerprint;   // version/build identity, no player data
	std::string   buildIdentity;
	RuntimeState  state = RuntimeState::Starting;
	ResolveStage  stage = ResolveStage::NoModule;
	bool          namesReady = false;
	bool          worldValid = false;
	bool          cameraValid = false;
	bool          recovering = false;
	bool          rendererOk = true;

	uintptr_t anchorRva = 0;
	uintptr_t worldPointer = 0;
	uint32_t  resolverMs = 0;
	uint32_t  lastScanMs = 0;
	uint32_t  snapshotAgeMs = 0;
	int       candidates = 0;
	int       fullScans = 0;
	int       reanchors = 0;
	int       rescues = 0;
	uint32_t  readFailures = 0;

	int       skeletonCacheSize = 0;
	int       skeletonNamed = 0;
	int       skeletonCoreBones = 0;
	int       refSkeletonOffset = -1;

	EntityCounters entities;
	EntityCounters lastCounters;
	double   overlayFps = 0.0;
	uint64_t sequence = 0;
};

} // namespace mythos
