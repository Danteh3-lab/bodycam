// ============================================================================
// EspRenderer — draws the read-only ESP from immutable snapshots.
// Runs on the overlay thread; never touches game memory.
// ============================================================================
#pragma once
#include "AimController.hpp"

#include "nova/BoxMath.hpp"
#include "nova/Config.hpp"
#include "nova/Diagnostics.hpp"
#include "nova/GameSnapshot.hpp"

#include <imgui.h>

#include <cstdint>
#include <vector>

namespace nova_host {

class EspRenderer {
public:
	void Draw(const nova::GameSnapshot& snapshot, const nova::OverlayConfig& config,
	          float screenWidth, float screenHeight, nova::BoneCounters& boneCounters);

	// FOV circles and the target line. Uses only config + worker telemetry.
	void DrawAimOverlay(const nova::OverlayConfig& config, const AimTelemetry& aim,
	                    float screenWidth, float screenHeight);

private:
	struct ProjectedPose {
		std::vector<nova::Vec2d> points;
		std::vector<uint8_t> valid;
		std::vector<nova::Vec2d> visible;
	};

	[[nodiscard]] nova::ProjectionSettings BuildProjection(const nova::OverlayConfig& config) const;

	void DrawSkeleton(const nova::PlayerSnapshot& player, const ProjectedPose& pose,
	                  ImU32 color, nova::BoneCounters& boneCounters) const;

	void DrawText(const ImVec2& position, const char* text, ImU32 color, bool centered) const;
	void DrawLine(const ImVec2& from, const ImVec2& to, ImU32 color) const;
	void DrawBoxOutline(const nova::BoxRect& box, ImU32 color) const;
	void DrawCornerBox(const nova::BoxRect& box, ImU32 color) const;
	void DrawCircle(const ImVec2& center, float radius, ImU32 color) const;
	void DrawHealthBar(const nova::BoxRect& box, float percent) const;

	float textScale_ = 1.0f;
	bool outline_ = true;
	float outlineExtra_ = 2.0f;
	float lineThickness_ = 1.0f;
};

} // namespace nova_host
