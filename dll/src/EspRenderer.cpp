#include "EspRenderer.hpp"

#include "Theme.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>

namespace nova_host {

nova::ProjectionSettings EspRenderer::BuildProjection(const nova::OverlayConfig& config) const {
	nova::ProjectionSettings settings;
	settings.axisOverride = config.projection.axisOverride;
	settings.fovScale = static_cast<double>(config.projection.fovScale);
	settings.fallbackFov = config.projection.fallbackFov;
	return settings;
}

void EspRenderer::DrawText(const ImVec2& position, const char* text, ImU32 color, bool centered) const {
	if (text == nullptr || *text == '\0') return;
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	ImFont* font = ImGui::GetFont();
	if (drawList == nullptr || font == nullptr) return;

	const float size = (std::max)(6.0f, ImGui::GetFontSize() * textScale_);
	ImVec2 point = position;
	if (centered) {
		point.x -= font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x * 0.5f;
	}

	if (outline_) {
		drawList->AddText(font, size, ImVec2(point.x - 1.0f, point.y), theme::kEspOutline, text);
		drawList->AddText(font, size, ImVec2(point.x + 1.0f, point.y), theme::kEspOutline, text);
		drawList->AddText(font, size, ImVec2(point.x, point.y - 1.0f), theme::kEspOutline, text);
		drawList->AddText(font, size, ImVec2(point.x, point.y + 1.0f), theme::kEspOutline, text);
	}
	drawList->AddText(font, size, point, color, text);
}

void EspRenderer::DrawLine(const ImVec2& from, const ImVec2& to, ImU32 color) const {
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	if (drawList == nullptr) return;
	if (outline_) {
		drawList->AddLine(from, to, theme::kEspOutline, lineThickness_ + outlineExtra_);
	}
	drawList->AddLine(from, to, color, lineThickness_);
}

void EspRenderer::DrawBoxOutline(const nova::BoxRect& box, ImU32 color) const {
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	if (drawList == nullptr) return;
	const ImVec2 topLeft(static_cast<float>(box.left), static_cast<float>(box.top));
	const ImVec2 bottomRight(static_cast<float>(box.right), static_cast<float>(box.bottom));
	if (outline_) {
		drawList->AddRect(topLeft, bottomRight, theme::kEspOutline, 0.0f, 0,
		                  lineThickness_ + outlineExtra_);
	}
	drawList->AddRect(topLeft, bottomRight, color, 0.0f, 0, lineThickness_);
}

void EspRenderer::DrawCornerBox(const nova::BoxRect& box, ImU32 color) const {
	const double width = box.width();
	const double height = box.height();
	if (width <= 0.0 || height <= 0.0) return;

	const double lengthX = width * 0.25;
	const double lengthY = height * 0.25;

	const ImVec2 tl(static_cast<float>(box.left), static_cast<float>(box.top));
	const ImVec2 tr(static_cast<float>(box.right), static_cast<float>(box.top));
	const ImVec2 bl(static_cast<float>(box.left), static_cast<float>(box.bottom));
	const ImVec2 br(static_cast<float>(box.right), static_cast<float>(box.bottom));

	DrawLine(tl, ImVec2(static_cast<float>(box.left + lengthX), tl.y), color);
	DrawLine(tl, ImVec2(tl.x, static_cast<float>(box.top + lengthY)), color);
	DrawLine(tr, ImVec2(static_cast<float>(box.right - lengthX), tr.y), color);
	DrawLine(tr, ImVec2(tr.x, static_cast<float>(box.top + lengthY)), color);
	DrawLine(bl, ImVec2(static_cast<float>(box.left + lengthX), bl.y), color);
	DrawLine(bl, ImVec2(bl.x, static_cast<float>(box.bottom - lengthY)), color);
	DrawLine(br, ImVec2(static_cast<float>(box.right - lengthX), br.y), color);
	DrawLine(br, ImVec2(br.x, static_cast<float>(box.bottom - lengthY)), color);
}

void EspRenderer::DrawCircle(const ImVec2& center, float radius, ImU32 color) const {
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	if (drawList == nullptr || radius <= 0.0f) return;
	if (outline_) {
		drawList->AddCircle(center, radius, theme::kEspOutline, 0, lineThickness_ + outlineExtra_);
	}
	drawList->AddCircle(center, radius, color, 0, lineThickness_);
}

void EspRenderer::DrawHealthBar(const nova::BoxRect& box, float percent) const {
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	if (drawList == nullptr) return;
	percent = (std::max)(0.0f, (std::min)(100.0f, percent));

	const float barWidth = 3.0f;
	const float x = static_cast<float>(box.left) - barWidth - 3.0f;
	const float height = static_cast<float>(box.height());
	if (height <= 1.0f) return;

	drawList->AddRectFilled(ImVec2(x - 1.0f, static_cast<float>(box.top) - 1.0f),
	                        ImVec2(x + barWidth + 1.0f, static_cast<float>(box.bottom) + 1.0f),
	                        theme::kEspOutline);

	const nova::Color4 color = nova::HealthColor(percent);
	const float fill = height * (percent / 100.0f);
	drawList->AddRectFilled(ImVec2(x, static_cast<float>(box.bottom) - fill),
	                        ImVec2(x + barWidth, static_cast<float>(box.bottom)),
	                        IM_COL32(static_cast<int>(color.r * 255.0f),
	                                 static_cast<int>(color.g * 255.0f),
	                                 static_cast<int>(color.b * 255.0f), 255));
}

void EspRenderer::DrawSkeleton(const nova::PlayerSnapshot& player, const ProjectedPose& pose,
                               ImU32 color, nova::BoneCounters& boneCounters) const {
	const size_t count = (std::min)(pose.points.size(), player.bones.size());
	int drawn = 0;
	for (size_t i = 0; i < count; ++i) {
		if (pose.valid[i] == 0) continue;
		const int parent = player.bones[i].parent;
		if (parent < 0 || static_cast<size_t>(parent) >= count) continue;
		if (pose.valid[static_cast<size_t>(parent)] == 0) continue;
		DrawLine(ImVec2(static_cast<float>(pose.points[i].x), static_cast<float>(pose.points[i].y)),
		         ImVec2(static_cast<float>(pose.points[static_cast<size_t>(parent)].x),
		                static_cast<float>(pose.points[static_cast<size_t>(parent)].y)),
		         color);
		++drawn;
	}
	if (drawn > 0) ++boneCounters.skeletonsDrawn;
}

void EspRenderer::Draw(const nova::GameSnapshot& snapshot, const nova::OverlayConfig& config,
                       float screenWidth, float screenHeight, nova::BoneCounters& boneCounters) {
	if (!snapshot.valid || screenWidth <= 0.0f || screenHeight <= 0.0f) return;

	textScale_ = config.visuals.textScale;
	outline_ = config.visuals.outline;
	outlineExtra_ = config.visuals.outlineExtra;
	lineThickness_ = config.visuals.lineThickness;

	nova::CameraView view = snapshot.camera;
	if (view.usedFallbackFov) view.fov = config.projection.fallbackFov;
	if (!view.valid) return;

	const nova::ProjectionSettings projection = BuildProjection(config);
	const nova::PlayerFeatureConfig& features = config.players;

	ProjectedPose poseScratch;

	for (const nova::PlayerSnapshot& player : snapshot.players) {
		const bool occluded = !player.visible;
		if (features.visibleOnly && occluded) continue;

		const ImU32 color = occluded ? theme::kEspOccluded
		                             : (player.sameTeam ? theme::kEspTeam : theme::kEspEnemy);

		nova::BoxRect box;
		bool haveBox = false;

		const bool needPoseProjection =
			(features.boxFromBones || features.skeleton || features.headDot) &&
			player.hasPose && !player.bones.empty();
		if (needPoseProjection) {
			poseScratch.points.assign(player.bones.size(), nova::Vec2d{});
			poseScratch.valid.assign(player.bones.size(), 0);
			poseScratch.visible.clear();

			for (size_t i = 0; i < player.bones.size(); ++i) {
				nova::Vec2d projected;
				if (!nova::ProjectWorldToScreen(view, projection, screenWidth, screenHeight,
				                                player.bones[i].world, projected)) {
					continue;
				}
				poseScratch.points[i] = projected;
				poseScratch.valid[i] = 1;
				poseScratch.visible.push_back(projected);
			}
		}

		if (features.boxFromBones && needPoseProjection) {
			box = nova::ComputePoseBox(poseScratch.visible, static_cast<int>(player.bones.size()));
			haveBox = box.valid;
		}

		if (!haveBox && player.hasCapsule) {
			nova::Vec2d top;
			nova::Vec2d bottom;
			if (nova::ProjectWorldToScreen(view, projection, screenWidth, screenHeight,
			                               player.capsuleTop, top) &&
			    nova::ProjectWorldToScreen(view, projection, screenWidth, screenHeight,
			                               player.capsuleBottom, bottom)) {
				box = nova::ComputeCapsuleBox(top, bottom, player.capsuleRadius,
				                              player.capsuleHalfHeight);
				haveBox = box.valid;
			}
		}

		if (!haveBox) continue;

		nova::ScaleBox(box, static_cast<double>(features.boxScale));
		if (box.right < 0.0 || box.bottom < 0.0 || box.left > screenWidth || box.top > screenHeight) {
			continue;
		}

		const float centerX = static_cast<float>(box.centerX());
		const float top = static_cast<float>(box.top);
		const float bottom = static_cast<float>(box.bottom);

		if (features.boxMode == static_cast<int>(nova::BoxMode::Full)) {
			DrawBoxOutline(box, color);
		} else if (features.boxMode == static_cast<int>(nova::BoxMode::Corners)) {
			DrawCornerBox(box, color);
		}

		if (features.health && player.hasHealth && player.maxHealth > 0.0f) {
			DrawHealthBar(box, player.health / player.maxHealth * 100.0f);
		}

		if (features.snapline) {
			DrawLine(ImVec2(screenWidth * 0.5f, screenHeight), ImVec2(centerX, bottom), color);
		}

		if (features.skeleton && player.hasPose && !player.bones.empty()) {
			DrawSkeleton(player, poseScratch, color, boneCounters);
		}

		float textY = top - ImGui::GetFontSize() * textScale_ - 2.0f;

		if (features.name && player.hasName && player.name[0] != '\0') {
			DrawText(ImVec2(centerX, textY), player.name, color, true);
			textY -= ImGui::GetFontSize() * textScale_ + 1.0f;
		}
		if (player.isDrone()) {
			DrawText(ImVec2(centerX, textY), "[DRONE]", theme::kEspDrone, true);
			textY -= ImGui::GetFontSize() * textScale_ + 1.0f;
		}

		float belowY = bottom + 2.0f;
		if (features.health && player.hasHealth) {
			char healthText[32] = {};
			std::snprintf(healthText, sizeof(healthText), "%.0f HP",
			              static_cast<double>(player.health));
			const nova::Color4 healthColor = nova::HealthColor(player.health / player.maxHealth * 100.0f);
			DrawText(ImVec2(centerX, belowY), healthText,
			         IM_COL32(static_cast<int>(healthColor.r * 255.0f),
			                  static_cast<int>(healthColor.g * 255.0f),
			                  static_cast<int>(healthColor.b * 255.0f), 255),
			         true);
			belowY += ImGui::GetFontSize() * textScale_ + 1.0f;
		}
		if (features.distance && player.distanceMeters > 0.0) {
			char distanceText[32] = {};
			std::snprintf(distanceText, sizeof(distanceText), "%.0f m", player.distanceMeters);
			DrawText(ImVec2(centerX, belowY), distanceText, theme::kEspInfo, true);
		}

		if (features.headDot && player.hasPose && player.headBone >= 0 &&
		    static_cast<size_t>(player.headBone) < poseScratch.points.size() &&
		    poseScratch.valid[static_cast<size_t>(player.headBone)] != 0) {
			const nova::Vec2d& head = poseScratch.points[static_cast<size_t>(player.headBone)];
			DrawCircle(ImVec2(static_cast<float>(head.x), static_cast<float>(head.y)),
			           features.headDotSize, color);
		}
	}
}

void EspRenderer::DrawAimOverlay(const nova::OverlayConfig& config, const AimTelemetry& aim,
                                 float screenWidth, float screenHeight) {
	if (screenWidth <= 0.0f || screenHeight <= 0.0f) return;
	if (!config.aim.drawFov && !config.aim.drawTarget) return;

	textScale_ = config.visuals.textScale;
	outline_ = config.visuals.outline;
	outlineExtra_ = config.visuals.outlineExtra;
	lineThickness_ = config.visuals.lineThickness;

	const ImVec2 center(screenWidth * 0.5f, screenHeight * 0.5f);

	if (config.aim.drawFov) {
		if (config.aim.enabled) {
			DrawCircle(center, config.aim.fov, IM_COL32(255, 255, 255, 110));
		}
		if (config.aim.softAim) {
			DrawCircle(center, config.aim.softFov, IM_COL32(255, 200, 0, 110));
		}
	}

	if (config.aim.drawTarget && (config.aim.enabled || config.aim.softAim) && aim.hasTarget) {
		DrawLine(center,
		         ImVec2(static_cast<float>(aim.targetScreen.x),
		                static_cast<float>(aim.targetScreen.y)),
		         IM_COL32(255, 220, 0, 200));
	}
}

} // namespace nova_host
