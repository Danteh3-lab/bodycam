#include "nova/Aim.hpp"

#include "test_framework.h"

#include <cmath>
#include <cstddef>

namespace {

bool Near(double value, double expected, double tolerance = 1e-6) {
	return std::fabs(value - expected) <= tolerance;
}

nova::GameSnapshot MakeSnapshot() {
	nova::GameSnapshot snapshot;
	snapshot.valid = true;
	snapshot.camera.location = nova::FVector{ 0.0, 0.0, 0.0 };
	snapshot.camera.rotation = nova::FRotator{ 0.0, 0.0, 0.0 };
	snapshot.camera.fov = 90.0f;
	snapshot.camera.aspectRatio = 1.0f;
	snapshot.camera.valid = true;
	return snapshot;
}

nova::PlayerSnapshot RootPlayer(double x, double y, double z) {
	nova::PlayerSnapshot player;
	player.hasRoot = true;
	player.root = nova::FVector{ x, y, z };
	return player;
}

nova::AimSelectionSettings DefaultSelection() {
	nova::AimSelectionSettings settings;
	settings.fovPixels = 150.0;
	settings.boneMode = 0;
	settings.visibleOnly = false;
	settings.ignoreTeam = true;
	return settings;
}

nova::ProjectionSettings DefaultProjection() {
	nova::ProjectionSettings projection;
	projection.axisOverride = static_cast<int>(nova::AspectAxis::MaintainYFOV);
	projection.fovScale = 1.0;
	projection.fallbackFov = 90.0f;
	return projection;
}

bool Select(const nova::GameSnapshot& snapshot, const nova::AimSelectionSettings& settings,
            nova::AimTarget& target) {
	return nova::SelectAimTarget(snapshot, DefaultProjection(), 100.0f, 100.0f, settings, target);
}

} // namespace

NOVA_TEST(CalcAngleCardinalDirections) {
	const nova::FVector origin{ 0.0, 0.0, 0.0 };

	const nova::FRotator forward = nova::CalcAngle(origin, nova::FVector{ 100.0, 0.0, 0.0 });
	CHECK(Near(forward.yaw, 0.0));
	CHECK(Near(forward.pitch, 0.0));

	const nova::FRotator left = nova::CalcAngle(origin, nova::FVector{ 0.0, 100.0, 0.0 });
	CHECK(Near(left.yaw, 90.0));

	const nova::FRotator up = nova::CalcAngle(origin, nova::FVector{ 0.0, 0.0, 100.0 });
	CHECK(Near(up.pitch, 90.0));

	const nova::FRotator down = nova::CalcAngle(nova::FVector{ 10.0, 10.0, 10.0 },
	                                            nova::FVector{ 10.0, 10.0, 0.0 });
	CHECK(Near(down.pitch, -90.0));
	CHECK(Near(down.roll, 0.0));
}

NOVA_TEST(NormalizeAngleWrapsIntoRange) {
	CHECK(Near(nova::NormalizeAngle(190.0), -170.0));
	CHECK(Near(nova::NormalizeAngle(-190.0), 170.0));
	CHECK(Near(nova::NormalizeAngle(360.0), 0.0));
	CHECK(Near(nova::NormalizeAngle(20.0), 20.0));
}

NOVA_TEST(ComputeAimStepSmoothsAndClamps) {
	const nova::FRotator current{ 0.0, 0.0, 0.0 };

	const nova::FRotator half = nova::ComputeAimStep(current, nova::FRotator{ 0.0, 10.0, 0.0 },
	                                                 2.0, 25.0);
	CHECK(Near(half.yaw, 5.0));
	CHECK(Near(half.pitch, 0.0));

	const nova::FRotator clamped = nova::ComputeAimStep(current, nova::FRotator{ -500.0, 500.0, 0.0 },
	                                                    1.0, 25.0);
	CHECK(Near(clamped.yaw, 25.0));
	CHECK(Near(clamped.pitch, -25.0));

	const nova::FRotator wrapped = nova::ComputeAimStep(nova::FRotator{ 0.0, 170.0, 0.0 },
	                                                    nova::FRotator{ 0.0, -170.0, 0.0 },
	                                                    1.0, 25.0);
	CHECK(Near(wrapped.yaw, 20.0));
}

NOVA_TEST(SmoothRotationBlendsFullError) {
	const nova::FRotator result = nova::SmoothRotation(nova::FRotator{ 0.0, 0.0, 0.0 },
	                                                   nova::FRotator{ 10.0, 90.0, 0.0 },
	                                                   2.0f);
	CHECK(Near(result.pitch, 5.0));
	CHECK(Near(result.yaw, 45.0));
}

NOVA_TEST(SelectAimTargetPicksClosestToCrosshair) {
	nova::GameSnapshot snapshot = MakeSnapshot();
	snapshot.players.push_back(RootPlayer(1000.0, 100.0, 0.0));
	snapshot.players.push_back(RootPlayer(1000.0, 0.0, 10.0));

	nova::AimTarget target;
	CHECK(Select(snapshot, DefaultSelection(), target));
	CHECK_EQ(target.playerIndex, static_cast<std::size_t>(1));
	CHECK(Near(target.world.y, 0.0));
	CHECK(Near(target.world.z, 10.0));
	CHECK(target.crosshairPixels < 1.0);
}

NOVA_TEST(SelectAimTargetAppliesFilters) {
	nova::AimSelectionSettings settings = DefaultSelection();

	{
		nova::GameSnapshot snapshot = MakeSnapshot();
		nova::PlayerSnapshot self = RootPlayer(1000.0, 0.0, 0.0);
		self.isSelf = true;
		snapshot.players.push_back(self);
		nova::AimTarget target;
		CHECK(!Select(snapshot, settings, target));
	}
	{
		nova::GameSnapshot snapshot = MakeSnapshot();
		nova::PlayerSnapshot drone = RootPlayer(1000.0, 0.0, 0.0);
		drone.kind = nova::PlayerKind::Drone;
		snapshot.players.push_back(drone);
		nova::AimTarget target;
		CHECK(!Select(snapshot, settings, target));
	}
	{
		nova::GameSnapshot snapshot = MakeSnapshot();
		nova::PlayerSnapshot dead = RootPlayer(1000.0, 0.0, 0.0);
		dead.hasHealth = true;
		dead.health = 0.0f;
		dead.dead = true;
		snapshot.players.push_back(dead);
		nova::AimTarget target;
		CHECK(!Select(snapshot, settings, target));
	}
	{
		nova::GameSnapshot snapshot = MakeSnapshot();
		nova::PlayerSnapshot teammate = RootPlayer(1000.0, 0.0, 0.0);
		teammate.sameTeam = true;
		snapshot.players.push_back(teammate);
		nova::AimTarget target;
		CHECK(!Select(snapshot, settings, target));

		settings.ignoreTeam = false;
		CHECK(Select(snapshot, settings, target));
		settings.ignoreTeam = true;
	}
	{
		nova::GameSnapshot snapshot = MakeSnapshot();
		nova::PlayerSnapshot occluded = RootPlayer(1000.0, 0.0, 0.0);
		occluded.visible = false;
		snapshot.players.push_back(occluded);

		nova::AimTarget target;
		CHECK(Select(snapshot, settings, target)); // fail-open when not filtered

		settings.visibleOnly = true;
		CHECK(!Select(snapshot, settings, target));
		settings.visibleOnly = false;
	}
}

NOVA_TEST(SelectAimTargetHonoursFovAndScreen) {
	nova::AimSelectionSettings settings = DefaultSelection();
	settings.fovPixels = 10.0;

	nova::GameSnapshot snapshot = MakeSnapshot();
	snapshot.players.push_back(RootPlayer(1000.0, 500.0, 0.0)); // ~25 px off centre
	nova::AimTarget target;
	CHECK(!Select(snapshot, settings, target));

	settings.fovPixels = 150.0;
	snapshot.players.clear();
	snapshot.players.push_back(RootPlayer(-1000.0, 0.0, 0.0)); // behind the camera
	CHECK(!Select(snapshot, settings, target));

	snapshot.players.clear();
	snapshot.players.push_back(RootPlayer(1000.0, 0.0, 0.0));
	CHECK(Select(snapshot, settings, target));
}

NOVA_TEST(SelectAimTargetPrefersHeadBone) {
	nova::GameSnapshot snapshot = MakeSnapshot();
	nova::PlayerSnapshot player;
	player.hasPose = true;
	player.bones.push_back(nova::BonePoint{ nova::FVector{ 1000.0, 0.0, 0.0 } });
	player.bones.push_back(nova::BonePoint{ nova::FVector{ 1000.0, 0.0, 50.0 } });
	player.bones.push_back(nova::BonePoint{ nova::FVector{ 1000.0, 0.0, 100.0 } });
	player.headBone = 2;
	snapshot.players.push_back(player);

	nova::AimTarget target;
	nova::AimSelectionSettings settings = DefaultSelection();
	CHECK(Select(snapshot, settings, target));
	CHECK(Near(target.world.z, 100.0));

	settings.boneMode = 1;
	CHECK(Select(snapshot, settings, target));
	CHECK(Near(target.world.z, 50.0));
}
