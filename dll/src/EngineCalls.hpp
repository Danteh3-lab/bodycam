// ============================================================================
// EngineCalls — QUARANTINED engine interaction.
//
// Resolves the game's own AddYawInput/AddPitchInput functions (known RVA,
// verified by prologue + tail signature; bounded executable-section scan as
// the patch-surviving fallback) and invokes them with SEH protection. The
// direct RotationInput/ControlRotation writes are the guarded fallbacks.
//
// This is one of the only NOVA.dll modules allowed to call engine functions
// or write game memory. nova_core never includes or consumes it.
// ============================================================================
#pragma once
#include "GameThread.hpp"

#include "nova/ReadOnlyMemory.hpp"
#include "nova/Scan.hpp"
#include "nova/UnrealTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace nova_host {

class EngineCalls {
public:
	struct Status {
		bool        attempted = false;
		bool        ok = false;
		bool        foundByScan = false;
		uint64_t    rvaPitch = 0;
		uint64_t    rvaYaw = 0;
		uint32_t    scanMs = 0;
		double      yawScale = 1.0;
		double      pitchScale = 1.0;
		bool        yawCalibrated = false;
		bool        pitchCalibrated = false;
		// Availability, kept as separate facts so the UI never recommends an
		// unavailable method: resolved functions, verified game-thread path,
		// owner opt-in, and the combination the engine method actually needs.
		bool        functionsResolved = false;
		bool        gameThreadAvailable = false;
		bool        engineCallsEnabled = false;
		bool        engineMethodAvailable = false;
		std::string enginePath = "game-thread path not initialized";
		std::string message = "not resolved yet";
	};

	EngineCalls(const nova::ReadOnlyMemory& memory, GameThreadExecutor& gameThread);

	// Owner opt-in for engine function calls. Off by default: calling engine
	// code can re-enter it at an unsafe phase.
	void SetEngineCallsEnabled(bool enabled) { engineCallsEnabled_ = enabled; }
	[[nodiscard]] bool engineCallsEnabled() const { return engineCallsEnabled_; }

	// Tries the known RVAs first, then advances a bounded executable-section
	// scan. Call once per worker tick until ready().
	void Resolve(std::size_t scanBudgetBytes);

	// Refreshes the engine-path fields in Status (verified + resolved).
	void RefreshStatus();

	[[nodiscard]] bool ready() const { return addPitch_ != nullptr && addYaw_ != nullptr; }
	// True only when the functions are resolved AND the game-thread execution
	// path is verified AND the owner opted in; the engine method must not run
	// otherwise.
	[[nodiscard]] bool enginePathVerified() const {
		return engineCallsEnabled_ && ready() && gameThread_.verified();
	}
	// Direct rotation writes are queued on the game thread too; without the
	// verified path they are unavailable as well.
	[[nodiscard]] bool gameThreadVerified() const { return gameThread_.verified(); }
	[[nodiscard]] const Status& status() const { return status_; }
	[[nodiscard]] double yawScale() const { return yawScale_; }
	[[nodiscard]] double pitchScale() const { return pitchScale_; }
	[[nodiscard]] bool yawCalibrated() const { return yawCalibrated_; }
	[[nodiscard]] bool pitchCalibrated() const { return pitchCalibrated_; }

	// Applies a view delta in degrees through the engine functions, clamped to
	// maxStep and mapped through the measured input scale.
	bool AddLookInput(uintptr_t playerController, double deltaYaw, double deltaPitch,
	                  double maxStep);

	// Same delta written straight into RotationInput (no engine call).
	bool AddLookInputDirect(uintptr_t playerController, double deltaYaw, double deltaPitch,
	                        double maxStep);

	// Legacy method: overwrite ControlRotation directly.
	bool SetControlRotation(uintptr_t playerController, const nova::FRotator& rotation);

private:
	using AddInputFn = void(__fastcall*)(void* playerController, float value);

	static bool ScanChunk(void* context, uintptr_t address, const uint8_t* data, size_t size);
	[[nodiscard]] bool VerifyAddInput(uintptr_t function, uint32_t wantOffset) const;
	void TryKnownRvas();
	void Adopt(uintptr_t pitch, uintptr_t yaw, bool foundByScan);
	void ProbeScale(uintptr_t playerController, bool yaw);
	void CalibrateFrom(double before, double after, float sent, double& scale, bool& calibrated);
	void SyncStatus();
	bool InvokeOnGameThread(AddInputFn function, uintptr_t playerController, float value);

	const nova::ReadOnlyMemory& memory_;
	GameThreadExecutor& gameThread_;
	nova::ModuleScanner scanner_;
	nova::ModuleScanner::Cursor scanCursor_;
	bool scanStarted_ = false;
	uintptr_t pitchAddress_ = 0;
	uintptr_t yawAddress_ = 0;
	AddInputFn addPitch_ = nullptr;
	AddInputFn addYaw_ = nullptr;
	double yawScale_ = 1.0;
	double pitchScale_ = 1.0;
	bool yawCalibrated_ = false;
	bool pitchCalibrated_ = false;
	bool engineCallsEnabled_ = false;
	Status status_;
};

} // namespace nova_host
