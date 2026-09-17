#include "test_framework.h"

#include "mythos/BoxMath.hpp"
#include "mythos/Projection.hpp"

#include <cmath>

namespace {

mythos::CameraView CenteredCamera() {
	mythos::CameraView view;
	view.location = mythos::FVector{ 0.0, 0.0, 0.0 };
	view.rotation = mythos::FRotator{ 0.0, 0.0, 0.0 }; // facing +X
	view.fov = 90.0f;
	view.axisConstraint = static_cast<uint8_t>(mythos::AspectAxis::MaintainXFOV);
	view.valid = true;
	return view;
}

constexpr float kWidth = 1920.0f;
constexpr float kHeight = 1080.0f;

} // namespace

MYTHOS_TEST(CenterProjectsToScreenCenter) {
	mythos::ProjectionSettings settings;
	settings.fovScale = 1.0;
	mythos::Vec2d point;
	CHECK(mythos::ProjectWorldToScreen(CenteredCamera(), settings, kWidth, kHeight,
	                                 mythos::FVector{ 100.0, 0.0, 0.0 }, point));
	CHECK(std::abs(point.x - kWidth * 0.5) < 1e-6);
	CHECK(std::abs(point.y - kHeight * 0.5) < 1e-6);
}

MYTHOS_TEST(OffCenterProjectsOutward) {
	mythos::ProjectionSettings settings;
	settings.fovScale = 1.0;
	mythos::Vec2d point;
	CHECK(mythos::ProjectWorldToScreen(CenteredCamera(), settings, kWidth, kHeight,
	                                 mythos::FVector{ 100.0, 100.0, 0.0 }, point));
	CHECK(point.x > kWidth * 0.5);
	CHECK(std::abs(point.y - kHeight * 0.5) < 1e-6);
}

MYTHOS_TEST(BehindCameraIsRejected) {
	mythos::ProjectionSettings settings;
	mythos::Vec2d point;
	CHECK(!mythos::ProjectWorldToScreen(CenteredCamera(), settings, kWidth, kHeight,
	                                  mythos::FVector{ -100.0, 0.0, 0.0 }, point));
	mythos::CameraView invalid = CenteredCamera();
	invalid.valid = false;
	CHECK(!mythos::ProjectWorldToScreen(invalid, settings, kWidth, kHeight,
	                                  mythos::FVector{ 100.0, 0.0, 0.0 }, point));
}

MYTHOS_TEST(FovScaleChangesProjection) {
	mythos::CameraView view = CenteredCamera();
	mythos::Vec2d unscaled;
	mythos::Vec2d scaled;
	mythos::ProjectionSettings plain;
	plain.fovScale = 1.0;
	mythos::ProjectionSettings corrected;
	corrected.fovScale = 1.15;

	CHECK(mythos::ProjectWorldToScreen(view, plain, kWidth, kHeight,
	                                 mythos::FVector{ 100.0, 50.0, 0.0 }, unscaled));
	CHECK(mythos::ProjectWorldToScreen(view, corrected, kWidth, kHeight,
	                                 mythos::FVector{ 100.0, 50.0, 0.0 }, scaled));
	CHECK(scaled.x > unscaled.x);
}

MYTHOS_TEST(ProjectionMultiplierModes) {
	mythos::CameraView view = CenteredCamera();

	mythos::ProjectionSettings forcedX;
	forcedX.axisOverride = static_cast<int>(mythos::AspectAxis::MaintainXFOV);
	const mythos::ProjectionMultipliers x = mythos::ComputeProjectionMultipliers(view, forcedX, kWidth, kHeight);
	CHECK(std::abs(x.x - 1.0) < 1e-9);
	CHECK(std::abs(x.y - static_cast<double>(kWidth) / kHeight) < 1e-9);

	mythos::ProjectionSettings forcedY;
	forcedY.axisOverride = static_cast<int>(mythos::AspectAxis::MaintainYFOV);
	const mythos::ProjectionMultipliers y = mythos::ComputeProjectionMultipliers(view, forcedY, kWidth, kHeight);
	CHECK(std::abs(y.x - static_cast<double>(kHeight) / kWidth) < 1e-9);
	CHECK(std::abs(y.y - 1.0) < 1e-9);

	// Auto mode with a constrained aspect ratio uses the reported ratio.
	mythos::CameraView constrained = view;
	constrained.constrainAspect = true;
	constrained.aspectRatio = 2.0f;
	mythos::ProjectionSettings automatic;
	automatic.axisOverride = -1;
	const mythos::ProjectionMultipliers autoMultipliers =
		mythos::ComputeProjectionMultipliers(constrained, automatic, kWidth, kHeight);
	CHECK(std::abs(autoMultipliers.y - 2.0) < 1e-9);
}

MYTHOS_TEST(CapsuleBoxMirrorsAspect) {
	const mythos::BoxRect box = mythos::ComputeCapsuleBox(
		mythos::Vec2d{ 100.0, 10.0 }, mythos::Vec2d{ 100.0, 210.0 }, 34.0, 88.0);
	CHECK(box.valid);
	CHECK(std::abs(box.height() - 200.0) < 1e-9);
	CHECK(std::abs(box.width() - 200.0 * (34.0 / 88.0)) < 1e-9);
	CHECK(std::abs(box.centerX() - 100.0) < 1e-9);

	const mythos::BoxRect degenerate = mythos::ComputeCapsuleBox(
		mythos::Vec2d{ 0.0, 10.0 }, mythos::Vec2d{ 0.0, 10.5 }, 34.0, 88.0);
	CHECK(!degenerate.valid);
}

MYTHOS_TEST(PoseBoxNeedsMostOfThePose) {
	std::vector<mythos::Vec2d> points;
	for (int i = 0; i < 8; ++i) {
		points.push_back(mythos::Vec2d{ 100.0, 10.0 + static_cast<double>(i) * 10.0 });
	}
	const mythos::BoxRect full = mythos::ComputePoseBox(points, 9);
	CHECK(full.valid);

	std::vector<mythos::Vec2d> partial(points.begin(), points.begin() + 4);
	const mythos::BoxRect occluded = mythos::ComputePoseBox(partial, 9);
	CHECK(!occluded.valid);

	std::vector<mythos::Vec2d> tooFew{ { 0.0, 0.0 }, { 0.0, 1.0 }, { 0.0, 2.0 } };
	CHECK(!mythos::ComputePoseBox(tooFew, 9).valid);
}

MYTHOS_TEST(ScaleBoxAroundCenter) {
	mythos::BoxRect box = mythos::ComputeCapsuleBox(
		mythos::Vec2d{ 0.0, 0.0 }, mythos::Vec2d{ 0.0, 100.0 }, 50.0, 100.0);
	CHECK(box.valid);
	const double centerX = box.centerX();
	const double centerY = box.centerY();
	mythos::ScaleBox(box, 2.0);
	CHECK(std::abs(box.centerX() - centerX) < 1e-9);
	CHECK(std::abs(box.centerY() - centerY) < 1e-9);
	CHECK(std::abs(box.height() - 200.0) < 1e-9);
}

MYTHOS_TEST(HealthColorRamp) {
	const mythos::Color4 high = mythos::HealthColor(90.0f);
	CHECK(high.g > high.r);
	const mythos::Color4 low = mythos::HealthColor(10.0f);
	CHECK(low.r > low.g);
	const mythos::Color4 clamped = mythos::HealthColor(1000.0f);
	CHECK(clamped.g > clamped.r);
}
