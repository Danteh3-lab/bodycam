#include "nova/Projection.hpp"

#include <cmath>

namespace nova {
namespace {

constexpr double kPi = 3.14159265358979323846;

double DegreesToRadians(double degrees) {
	return degrees * kPi / 180.0;
}

} // namespace

void BuildCameraBasis(const FRotator& rotation, FVector& forward, FVector& right, FVector& up) {
	const double radPitch = DegreesToRadians(rotation.pitch);
	const double radYaw = DegreesToRadians(rotation.yaw);
	const double radRoll = DegreesToRadians(rotation.roll);

	const double sp = std::sin(radPitch);
	const double cp = std::cos(radPitch);
	const double sy = std::sin(radYaw);
	const double cy = std::cos(radYaw);
	const double sr = std::sin(radRoll);
	const double cr = std::cos(radRoll);

	// Row 0: forward.
	forward = FVector{ cp * cy, cp * sy, sp };
	// Row 1: right.
	right = FVector{ sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp };
	// Row 2: up.
	up = FVector{ -(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp };
}

ProjectionMultipliers ComputeProjectionMultipliers(const CameraView& view,
                                                   const ProjectionSettings& settings,
                                                   float screenWidth,
                                                   float screenHeight) {
	ProjectionMultipliers multipliers;
	if (screenWidth <= 0.0f || screenHeight <= 0.0f) return multipliers;

	const bool forced = settings.axisOverride >= 0 && settings.axisOverride <= 2;

	if (!forced && view.constrainAspect && view.aspectRatio > 0.01f && view.aspectRatio < 100.0f) {
		multipliers.x = 1.0;
		multipliers.y = static_cast<double>(view.aspectRatio);
		return multipliers;
	}

	int axis = view.axisConstraint;
	if (forced) axis = settings.axisOverride;

	const bool wide = screenWidth > screenHeight;
	if ((wide && axis == static_cast<int>(AspectAxis::MajorAxisFOV)) ||
	    axis == static_cast<int>(AspectAxis::MaintainXFOV)) {
		multipliers.x = 1.0;
		multipliers.y = static_cast<double>(screenWidth) / static_cast<double>(screenHeight);
	} else {
		multipliers.x = static_cast<double>(screenHeight) / static_cast<double>(screenWidth);
		multipliers.y = 1.0;
	}
	return multipliers;
}

bool ProjectWorldToScreen(const CameraView& view,
                          const ProjectionSettings& settings,
                          float screenWidth,
                          float screenHeight,
                          const FVector& world,
                          Vec2d& out) {
	out = Vec2d{};
	if (!view.valid || !view.finite()) return false;
	if (screenWidth <= 0.0f || screenHeight <= 0.0f) return false;
	if (!world.finite()) return false;

	FVector forward, right, up;
	BuildCameraBasis(view.rotation, forward, right, up);

	const FVector delta = world - view.location;
	const double depth = delta.dot(forward);
	if (depth <= 1.0) return false;

	double tanHalf = std::tan(static_cast<double>(view.fov) * kPi / 360.0);
	if (tanHalf <= 1e-6) return false;
	if (settings.fovScale > 0.05) tanHalf /= settings.fovScale;

	const ProjectionMultipliers multipliers =
		ComputeProjectionMultipliers(view, settings, screenWidth, screenHeight);

	const double width = static_cast<double>(screenWidth);
	const double height = static_cast<double>(screenHeight);
	const double sx = width * 0.5 + delta.dot(right) / depth * (multipliers.x / tanHalf) * (width * 0.5);
	const double sy = height * 0.5 - delta.dot(up) / depth * (multipliers.y / tanHalf) * (height * 0.5);

	Vec2d result{ sx, sy };
	if (!result.finite()) return false;
	out = result;
	return true;
}

} // namespace nova
