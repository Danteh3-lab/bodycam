#include "nova/WorldResolver.hpp"

#include "nova/Logging.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace nova {
namespace {

uint32_t ElapsedMs(std::chrono::steady_clock::time_point start) {
	const auto elapsed = std::chrono::steady_clock::now() - start;
	return static_cast<uint32_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

} // namespace

const char* ResolveStageName(ResolveStage stage) {
	switch (stage) {
	case ResolveStage::Ok: return "OK";
	case ResolveStage::NoModule: return "Game module not mapped";
	case ResolveStage::NoGWorld: return "World anchor not found";
	case ResolveStage::NoWorld: return "UWorld not loaded (main menu?)";
	case ResolveStage::NoGameInstance: return "UWorld->OwningGameInstance failed";
	case ResolveStage::NoLocalPlayer: return "GameInstance->LocalPlayers[0] failed";
	case ResolveStage::NoPlayerController: return "LocalPlayer->PlayerController failed";
	case ResolveStage::NoCameraManager: return "PlayerController->PlayerCameraManager failed";
	case ResolveStage::NoGameState: return "UWorld->GameState failed";
	case ResolveStage::NoPlayerArray: return "GameState->PlayerArray failed";
	case ResolveStage::NoLocalPawn: return "No local pawn (dead / spectating)";
	case ResolveStage::WorldUnproven: return "World not confirmed yet (joining?)";
	case ResolveStage::OffsetsInvalid: return "Offsets invalid for this build";
	}
	return "Unknown";
}

WorldResolver::WorldResolver(const ReadOnlyMemory& memory, NamePool& names, ResolverOptions options)
	: memory_(memory),
	  names_(names),
	  options_(options),
	  nameScanner_(memory, true),
	  worldScanner_(memory, false),
	  reanchorScanner_(memory, false) {
	anchorSlot_ = KnownWorldSlot();
	anchorFromRva_ = anchorSlot_ != 0;
	diagnostics_.anchorSlot = anchorSlot_;
	recoverLimit_ = Offsets::Scan::RecoverLimitMin;
	diagnostics_.recoverLimit = recoverLimit_;
}

uintptr_t WorldResolver::KnownWorldSlot() const {
	if (!options_.useKnownRva) return 0;
	const ModuleInfo module = memory_.module();
	if (!module.valid()) return 0;
	return module.base + Offsets::Globals::GWorld;
}

bool WorldResolver::ReadPointer(uintptr_t address, uintptr_t& out) {
	out = 0;
	if (!memory_.readPointer(address, out)) {
		++diagnostics_.readFailures;
		return false;
	}
	return true;
}

bool WorldResolver::ReadInt32(uintptr_t address, int32_t& out) {
	out = 0;
	if (!memory_.readValue<int32_t>(address, out)) {
		++diagnostics_.readFailures;
		return false;
	}
	return true;
}

bool WorldResolver::ReadUInt8(uintptr_t address, uint8_t& out) {
	out = 0;
	if (!memory_.readValue<uint8_t>(address, out)) {
		++diagnostics_.readFailures;
		return false;
	}
	return true;
}

bool WorldResolver::SeedUsable(uintptr_t seed) {
	if (!IsPlausiblePointer(seed) || (seed & 7) != 0) return false;
	uintptr_t gameInstance = 0;
	if (!ReadPointer(seed + Offsets::GameInstance, gameInstance)) return false;
	ArrayView localPlayers =
		ReadArrayView(memory_, gameInstance + Offsets::LocalPlayer, Offsets::Limits::MaxLocalPlayers);
	return localPlayers.valid() && localPlayers.count >= 1;
}

bool WorldResolver::ProbeFromSeed(uintptr_t seed, ChainProbe& probe) {
	probe = ChainProbe{};
	if (!IsPlausiblePointer(seed) || (seed & 7) != 0) {
		probe.stage = ResolveStage::NoWorld;
		return false;
	}

	if (!ReadPointer(seed + Offsets::GameInstance, probe.gameInstance)) {
		probe.stage = ResolveStage::NoGameInstance;
		return false;
	}

	const ArrayView localPlayers =
		ReadArrayView(memory_, probe.gameInstance + Offsets::LocalPlayer, Offsets::Limits::MaxLocalPlayers);
	if (!localPlayers.valid() || localPlayers.count < 1) {
		probe.stage = ResolveStage::NoLocalPlayer;
		return false;
	}
	if (!ReadArrayElement(memory_, localPlayers, 0, probe.localPlayer)) {
		probe.stage = ResolveStage::NoLocalPlayer;
		return false;
	}

	(void)ReadUInt8(probe.localPlayer + Offsets::AspectAxisConstraint, probe.aspectAxis);

	if (!ReadPointer(probe.localPlayer + Offsets::LPPlayerController, probe.playerController)) {
		probe.stage = ResolveStage::NoPlayerController;
		return false;
	}
	if (!ReadPointer(probe.playerController + Offsets::CameraManager, probe.cameraManager)) {
		probe.stage = ResolveStage::NoCameraManager;
		return false;
	}

	(void)ReadPointer(probe.playerController + Offsets::ACPlayerState, probe.playerState);

	uintptr_t level = 0;
	if (ReadPointer(probe.playerController + Offsets::UObject::Outer, level)) {
		(void)ReadPointer(level + Offsets::LevelOwningWorld, probe.outerWorld);
	}

	probe.stage = ResolveStage::Ok;
	return true;
}

WorldResolver::RosterView WorldResolver::ReadRoster(uintptr_t world) {
	RosterView roster;
	uintptr_t gameState = 0;
	if (!ReadPointer(world + Offsets::GameState, gameState)) return roster;
	roster.gameState = gameState;

	const ArrayView playerArray =
		ReadArrayView(memory_, gameState + Offsets::PlayerArray, Offsets::Limits::MaxPlayers);
	if (!playerArray.valid()) return roster;

	roster.data = playerArray.data;
	roster.count = playerArray.count;
	roster.ok = true;
	return roster;
}

bool WorldResolver::RosterContains(const RosterView& roster, uintptr_t playerState) {
	if (!roster.ok || !playerState || roster.data == 0) return false;
	for (int i = 0; i < roster.count; ++i) {
		uintptr_t element = 0;
		if (!ReadArrayElement(memory_, ArrayView{ roster.data, roster.count, roster.count, true },
		                      i, element)) {
			continue;
		}
		if (element == playerState) return true;
	}
	return false;
}

bool WorldResolver::TryResolveSeed(uintptr_t seed, WorldContext& context, ResolveStage& stage) {
	ChainProbe probe;
	if (!ProbeFromSeed(seed, probe)) {
		context = WorldContext{};
		context.world = seed;
		context.gameInstance = probe.gameInstance;
		context.localPlayer = probe.localPlayer;
		stage = probe.stage;
		return false;
	}

	uintptr_t chosen = seed;
	bool proven = false;

	RosterView roster = ReadRoster(seed);
	if (probe.playerState && RosterContains(roster, probe.playerState)) {
		proven = true;
	} else if (probe.outerWorld && probe.outerWorld != seed && IsPlausiblePointer(probe.outerWorld)) {
		const RosterView alternate = ReadRoster(probe.outerWorld);
		if (probe.playerState && RosterContains(alternate, probe.playerState)) {
			chosen = probe.outerWorld;
			roster = alternate;
			proven = true;
			++diagnostics_.rescues;
			reanchorTargetWorld_ = chosen;
			reanchorCursor_ = ModuleScanner::Cursor{};
			reanchorScanner_.Refresh();
			foundReanchorSlot_ = 0;
			nextReanchorMs_ = 0;
			directWorld_ = chosen;
		}
	}

	context = WorldContext{};
	context.world = chosen;
	context.anchorSlot = anchorSlot_;
	context.gameInstance = probe.gameInstance;
	context.localPlayer = probe.localPlayer;
	context.playerController = probe.playerController;
	context.cameraManager = probe.cameraManager;
	context.playerState = probe.playerState;
	context.aspectAxis = probe.aspectAxis;
	context.proven = proven;

	if (!roster.ok) {
		stage = roster.gameState != 0 ? ResolveStage::NoPlayerArray : ResolveStage::NoGameState;
		return false;
	}

	context.gameState = roster.gameState;
	context.playerArray = roster.data;
	context.playerCount = roster.count;

	int32_t localTeam = -1;
	if (probe.playerState != 0 && ReadInt32(probe.playerState + Offsets::PSTeamId, localTeam)) {
		context.localTeam = localTeam;
	}

	uintptr_t acknowledgedPawn = 0;
	if (ReadPointer(probe.playerController + Offsets::AcknowledgedPawn, acknowledgedPawn)) {
		context.acknowledgedPawn = acknowledgedPawn;
		uintptr_t attributeSet = 0;
		if (ReadPointer(acknowledgedPawn + Offsets::BCCharacterSet, attributeSet)) {
			context.attributeSet = attributeSet;
		}
	} else {
		stage = ResolveStage::NoLocalPawn;
		return false;
	}

	context.valid = true;
	stage = proven ? ResolveStage::Ok : ResolveStage::WorldUnproven;
	return true;
}

void WorldResolver::NoteWorldHealthy() {
	failStreak_ = 0;
	recoverLimit_ = Offsets::Scan::RecoverLimitMin;
	diagnostics_.failStreak = failStreak_;
	diagnostics_.recoverLimit = recoverLimit_;
}

void WorldResolver::NoteWorldUnhealthy(uint64_t nowMs) {
	(void)nowMs;
	failStreak_ = (std::min)(failStreak_ + 1, Offsets::Scan::RecoverLimitMax);
	diagnostics_.failStreak = failStreak_;
	if (failStreak_ < recoverLimit_) return;

	failStreak_ = 0;
	diagnostics_.failStreak = 0;
	forceWorldRescan_ = true;
	if (recoverLimit_ < Offsets::Scan::RecoverLimitMax) {
		recoverLimit_ *= 2;
		diagnostics_.recoverLimit = recoverLimit_;
	}
}

void WorldResolver::ResetBackoff() {
	worldBackoffMs_ = 0;
	nextWorldScanMs_ = 0;
	worldScanBestSlot_ = 0;
	worldScanBestWorld_ = 0;
	worldScanBestTier_ = 0;
}

void WorldResolver::OnMapTransition() {
	NoteWorldHealthy();
	failStreak_ = 0;
	diagnostics_.failStreak = 0;
	forceWorldRescan_ = false;
	// Keep the last known anchor: it is usually still valid, and validation
	// will fail closed if it is not.
}

void WorldResolver::MarkOffsetsInvalid() {
	offsetsInvalid_ = true;
	stage_ = ResolveStage::OffsetsInvalid;
	context_ = WorldContext{};
	diagnostics_.fallbackActive = false;
}

void WorldResolver::TryKnownNames() {
	if (namesKnownTried_) return;
	namesKnownTried_ = true;
	if (!options_.useKnownRva) return;

	const ModuleInfo module = memory_.module();
	if (!module.valid()) return;

	const uintptr_t hint = module.base + Offsets::Globals::GNames;
	if (NamePool::IsCertain(memory_, hint)) {
		names_.Attach(hint);
		diagnostics_.namesFromRva = true;
		diagnostics_.namesPool = hint;
		char buffer[160] = {};
		std::snprintf(buffer, sizeof(buffer),
		              "name pool attached via known RVA 0x%llX",
		              static_cast<unsigned long long>(Offsets::Globals::GNames));
		LogInfo(buffer);
	}
}

bool WorldResolver::Resolve() {
	if (offsetsInvalid_) {
		stage_ = ResolveStage::OffsetsInvalid;
		return false;
	}

	const ModuleInfo module = memory_.module();
	if (!module.valid()) {
		stage_ = ResolveStage::NoModule;
		context_ = WorldContext{};
		return false;
	}

	TryKnownNames();

	uintptr_t seed = 0;
	bool haveSeed = false;
	if (anchorSlot_ != 0) {
		uintptr_t value = 0;
		if (ReadPointer(anchorSlot_, value) && SeedUsable(value)) {
			seed = value;
			haveSeed = true;
		}
	}
	if (!haveSeed && directWorld_ != 0 && SeedUsable(directWorld_)) {
		seed = directWorld_;
		haveSeed = true;
	}

	if (!haveSeed) {
		// The anchor read failed or holds garbage: ask the fallback scanner for
		// a fresh pass. This does not perform any scanning itself.
		if (anchorSlot_ != 0 || directWorld_ != 0) {
			forceWorldRescan_ = true;
		}
		stage_ = ResolveStage::NoGWorld;
		context_ = WorldContext{};
		NoteWorldUnhealthy(0);
		return false;
	}

	WorldContext context;
	ResolveStage stage = ResolveStage::Ok;
	if (!TryResolveSeed(seed, context, stage)) {
		context_ = context;
		stage_ = stage;
		if (stage != ResolveStage::NoLocalPawn && stage != ResolveStage::NoPlayerArray) {
			NoteWorldUnhealthy(0);
		}
		return false;
	}

	context_ = context;
	context_.anchorSlot = anchorSlot_;
	stage_ = stage;
	diagnostics_.anchorSlot = anchorSlot_;
	diagnostics_.world = context.world;
	diagnostics_.worldFromRva = anchorFromRva_;
	NoteWorldHealthy();
	return true;
}

bool WorldResolver::PumpFallback(uint64_t nowMs) {
	if (offsetsInvalid_) return false;

	const ModuleInfo module = memory_.module();
	if (!module.valid()) return false;

	TryKnownNames();

	diagnostics_.fallbackActive = true;

	// ---- FNamePool fallback: bounded signature scan. ------------------------
	if (!names_.ready()) {
		if (!namesScanExhausted_) {
			if (nowMs >= nextNameScanMs_) {
				const auto start = std::chrono::steady_clock::now();
				const ModuleScanner::StepResult result =
					nameScanner_.Step(nameCursor_, options_.fallbackStepBytes, &NameScanChunk, this);
				const uint32_t elapsed = ElapsedMs(start);
				diagnostics_.lastScanMs = elapsed;
				diagnostics_.totalScanMs += elapsed;

				if (result == ModuleScanner::StepResult::Found && foundNamePool_ != 0) {
					names_.Attach(foundNamePool_);
					diagnostics_.namesFromRva = false;
					diagnostics_.namesPool = foundNamePool_;
					char buffer[160] = {};
					std::snprintf(buffer, sizeof(buffer),
					              "name pool attached via signature scan at 0x%llX (rva 0x%llX)",
					              static_cast<unsigned long long>(foundNamePool_),
					              static_cast<unsigned long long>(foundNamePool_ - module.base));
					LogInfo(buffer);
				} else if (result == ModuleScanner::StepResult::Exhausted) {
					namesScanExhausted_ = true;
					if (options_.useKnownRva &&
					    NamePool::IsPlausible(memory_, module.base + Offsets::Globals::GNames)) {
						const uintptr_t hint = module.base + Offsets::Globals::GNames;
						names_.Attach(hint);
						diagnostics_.namesFromRva = true;
						diagnostics_.namesPool = hint;
					}
					nextNameScanMs_ = nowMs + options_.retryBackoffMaxMs;
				}
			}
		} else if (nowMs >= nextNameScanMs_ && names_.poolAddress() == 0) {
			namesScanExhausted_ = false;
			nameCursor_ = ModuleScanner::Cursor{};
			nameScanner_.Refresh();
			nextNameScanMs_ = 0;
		}
	}

	// ---- World anchor fallback: bounded data-section scan. ------------------
	if (forceWorldRescan_) {
		// A rescan request must never restart a pass that is still in flight,
		// and it must respect the retry backoff once a pass has completed;
		// otherwise Resolve() failing every tick would reset the cursor before
		// the scanner ever reaches the end of the data sections.
		forceWorldRescan_ = false;
		if (worldScanExhausted_ && nowMs >= nextWorldScanMs_) {
			worldCursor_ = ModuleScanner::Cursor{};
			worldScanner_.Refresh();
			worldScanExhausted_ = false;
			worldScanBestSlot_ = 0;
			worldScanBestWorld_ = 0;
			worldScanBestTier_ = 0;
			nextWorldScanMs_ = 0;
		}
	}

	if (!worldScanExhausted_) {
		if (nowMs >= nextWorldScanMs_) {
			const auto start = std::chrono::steady_clock::now();
			const ModuleScanner::StepResult result =
				worldScanner_.Step(worldCursor_, options_.fallbackStepBytes, &WorldScanChunk, this);
			const uint32_t elapsed = ElapsedMs(start);
			diagnostics_.lastScanMs = elapsed;
			diagnostics_.totalScanMs += elapsed;

			if (result == ModuleScanner::StepResult::Found ||
			    result == ModuleScanner::StepResult::Exhausted) {
				worldScanExhausted_ = true;
				++diagnostics_.fullScans;
				diagnostics_.candidates += worldScanCandidates_;
				const int passCandidates = worldScanCandidates_;
				worldScanCandidates_ = 0;

				if (diagnostics_.fullScans <= 5 || diagnostics_.fullScans % 30 == 0) {
					char buffer[320] = {};
					std::snprintf(
						buffer, sizeof(buffer),
						"world scan pass #%d: candidates=%d bestTier=%d bestSlot=0x%llX "
						"| probe fails: gameInstance=%d localPlayer=%d controller=%d camera=%d",
						diagnostics_.fullScans, passCandidates, worldScanBestTier_,
						static_cast<unsigned long long>(worldScanBestSlot_),
						diagnostics_.probeNoGameInstance, diagnostics_.probeNoLocalPlayer,
						diagnostics_.probeNoPlayerController, diagnostics_.probeNoCameraManager);
					LogInfo(buffer);
				}

				if (worldScanBestSlot_ != 0) {
					anchorSlot_ = worldScanBestSlot_;
					directWorld_ = worldScanBestWorld_;
					anchorFromRva_ = false;
					diagnostics_.worldFromRva = false;
					ResetBackoff();
				} else {
					worldBackoffMs_ = worldBackoffMs_ == 0
						? options_.retryBackoffInitialMs
						: (std::min)(worldBackoffMs_ * 2, options_.retryBackoffMaxMs);
					nextWorldScanMs_ = nowMs + worldBackoffMs_;
				}
			}
		}
	} else if (worldScanBestSlot_ == 0 && !context_.valid && nowMs >= nextWorldScanMs_) {
		// Only keep sweeping while there is no valid world at all. Once the
		// chain resolves, further scan passes are unnecessary work.
		worldCursor_ = ModuleScanner::Cursor{};
		worldScanner_.Refresh();
		worldScanExhausted_ = false;
	}

	// ---- Re-anchor search after an outer-world rescue. ----------------------
	if (reanchorTargetWorld_ != 0 && nowMs >= nextReanchorMs_) {
		const ModuleScanner::StepResult result =
			reanchorScanner_.Step(reanchorCursor_, options_.fallbackStepBytes, &ReanchorScanChunk, this);
		if (result == ModuleScanner::StepResult::Found && foundReanchorSlot_ != 0) {
			anchorSlot_ = foundReanchorSlot_;
			anchorFromRva_ = false;
			directWorld_ = reanchorTargetWorld_;
			++diagnostics_.reanchors;
			diagnostics_.anchorSlot = anchorSlot_;
			reanchorTargetWorld_ = 0;
			foundReanchorSlot_ = 0;
		} else if (result == ModuleScanner::StepResult::Exhausted) {
			// Give up on the slot search; directWorld_ keeps the chain usable.
			reanchorTargetWorld_ = 0;
			foundReanchorSlot_ = 0;
		}
	}

	return Resolve();
}

bool WorldResolver::NameScanChunk(void* context, uintptr_t address, const uint8_t* data, size_t size) {
	auto* self = static_cast<WorldResolver*>(context);
	const uintptr_t pool = MatchNamePoolSignature(self->memory_, data, size, address);
	if (pool == 0) return false;
	self->foundNamePool_ = pool;
	return true;
}

bool WorldResolver::WorldScanChunk(void* context, uintptr_t address, const uint8_t* data, size_t size) {
	auto* self = static_cast<WorldResolver*>(context);
	if (size < sizeof(uintptr_t)) return false;

	const size_t count = size / sizeof(uintptr_t);
	for (size_t i = 0; i < count; ++i) {
		uintptr_t candidate = 0;
		std::memcpy(&candidate, data + i * sizeof(uintptr_t), sizeof(candidate));
		if (!IsPlausiblePointer(candidate)) continue;
		if ((candidate & 7) != 0) continue;
		if (!self->SeedUsable(candidate)) continue;

		++self->worldScanCandidates_;

		int tier = 1;
		ChainProbe probe;
		if (self->ProbeFromSeed(candidate, probe)) {
			tier = 2;
			if (probe.playerState != 0 &&
			    self->RosterContains(self->ReadRoster(candidate), probe.playerState)) {
				tier = 3;
			}
		} else {
			switch (probe.stage) {
			case ResolveStage::NoGameInstance: ++self->diagnostics_.probeNoGameInstance; break;
			case ResolveStage::NoLocalPlayer: ++self->diagnostics_.probeNoLocalPlayer; break;
			case ResolveStage::NoPlayerController:
				++self->diagnostics_.probeNoPlayerController;
				break;
			case ResolveStage::NoCameraManager: ++self->diagnostics_.probeNoCameraManager; break;
			default: break;
			}
		}

		if (tier > self->worldScanBestTier_) {
			self->worldScanBestTier_ = tier;
			self->worldScanBestSlot_ = address + i * sizeof(uintptr_t);
			self->worldScanBestWorld_ = candidate;
			if (tier >= 3) return true;
		}
	}
	return false;
}

bool WorldResolver::ReanchorScanChunk(void* context, uintptr_t address, const uint8_t* data, size_t size) {
	auto* self = static_cast<WorldResolver*>(context);
	if (size < sizeof(uintptr_t)) return false;

	const size_t count = size / sizeof(uintptr_t);
	for (size_t i = 0; i < count; ++i) {
		uintptr_t candidate = 0;
		std::memcpy(&candidate, data + i * sizeof(uintptr_t), sizeof(candidate));
		if (candidate == self->reanchorTargetWorld_) {
			self->foundReanchorSlot_ = address + i * sizeof(uintptr_t);
			return true;
		}
	}
	return false;
}

} // namespace nova
