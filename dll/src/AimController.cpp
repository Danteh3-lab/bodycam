#include "AimController.hpp"

#include "Offsets.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nova_host {

void AimController::Tick(const nova::WorldContext& world, const nova::GameSnapshot& snapshot,
                         const nova::AimConfig& config, const nova::ProjectionSettings& projection,
                         float screenWidth, float screenHeight) {
	telemetry_.on = false;
	telemetry_.hasTarget = false;

	const bool holdingAim = (GetAsyncKeyState(Offsets::Keys::AimDefault) & 0x8000) != 0;
	const bool firing = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	const bool aimbotActive = config.enabled && holdingAim;
	const bool softActive = config.softAim && firing;

	if (!config.enabled && !config.softAim) {
		telemetry_.status = "Aimbot: off";
		return;
	}
	if (!aimbotActive && !softActive) {
		telemetry_.status = config.softAim ? "Ready (soft aim on fire / hold RMB)"
		                                   : "Ready (hold RMB)";
		return;
	}
	telemetry_.on = true;

	if (!world.valid || world.playerController == 0 || !snapshot.valid ||
	    !snapshot.camera.valid) {
		telemetry_.status = "Aimbot: no local controller";
		return;
	}
	if (!(screenWidth > 0.0f) || !(screenHeight > 0.0f)) {
		telemetry_.status = "Aimbot: no viewport yet";
		return;
	}

	const bool useSoft = softActive;
	const double fovPixels = static_cast<double>(useSoft ? config.softFov : config.fov);
	const double smoothing = (std::max)(1.0, static_cast<double>(useSoft ? config.softSmooth
	                                                                     : config.smooth));
	const int boneMode = useSoft ? (config.softHeadOnly ? 0 : config.boneMode) : config.boneMode;

	nova::AimSelectionSettings selection;
	selection.fovPixels = fovPixels;
	selection.boneMode = boneMode;
	selection.visibleOnly = config.visibleOnly;
	selection.ignoreTeam = config.ignoreTeam;

	nova::AimTarget target;
	if (!nova::SelectAimTarget(snapshot, projection, screenWidth, screenHeight, selection,
	                           target)) {
		telemetry_.status = "Aimbot: no target";
		return;
	}
	telemetry_.hasTarget = true;
	telemetry_.targetWorld = target.world;
	telemetry_.targetScreen = target.screen;
	telemetry_.crosshairPixels = target.crosshairPixels;

	nova::FRotator current;
	if (!ReadControlRotation(world.playerController, current)) {
		telemetry_.status = "Aimbot: read ControlRotation failed";
		return;
	}

	const nova::FRotator want = nova::CalcAngle(snapshot.camera.location, target.world);
	const double stepCap = useSoft ? 180.0 : static_cast<double>(config.maxStep);
	const nova::FRotator step = nova::ComputeAimStep(current, want, smoothing, stepCap);
	telemetry_.stepYaw = step.yaw;
	telemetry_.stepPitch = step.pitch;

	bool applied = false;
	const char* how = "?";

	// Every method (including the direct writes) executes on the game thread.
	if (!calls_.gameThreadVerified()) {
		telemetry_.status = "Aimbot: game-thread path unavailable";
		return;
	}

	switch (config.method) {
	case 1:
		applied = calls_.AddLookInputDirect(world.playerController, step.yaw, step.pitch, stepCap);
		how = "RotationInput";
		break;
	case 2: {
		// Same capped step as the other methods: the legacy write must never
		// be able to spin the view.
		nova::FRotator capped;
		capped.pitch = current.pitch + step.pitch;
		capped.yaw = current.yaw + step.yaw;
		capped.roll = 0.0;
		applied = calls_.SetControlRotation(world.playerController, capped);
		how = "ControlRotation (legacy, capped)";
		break;
	}
	case 0:
	default:
		if (!calls_.engineCallsEnabled()) {
			telemetry_.status = "Aimbot: engine calls disabled (enable in the Aim section)";
			return;
		}
		if (!calls_.ready()) {
			telemetry_.status = "Aimbot: input functions unresolved";
			return;
		}
		if (!calls_.enginePathVerified()) {
			telemetry_.status = "Aimbot: game-thread path unavailable";
			return;
		}
		applied = calls_.AddLookInput(world.playerController, step.yaw, step.pitch, stepCap);
		how = "AddYawInput/AddPitchInput";
		break;
	}

	if (!applied) {
		telemetry_.status = std::string("Aimbot: apply failed via ") + how;
		return;
	}

	char buffer[192] = {};
	std::snprintf(buffer, sizeof(buffer),
	              "Aimbot: on target | %.0f px | dYaw %.2f dPitch %.2f | %s",
	              target.crosshairPixels, step.yaw, step.pitch, how);
	telemetry_.status = buffer;
}

bool AimController::ReadControlRotation(uintptr_t playerController,
                                        nova::FRotator& rotation) const {
	rotation = nova::FRotator{};
	if (!nova::IsPlausiblePointer(playerController)) return false;
	if (!memory_.readRaw<nova::FRotator>(
	        playerController + Offsets::Aim::ControlRotation, rotation)) {
		return false;
	}
	return rotation.finite();
}

} // namespace nova_host
