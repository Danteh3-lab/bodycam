// ============================================================================
// BoxMath — pure 2D box generation for the ESP renderer.
// Kept in core so it is unit-testable without a renderer or a game.
// ============================================================================
#pragma once
#include "nova/UnrealTypes.hpp"

#include <vector>

namespace nova {

struct BoxRect {
	double left = 0.0;
	double top = 0.0;
	double right = 0.0;
	double bottom = 0.0;
	bool   valid = false;

	[[nodiscard]] double width() const { return right - left; }
	[[nodiscard]] double height() const { return bottom - top; }
	[[nodiscard]] double centerX() const { return (left + right) * 0.5; }
	[[nodiscard]] double centerY() const { return (top + bottom) * 0.5; }
};

// Box from the screen-space capsule endpoints. `radius`/`halfHeight` are in
// world units; the width/height ratio mirrors the 3D capsule aspect.
[[nodiscard]] BoxRect ComputeCapsuleBox(const Vec2d& top, const Vec2d& bottom,
                                        double radius, double halfHeight);

// Box fitted to successfully projected pose points. `totalBones` is the full
// pose count so partial occlusion can be rejected.
[[nodiscard]] BoxRect ComputePoseBox(const std::vector<Vec2d>& projectedPoints, int totalBones);

// Scales a box around its center.
void ScaleBox(BoxRect& box, double scale);

// Health colour ramp shared by the health bar and the numeric readout.
struct Color4 { float r = 1.0f; float g = 1.0f; float b = 1.0f; float a = 1.0f; };
[[nodiscard]] Color4 HealthColor(float percent);

} // namespace nova
