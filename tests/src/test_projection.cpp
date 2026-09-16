#include "test_framework.h"

#include "nova/BoxMath.hpp"
#include "nova/Projection.hpp"

#include <cmath>

namespace {

nova::CameraView CenteredCamera() {
	nova::CameraView view;
	view.location = nova::FVector{ 0.0, 0.0, 0.0 };
	view.rotation = nova::FRotator{ 0.0, 0.0, 0.0 }; // facing +X
	view.fov = 90.0f;
	view.axisConstraint = static_cast<uint8_t>(nova::AspectAxis::MaintainXFOV);
	view.valid = true;
	return view;
}

constexpr float kWidth = 1920.0f;
constexpr float kHeight = 1080.0f;

} // namespace

NOVA_TEST(CenterProjectsToScreenCenter) {
	nova::ProjectionSettings settings;
	settings.fovScale = 1.0;
	nova::Vec2d point;
	CHECK(nova::ProjectWorldToScreen(CenteredCamera(), settings, kWidth, kHeight,
	                                 nova::FVector{ 100.0, 0.0, 0.0 }, point));
	CHECK(std::abs(point.x - kWidth * 0.5) < 1e-6);
	CHECK(std::abs(point.y - kHeight * 0.5) < 1e-6);
}

NOVA_TEST(OffCenterProjectsOutward) {
	nova::ProjectionSettings settings;
	settings.fovScale = 1.0;
	nova::Vec2d point;
	CHECK(nova::ProjectWorldToScreen(CenteredCamera(), settings, kWidth, kHeight,
	                                 nova::FVector{ 100.0, 100.0, 0.0 }, point));
	CHECK(point.x > kWidth * 0.5);
	CHECK(std::abs(point.y - kHeight * 0.5) < 1e-6);
}

NOVA_TEST(BehindCameraIsRejected) {
	nova::ProjectionSettings settings;
	nova::Vec2d point;
	CHECK(!nova::ProjectWorldToScreen(CenteredCamera(), settings, kWidth, kHeight,
	                                  nova::FVector{ -100.0, 0.0, 0.0 }, point));
	nova::CameraView invalid = CenteredCamera();
	invalid.valid = false;
	CHECK(!nova::ProjectWorldToScreen(invalid, settings, kWidth, kHeight,
	                                  nova::FVector{ 100.0, 0.0, 0.0 }, point));
}

NOVA_TEST(FovScaleChangesProjection) {
	nova::CameraView view = CenteredCamera();
	nova::Vec2d unscaled;
	nova::Vec2d scaled;
	nova::ProjectionSettings plain;
	plain.fovScale = 1.0;
	nova::ProjectionSettings corrected;
	corrected.fovScale = 1.15;

	CHECK(nova::ProjectWorldToScreen(view, plain, kWidth, kHeight,
	                                 nova::FVector{ 100.0, 50.0, 0.0 }, unscaled));
	CHECK(nova::ProjectWorldToScreen(view, corrected, kWidth, kHeight,
	                                 nova::FVector{ 100.0, 50.0, 0.0 }, scaled));
	CHECK(scaled.x > unscaled.x);
}

NOVA_TEST(ProjectionMultiplierModes) {
	nova::CameraView view = CenteredCamera();

	nova::ProjectionSettings forcedX;
	forcedX.axisOverride = static_cast<int>(nova::AspectAxis::MaintainXFOV);
	const nova::ProjectionMultipliers x = nova::ComputeProjectionMultipliers(view, forcedX, kWidth, kHeight);
	CHECK(std::abs(x.x - 1.0) < 1e-9);
	CHECK(std::abs(x.y - static_cast<double>(kWidth) / kHeight) < 1e-9);

	nova::ProjectionSettings forcedY;
	forcedY.axisOverride = static_cast<int>(nova::AspectAxis::MaintainYFOV);
	const nova::ProjectionMultipliers y = nova::ComputeProjectionMultipliers(view, forcedY, kWidth, kHeight);
	CHECK(std::abs(y.x - static_cast<double>(kHeight) / kWidth) < 1e-9);
	CHECK(std::abs(y.y - 1.0) < 1e-9);

	// Auto mode with a constrained aspect ratio uses the reported ratio.
	nova::CameraView constrained = view;
	constrained.constrainAspect = true;
	constrained.aspectRatio = 2.0f;
	nova::ProjectionSettings automatic;
	automatic.axisOverride = -1;
	const nova::ProjectionMultipliers autoMultipliers =
		nova::ComputeProjectionMultipliers(constrained, automatic, kWidth, kHeight);
	CHECK(std::abs(autoMultipliers.y - 2.0) < 1e-9);
}

NOVA_TEST(CapsuleBoxMirrorsAspect) {
	const nova::BoxRect box = nova::ComputeCapsuleBox(
		nova::Vec2d{ 100.0, 10.0 }, nova::Vec2d{ 100.0, 210.0 }, 34.0, 88.0);
	CHECK(box.valid);
	CHECK(std::abs(box.height() - 200.0) < 1e-9);
	CHECK(std::abs(box.width() - 200.0 * (34.0 / 88.0)) < 1e-9);
	CHECK(std::abs(box.centerX() - 100.0) < 1e-9);

	const nova::BoxRect degenerate = nova::ComputeCapsuleBox(
		nova::Vec2d{ 0.0, 10.0 }, nova::Vec2d{ 0.0, 10.5 }, 34.0, 88.0);
	CHECK(!degenerate.valid);
}

NOVA_TEST(PoseBoxNeedsMostOfThePose) {
	std::vector<nova::Vec2d> points;
	for (int i = 0; i < 8; ++i) {
		points.push_back(nova::Vec2d{ 100.0, 10.0 + static_cast<double>(i) * 10.0 });
	}
	const nova::BoxRect full = nova::ComputePoseBox(points, 9);
	CHECK(full.valid);

	std::vector<nova::Vec2d> partial(points.begin(), points.begin() + 4);
	const nova::BoxRect occluded = nova::ComputePoseBox(partial, 9);
	CHECK(!occluded.valid);

	std::vector<nova::Vec2d> tooFew{ { 0.0, 0.0 }, { 0.0, 1.0 }, { 0.0, 2.0 } };
	CHECK(!nova::ComputePoseBox(tooFew, 9).valid);
}

NOVA_TEST(ScaleBoxAroundCenter) {
	nova::BoxRect box = nova::ComputeCapsuleBox(
		nova::Vec2d{ 0.0, 0.0 }, nova::Vec2d{ 0.0, 100.0 }, 50.0, 100.0);
	CHECK(box.valid);
	const double centerX = box.centerX();
	const double centerY = box.centerY();
	nova::ScaleBox(box, 2.0);
	CHECK(std::abs(box.centerX() - centerX) < 1e-9);
	CHECK(std::abs(box.centerY() - centerY) < 1e-9);
	CHECK(std::abs(box.height() - 200.0) < 1e-9);
}

NOVA_TEST(HealthColorRamp) {
	const nova::Color4 high = nova::HealthColor(90.0f);
	CHECK(high.g > high.r);
	const nova::Color4 low = nova::HealthColor(10.0f);
	CHECK(low.r > low.g);
	const nova::Color4 clamped = nova::HealthColor(1000.0f);
	CHECK(clamped.g > clamped.r);
}
