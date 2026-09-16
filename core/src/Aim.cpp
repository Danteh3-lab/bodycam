#include "nova/Aim.hpp"

#include <cmath>

namespace nova {
namespace {

constexpr double kPi = 3.14159265358979323846;

FVector AimPoint(const PlayerSnapshot& player, int boneMode, bool& ok) {
	const bool hasHead = player.hasPose && player.headBone >= 0 &&
	                     player.headBone < static_cast<int>(player.bones.size());

	if (boneMode == 0 && hasHead) {
		ok = true;
		return player.bones[static_cast<std::size_t>(player.headBone)].world;
	}

	if (player.hasPose && !player.bones.empty()) {
		FVector point = player.bones[0].world;
		if (hasHead) {
			point.z = (point.z + player.bones[static_cast<std::size_t>(player.headBone)].world.z) * 0.5;
		}
		ok = true;
		return point;
	}

	if (player.hasRoot) {
		ok = true;
		return player.root;
	}

	ok = false;
	return FVector{};
}

} // namespace

FRotator CalcAngle(const FVector& source, const FVector& target) {
	const FVector delta = target - source;
	const double horizontal = std::sqrt(delta.x * delta.x + delta.y * delta.y);

	FRotator angle;
	angle.pitch = std::atan2(delta.z, horizontal) * (180.0 / kPi);
	angle.yaw = std::atan2(delta.y, delta.x) * (180.0 / kPi);
	angle.roll = 0.0;
	return angle;
}

double NormalizeAngle(double angle) {
	while (angle > 180.0) angle -= 360.0;
	while (angle < -180.0) angle += 360.0;
	return angle;
}

FRotator ComputeAimStep(const FRotator& current, const FRotator& target,
                        double smoothing, double maxStep) {
	if (!(smoothing >= 1.0)) smoothing = 1.0;
	if (!(maxStep > 0.0)) maxStep = 0.0;

	double stepYaw = NormalizeAngle(target.yaw - current.yaw) / smoothing;
	double stepPitch = NormalizeAngle(target.pitch - current.pitch) / smoothing;

	if (stepYaw > maxStep) stepYaw = maxStep;
	if (stepYaw < -maxStep) stepYaw = -maxStep;
	if (stepPitch > maxStep) stepPitch = maxStep;
	if (stepPitch < -maxStep) stepPitch = -maxStep;

	FRotator step;
	step.pitch = stepPitch;
	step.yaw = stepYaw;
	step.roll = 0.0;
	return step;
}

FRotator SmoothRotation(const FRotator& current, const FRotator& target, float smoothing) {
	if (!(smoothing >= 1.0f)) smoothing = 1.0f;

	FRotator result;
	result.pitch = current.pitch + NormalizeAngle(target.pitch - current.pitch) / smoothing;
	result.yaw = current.yaw + NormalizeAngle(target.yaw - current.yaw) / smoothing;
	result.roll = 0.0;
	return result;
}

bool SelectAimTarget(const GameSnapshot& snapshot,
                     const ProjectionSettings& projection,
                     float screenWidth,
                     float screenHeight,
                     const AimSelectionSettings& settings,
                     AimTarget& out) {
	if (!snapshot.valid || !snapshot.camera.valid) return false;
	if (!(screenWidth > 0.0f) || !(screenHeight > 0.0f)) return false;
	if (!(settings.fovPixels > 0.0)) return false;

	const double centerX = static_cast<double>(screenWidth) * 0.5;
	const double centerY = static_cast<double>(screenHeight) * 0.5;
	double bestDistance = settings.fovPixels;
	bool found = false;

	for (std::size_t i = 0; i < snapshot.players.size(); ++i) {
		const PlayerSnapshot& player = snapshot.players[i];
		if (player.isSelf || player.isDrone()) continue;
		if (player.hasHealth && player.dead) continue;
		if (settings.ignoreTeam && player.sameTeam) continue;
		if (settings.visibleOnly && !player.visible) continue;

		bool hasPoint = false;
		const FVector point = AimPoint(player, settings.boneMode, hasPoint);
		if (!hasPoint) continue;

		Vec2d screen;
		if (!ProjectWorldToScreen(snapshot.camera, projection, screenWidth, screenHeight,
		                          point, screen)) {
			continue;
		}
		if (!IsOnScreen(screen, screenWidth, screenHeight)) continue;

		const double dx = screen.x - centerX;
		const double dy = screen.y - centerY;
		const double distance = std::sqrt(dx * dx + dy * dy);
		if (distance > bestDistance) continue;

		bestDistance = distance;
		out.playerIndex = i;
		out.world = point;
		out.screen = screen;
		out.crosshairPixels = distance;
		found = true;
	}

	return found;
}

} // namespace nova
