#include "mythos/Aim.hpp"

#include "test_framework.h"

#include <cmath>
#include <cstddef>

namespace {

bool Near(double value, double expected, double tolerance = 1e-6) {
	return std::fabs(value - expected) <= tolerance;
}

mythos::GameSnapshot MakeSnapshot() {
	mythos::GameSnapshot snapshot;
	snapshot.valid = true;
	snapshot.camera.location = mythos::FVector{ 0.0, 0.0, 0.0 };
	snapshot.camera.rotation = mythos::FRotator{ 0.0, 0.0, 0.0 };
	snapshot.camera.fov = 90.0f;
	snapshot.camera.aspectRatio = 1.0f;
	snapshot.camera.valid = true;
	return snapshot;
}

mythos::PlayerSnapshot RootPlayer(double x, double y, double z) {
	mythos::PlayerSnapshot player;
	player.hasRoot = true;
	player.root = mythos::FVector{ x, y, z };
	return player;
}

mythos::AimSelectionSettings DefaultSelection() {
	mythos::AimSelectionSettings settings;
	settings.fovPixels = 150.0;
	settings.boneMode = 0;
	settings.visibleOnly = false;
	settings.ignoreTeam = true;
	return settings;
}

mythos::ProjectionSettings DefaultProjection() {
	mythos::ProjectionSettings projection;
	projection.axisOverride = static_cast<int>(mythos::AspectAxis::MaintainYFOV);
	projection.fovScale = 1.0;
	projection.fallbackFov = 90.0f;
	return projection;
}

bool Select(const mythos::GameSnapshot& snapshot, const mythos::AimSelectionSettings& settings,
            mythos::AimTarget& target) {
	return mythos::SelectAimTarget(snapshot, DefaultProjection(), 100.0f, 100.0f, settings, target);
}

} // namespace

MYTHOS_TEST(CalcAngleCardinalDirections) {
	const mythos::FVector origin{ 0.0, 0.0, 0.0 };

	const mythos::FRotator forward = mythos::CalcAngle(origin, mythos::FVector{ 100.0, 0.0, 0.0 });
	CHECK(Near(forward.yaw, 0.0));
	CHECK(Near(forward.pitch, 0.0));

	const mythos::FRotator left = mythos::CalcAngle(origin, mythos::FVector{ 0.0, 100.0, 0.0 });
	CHECK(Near(left.yaw, 90.0));

	const mythos::FRotator up = mythos::CalcAngle(origin, mythos::FVector{ 0.0, 0.0, 100.0 });
	CHECK(Near(up.pitch, 90.0));

	const mythos::FRotator down = mythos::CalcAngle(mythos::FVector{ 10.0, 10.0, 10.0 },
	                                            mythos::FVector{ 10.0, 10.0, 0.0 });
	CHECK(Near(down.pitch, -90.0));
	CHECK(Near(down.roll, 0.0));
}

MYTHOS_TEST(NormalizeAngleWrapsIntoRange) {
	CHECK(Near(mythos::NormalizeAngle(190.0), -170.0));
	CHECK(Near(mythos::NormalizeAngle(-190.0), 170.0));
	CHECK(Near(mythos::NormalizeAngle(360.0), 0.0));
	CHECK(Near(mythos::NormalizeAngle(20.0), 20.0));
}

MYTHOS_TEST(ComputeAimStepSmoothsAndClamps) {
	const mythos::FRotator current{ 0.0, 0.0, 0.0 };

	const mythos::FRotator half = mythos::ComputeAimStep(current, mythos::FRotator{ 0.0, 10.0, 0.0 },
	                                                 2.0, 25.0);
	CHECK(Near(half.yaw, 5.0));
	CHECK(Near(half.pitch, 0.0));

	const mythos::FRotator clamped = mythos::ComputeAimStep(current, mythos::FRotator{ -500.0, 500.0, 0.0 },
	                                                    1.0, 25.0);
	CHECK(Near(clamped.yaw, 25.0));
	CHECK(Near(clamped.pitch, -25.0));

	const mythos::FRotator wrapped = mythos::ComputeAimStep(mythos::FRotator{ 0.0, 170.0, 0.0 },
	                                                    mythos::FRotator{ 0.0, -170.0, 0.0 },
	                                                    1.0, 25.0);
	CHECK(Near(wrapped.yaw, 20.0));
}

MYTHOS_TEST(SmoothRotationBlendsFullError) {
	const mythos::FRotator result = mythos::SmoothRotation(mythos::FRotator{ 0.0, 0.0, 0.0 },
	                                                   mythos::FRotator{ 10.0, 90.0, 0.0 },
	                                                   2.0f);
	CHECK(Near(result.pitch, 5.0));
	CHECK(Near(result.yaw, 45.0));
}

MYTHOS_TEST(SelectAimTargetPicksClosestToCrosshair) {
	mythos::GameSnapshot snapshot = MakeSnapshot();
	snapshot.players.push_back(RootPlayer(1000.0, 100.0, 0.0));
	snapshot.players.push_back(RootPlayer(1000.0, 0.0, 10.0));

	mythos::AimTarget target;
	CHECK(Select(snapshot, DefaultSelection(), target));
	CHECK_EQ(target.playerIndex, static_cast<std::size_t>(1));
	CHECK(Near(target.world.y, 0.0));
	CHECK(Near(target.world.z, 10.0));
	CHECK(target.crosshairPixels < 1.0);
}

MYTHOS_TEST(SelectAimTargetAppliesFilters) {
	mythos::AimSelectionSettings settings = DefaultSelection();

	{
		mythos::GameSnapshot snapshot = MakeSnapshot();
		mythos::PlayerSnapshot self = RootPlayer(1000.0, 0.0, 0.0);
		self.isSelf = true;
		snapshot.players.push_back(self);
		mythos::AimTarget target;
		CHECK(!Select(snapshot, settings, target));
	}
	{
		mythos::GameSnapshot snapshot = MakeSnapshot();
		mythos::PlayerSnapshot drone = RootPlayer(1000.0, 0.0, 0.0);
		drone.kind = mythos::PlayerKind::Drone;
		snapshot.players.push_back(drone);
		mythos::AimTarget target;
		CHECK(!Select(snapshot, settings, target));
	}
	{
		mythos::GameSnapshot snapshot = MakeSnapshot();
		mythos::PlayerSnapshot dead = RootPlayer(1000.0, 0.0, 0.0);
		dead.hasHealth = true;
		dead.health = 0.0f;
		dead.dead = true;
		snapshot.players.push_back(dead);
		mythos::AimTarget target;
		CHECK(!Select(snapshot, settings, target));
	}
	{
		mythos::GameSnapshot snapshot = MakeSnapshot();
		mythos::PlayerSnapshot teammate = RootPlayer(1000.0, 0.0, 0.0);
		teammate.sameTeam = true;
		snapshot.players.push_back(teammate);
		mythos::AimTarget target;
		CHECK(!Select(snapshot, settings, target));

		settings.ignoreTeam = false;
		CHECK(Select(snapshot, settings, target));
		settings.ignoreTeam = true;
	}
	{
		mythos::GameSnapshot snapshot = MakeSnapshot();
		mythos::PlayerSnapshot occluded = RootPlayer(1000.0, 0.0, 0.0);
		occluded.visible = false;
		snapshot.players.push_back(occluded);

		mythos::AimTarget target;
		CHECK(Select(snapshot, settings, target)); // fail-open when not filtered

		settings.visibleOnly = true;
		CHECK(!Select(snapshot, settings, target));
		settings.visibleOnly = false;
	}
}

MYTHOS_TEST(SelectAimTargetHonoursFovAndScreen) {
	mythos::AimSelectionSettings settings = DefaultSelection();
	settings.fovPixels = 10.0;

	mythos::GameSnapshot snapshot = MakeSnapshot();
	snapshot.players.push_back(RootPlayer(1000.0, 500.0, 0.0)); // ~25 px off centre
	mythos::AimTarget target;
	CHECK(!Select(snapshot, settings, target));

	settings.fovPixels = 150.0;
	snapshot.players.clear();
	snapshot.players.push_back(RootPlayer(-1000.0, 0.0, 0.0)); // behind the camera
	CHECK(!Select(snapshot, settings, target));

	snapshot.players.clear();
	snapshot.players.push_back(RootPlayer(1000.0, 0.0, 0.0));
	CHECK(Select(snapshot, settings, target));
}

MYTHOS_TEST(SelectAimTargetPrefersHeadBone) {
	mythos::GameSnapshot snapshot = MakeSnapshot();
	mythos::PlayerSnapshot player;
	player.hasPose = true;
	player.bones.push_back(mythos::BonePoint{ mythos::FVector{ 1000.0, 0.0, 0.0 } });
	player.bones.push_back(mythos::BonePoint{ mythos::FVector{ 1000.0, 0.0, 50.0 } });
	player.bones.push_back(mythos::BonePoint{ mythos::FVector{ 1000.0, 0.0, 100.0 } });
	player.headBone = 2;
	snapshot.players.push_back(player);

	mythos::AimTarget target;
	mythos::AimSelectionSettings settings = DefaultSelection();
	CHECK(Select(snapshot, settings, target));
	CHECK(Near(target.world.z, 100.0));

	settings.boneMode = 1;
	CHECK(Select(snapshot, settings, target));
	CHECK(Near(target.world.z, 50.0));
}
