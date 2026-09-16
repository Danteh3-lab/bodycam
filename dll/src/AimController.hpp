// ============================================================================
// AimController — QUARANTINED aim-assist tick.
//
// Runs on the worker after each capture: optionally engages while the aim key
// (RMB) is held or the fire button is pressed (soft aim), selects the target
// with the pure core selector, computes the per-tick step and applies it
// through EngineCalls. Roll is never touched.
//
// This is one of the only NOVA.dll modules allowed to call engine functions
// or write game memory.
// ============================================================================
#pragma once
#include "EngineCalls.hpp"

#include "nova/Aim.hpp"
#include "nova/Config.hpp"
#include "nova/GameSnapshot.hpp"
#include "nova/Projection.hpp"
#include "nova/ReadOnlyMemory.hpp"
#include "nova/WorldResolver.hpp"

#include <cstdint>
#include <string>

namespace nova_host {

struct AimTelemetry {
	bool          on = false;
	bool          hasTarget = false;
	nova::FVector targetWorld;
	nova::Vec2d   targetScreen;
	double        crosshairPixels = 0.0;
	double        stepYaw = 0.0;
	double        stepPitch = 0.0;
	std::string   status = "Aimbot: off";
};

class AimController {
public:
	AimController(const nova::ReadOnlyMemory& memory, EngineCalls& calls)
		: memory_(memory), calls_(calls) {}

	void Tick(const nova::WorldContext& world, const nova::GameSnapshot& snapshot,
	          const nova::AimConfig& config, const nova::ProjectionSettings& projection,
	          float screenWidth, float screenHeight);

	[[nodiscard]] const AimTelemetry& telemetry() const { return telemetry_; }

private:
	[[nodiscard]] bool ReadControlRotation(uintptr_t playerController,
	                                       nova::FRotator& rotation) const;

	const nova::ReadOnlyMemory& memory_;
	EngineCalls& calls_;
	AimTelemetry telemetry_;
};

} // namespace nova_host
