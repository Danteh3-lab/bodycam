// ============================================================================
// WorldResolver — locates and validates the read-only UE world chain.
//
// Resolution order:
//   1. The supplied GWorld RVA, validated through the complete chain.
//   2. A bounded, resumable data-section scan for the anchor slot.
// The FNamePool is resolved the same way (known RVA first, signature scan
// fallback). Every fallback runs through ModuleScanner with an explicit byte
// budget so rendering is never blocked.
// ============================================================================
#pragma once
#include "mythos/Diagnostics.hpp"
#include "mythos/NamePool.hpp"
#include "mythos/ReadOnlyMemory.hpp"
#include "mythos/Scan.hpp"

#include "Offsets.hpp"

#include <cstddef>
#include <cstdint>

namespace mythos {

enum class ResolveStage : uint8_t {
	Ok = 0,
	NoModule,
	NoGWorld,
	NoWorld,
	NoGameInstance,
	NoLocalPlayer,
	NoPlayerController,
	NoCameraManager,
	NoGameState,
	NoPlayerArray,
	NoLocalPawn,
	WorldUnproven,
	OffsetsInvalid,
};

[[nodiscard]] const char* ResolveStageName(ResolveStage stage);

struct WorldContext {
	uintptr_t world = 0;
	uintptr_t gameInstance = 0;
	uintptr_t localPlayer = 0;
	uintptr_t playerController = 0;
	uintptr_t cameraManager = 0;
	uintptr_t acknowledgedPawn = 0;
	uintptr_t playerState = 0;
	uintptr_t attributeSet = 0;
	uintptr_t gameState = 0;
	uintptr_t playerArray = 0;
	uintptr_t anchorSlot = 0;
	int       playerCount = 0;
	int       localTeam = -1;
	uint8_t   aspectAxis = 0;
	bool      proven = false;
	bool      valid = false;
};

struct ResolverOptions {
	bool   useKnownRva = true;
	size_t fallbackStepBytes = 1024 * 1024;  // scan budget per PumpFallback call
	unsigned retryBackoffInitialMs = 500;
	unsigned retryBackoffMaxMs = 5000;
};

struct ResolverDiagnostics {
	uintptr_t anchorSlot = 0;
	uintptr_t world = 0;
	uintptr_t namesPool = 0;
	int       candidates = 0;
	uint32_t  lastScanMs = 0;
	uint32_t  totalScanMs = 0;
	int       fullScans = 0;
	int       reanchors = 0;
	int       rescues = 0;
	uint32_t  readFailures = 0;
	// Chain-probe failure histogram for the most recent scan passes (useful
	// when a build changes the offset chain).
	int probeNoGameInstance = 0;
	int probeNoLocalPlayer = 0;
	int probeNoPlayerController = 0;
	int probeNoCameraManager = 0;
	bool      worldFromRva = false;
	bool      namesFromRva = false;
	bool      fallbackActive = false;
	int       failStreak = 0;
	int       recoverLimit = Offsets::Scan::RecoverLimitMin;
};

class WorldResolver {
public:
	WorldResolver(const ReadOnlyMemory& memory, NamePool& names, ResolverOptions options = {});

	// Fast path. Validates and updates the cached anchor; performs no scanning.
	// Safe to call every tick.
	bool Resolve();

	// Bounded fallback work, called when Resolve() fails. Advances at most one
	// scanner by options.fallbackStepBytes. `nowMs` is a monotonic millisecond
	// clock supplied by the caller (deterministic in tests).
	bool PumpFallback(uint64_t nowMs);

	// Re-arms recovery after a map transition and clears nothing else.
	void OnMapTransition();

	// Known build mismatch: fail closed. Fallback work stops permanently.
	void MarkOffsetsInvalid();

	[[nodiscard]] const WorldContext& context() const { return context_; }
	[[nodiscard]] ResolveStage stage() const { return stage_; }
	[[nodiscard]] const ResolverDiagnostics& diagnostics() const { return diagnostics_; }
	[[nodiscard]] bool offsetsInvalid() const { return offsetsInvalid_; }
	[[nodiscard]] NamePool& names() { return names_; }

private:
	struct ChainProbe {
		uintptr_t gameInstance = 0;
		uintptr_t localPlayer = 0;
		uintptr_t playerController = 0;
		uintptr_t cameraManager = 0;
		uintptr_t playerState = 0;
		uintptr_t outerWorld = 0;
		uint8_t   aspectAxis = 0;
		ResolveStage stage = ResolveStage::Ok;
	};

	struct RosterView {
		uintptr_t gameState = 0;
		uintptr_t data = 0;
		int       count = 0;
		bool      ok = false;
	};

	// Counting read helpers: every guarded read in the resolver funnels here
	// so the read-failure diagnostic stays accurate.
	bool ReadPointer(uintptr_t address, uintptr_t& out);
	bool ReadInt32(uintptr_t address, int32_t& out);
	bool ReadUInt8(uintptr_t address, uint8_t& out);

	bool SeedUsable(uintptr_t seed);
	bool ProbeFromSeed(uintptr_t seed, ChainProbe& probe);
	RosterView ReadRoster(uintptr_t world);
	bool RosterContains(const RosterView& roster, uintptr_t playerState);
	void TryKnownNames();

	uintptr_t KnownWorldSlot() const;
	bool TryResolveSeed(uintptr_t seed, WorldContext& context, ResolveStage& stage);

	void NoteWorldHealthy();
	void NoteWorldUnhealthy(uint64_t nowMs);
	void ResetBackoff();

	// Scanner callbacks.
	static bool NameScanChunk(void* context, uintptr_t address, const uint8_t* data, size_t size);
	static bool WorldScanChunk(void* context, uintptr_t address, const uint8_t* data, size_t size);
	static bool ReanchorScanChunk(void* context, uintptr_t address, const uint8_t* data, size_t size);

	const ReadOnlyMemory& memory_;
	NamePool&             names_;
	ResolverOptions       options_;

	WorldContext context_;
	ResolveStage stage_ = ResolveStage::NoModule;
	ResolverDiagnostics diagnostics_;
	bool offsetsInvalid_ = false;

	// Anchor state.
	uintptr_t anchorSlot_ = 0;
	uintptr_t directWorld_ = 0;
	bool      anchorFromRva_ = false;

	// Fallback scan state.
	ModuleScanner nameScanner_;
	ModuleScanner::Cursor nameCursor_;
	bool namesScanExhausted_ = false;
	bool namesKnownTried_ = false;
	uintptr_t foundNamePool_ = 0;
	uint64_t nextNameScanMs_ = 0;

	ModuleScanner worldScanner_;
	ModuleScanner::Cursor worldCursor_;
	bool worldScanExhausted_ = false;
	uint64_t nextWorldScanMs_ = 0;
	unsigned worldBackoffMs_ = 0;
	uintptr_t worldScanBestSlot_ = 0;
	uintptr_t worldScanBestWorld_ = 0;
	int       worldScanBestTier_ = 0;
	int       worldScanCandidates_ = 0;

	// Re-anchor scan state (rescue via ULevel::OwningWorld).
	ModuleScanner reanchorScanner_;
	ModuleScanner::Cursor reanchorCursor_;
	uintptr_t reanchorTargetWorld_ = 0;
	uintptr_t foundReanchorSlot_ = 0;
	uint64_t nextReanchorMs_ = 0;

	// World health streak used to throttle recovery after repeated failures.
	bool forceWorldRescan_ = false;
	int failStreak_ = 0;
	int recoverLimit_ = Offsets::Scan::RecoverLimitMin;
};

} // namespace mythos
