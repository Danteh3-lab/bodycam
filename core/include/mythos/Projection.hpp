// ============================================================================
// Projection — camera view model and world-to-screen math (UE5 LWC doubles).
// Pure functions: no game memory, no rendering dependency, fully testable.
// ============================================================================
#pragma once
#include "mythos/UnrealTypes.hpp"

#include "Offsets.hpp"

namespace mythos {

enum class AspectAxis : uint8_t {
	MaintainYFOV = 0,
	MaintainXFOV = 1,
	MajorAxisFOV = 2,
};

struct CameraView {
	FVector  location;
	FRotator rotation;
	float    fov = 0.0f;
	float    aspectRatio = 0.0f;
	bool     constrainAspect = false;
	uint8_t  axisConstraint = static_cast<uint8_t>(AspectAxis::MaintainYFOV);
	bool     valid = false;
	bool     usedFallbackFov = false; // both camera caches failed; use the configured fallback

	[[nodiscard]] bool finite() const {
		return location.finite() && rotation.finite() && std::isfinite(fov) && std::isfinite(aspectRatio);
	}
};

struct ProjectionSettings {
	// -1 = read the axis from the game, 0..2 = force an axis.
	int    axisOverride = static_cast<int>(AspectAxis::MaintainXFOV);
	double fovScale = static_cast<double>(Offsets::Limits::FovScale);
	float  fallbackFov = Offsets::Limits::FallbackFOV;
};

struct ProjectionMultipliers {
	double x = 1.0;
	double y = 1.0;
};

// Axis/aspect handling shared by the projection and by the diagnostics view.
[[nodiscard]] ProjectionMultipliers ComputeProjectionMultipliers(const CameraView& view,
                                                                 const ProjectionSettings& settings,
                                                                 float screenWidth,
                                                                 float screenHeight);

// Projects a world position into screen space. Returns false when the point is
// behind the camera, the camera is invalid, or the result is not finite.
[[nodiscard]] bool ProjectWorldToScreen(const CameraView& view,
                                        const ProjectionSettings& settings,
                                        float screenWidth,
                                        float screenHeight,
                                        const FVector& world,
                                        Vec2d& out);

// Rotation matrix rows (forward/right/up) for a rotator, matching the legacy
// renderer's basis construction.
void BuildCameraBasis(const FRotator& rotation, FVector& forward, FVector& right, FVector& up);

} // namespace mythos
