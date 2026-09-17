// ============================================================================
// AimController — QUARANTINED aim-assist tick.
//
// Runs on the worker after each capture: optionally engages while the aim key
// (RMB) is held or the fire button is pressed (soft aim), selects the target
// with the pure core selector, computes the per-tick step and applies it
// through EngineCalls. Roll is never touched.
//
// This is one of the only MYTHOS.dll modules allowed to call engine functions
// or write game memory.
// ============================================================================
#pragma once
#include "EngineCalls.hpp"

#include "mythos/Aim.hpp"
#include "mythos/Config.hpp"
#include "mythos/GameSnapshot.hpp"
#include "mythos/Projection.hpp"
#include "mythos/ReadOnlyMemory.hpp"
#include "mythos/WorldResolver.hpp"

#include <cstdint>
#include <string>

namespace mythos_host {

struct AimTelemetry {
	bool          on = false;
	bool          hasTarget = false;
	mythos::FVector targetWorld;
	mythos::Vec2d   targetScreen;
	double        crosshairPixels = 0.0;
	double        stepYaw = 0.0;
	double        stepPitch = 0.0;
	std::string   status = "Aimbot: off";
};

class AimController {
public:
	AimController(const mythos::ReadOnlyMemory& memory, EngineCalls& calls)
		: memory_(memory), calls_(calls) {}

	void Tick(const mythos::WorldContext& world, const mythos::GameSnapshot& snapshot,
	          const mythos::AimConfig& config, const mythos::ProjectionSettings& projection,
	          float screenWidth, float screenHeight);

	[[nodiscard]] const AimTelemetry& telemetry() const { return telemetry_; }

private:
	[[nodiscard]] bool ReadControlRotation(uintptr_t playerController,
	                                       mythos::FRotator& rotation) const;

	const mythos::ReadOnlyMemory& memory_;
	EngineCalls& calls_;
	AimTelemetry telemetry_;
};

} // namespace mythos_host
