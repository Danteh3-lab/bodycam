// ============================================================================
// EspRenderer — draws the read-only ESP from immutable snapshots.
// Runs on the overlay thread; never touches game memory.
// ============================================================================
#pragma once
#include "AimController.hpp"

#include "mythos/BoxMath.hpp"
#include "mythos/Config.hpp"
#include "mythos/Diagnostics.hpp"
#include "mythos/GameSnapshot.hpp"

#include <imgui.h>

#include <cstdint>
#include <vector>

namespace mythos_host {

class EspRenderer {
public:
	void Draw(const mythos::GameSnapshot& snapshot, const mythos::OverlayConfig& config,
	          float screenWidth, float screenHeight, mythos::BoneCounters& boneCounters,
	          mythos::EntityCounters& entityCounters);

	// FOV circles and the target line. Uses only config + worker telemetry.
	void DrawAimOverlay(const mythos::OverlayConfig& config, const AimTelemetry& aim,
	                    float screenWidth, float screenHeight);

private:
	struct ProjectedPose {
		std::vector<mythos::Vec2d> points;
		std::vector<uint8_t> valid;
		std::vector<mythos::Vec2d> visible;
	};

	[[nodiscard]] mythos::ProjectionSettings BuildProjection(const mythos::OverlayConfig& config) const;

	void DrawSkeleton(const mythos::PlayerSnapshot& player, const ProjectedPose& pose,
	                  ImU32 color, mythos::BoneCounters& boneCounters) const;

	void DrawText(const ImVec2& position, const char* text, ImU32 color, bool centered) const;
	void DrawLine(const ImVec2& from, const ImVec2& to, ImU32 color) const;
	void DrawBoxOutline(const mythos::BoxRect& box, ImU32 color) const;
	void DrawCornerBox(const mythos::BoxRect& box, ImU32 color) const;
	void DrawCircle(const ImVec2& center, float radius, ImU32 color) const;
	void DrawHealthBar(const mythos::BoxRect& box, float percent) const;

	float textScale_ = 1.0f;
	bool outline_ = true;
	float outlineExtra_ = 2.0f;
	float lineThickness_ = 1.0f;
};

} // namespace mythos_host
