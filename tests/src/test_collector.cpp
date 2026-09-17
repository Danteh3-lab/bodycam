#include "test_framework.h"

#include "world_fixture.h"

#include "Offsets.hpp"
#include "mythos/NamePool.hpp"
#include "mythos/SnapshotCollector.hpp"
#include "mythos/WorldResolver.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

struct CollectorContext {
	CollectorContext() : names(fixture.memory), collector(fixture.memory, names) {}

	mythostest::WorldFixture fixture;
	mythos::NamePool names;
	mythos::SnapshotCollector collector;
	mythos::WorldResolver resolver{ fixture.memory, names };
};

mythos::CaptureSettings DefaultSettings() {
	mythos::CaptureSettings settings;
	settings.name = true;
	settings.health = true;
	settings.distance = true;
	settings.skeleton = true;
	settings.headDot = true;
	settings.boxFromBones = true;
	settings.showEnemy = true;
	settings.showTeam = false;
	settings.showDrones = false;
	settings.hideDead = true;
	settings.maxDistanceMeters = 300.0;
	return settings;
}

} // namespace

MYTHOS_TEST(CaptureFiltersEveryCategory) {
	CollectorContext context;
	mythostest::WorldFixture& fixture = context.fixture;

	fixture.AddPlayer(1, 80.0f, 100.0f, "BP_Character_C", mythos::FVector{ 500.0, 0.0, 100.0 }, true, "Enemy");
	fixture.AddPlayer(0, 100.0f, 100.0f, "BP_Character_C", mythos::FVector{ 500.0, 500.0, 100.0 }, true, "Team");
	fixture.AddPlayer(1, 0.0f, 100.0f, "BP_Character_C", mythos::FVector{ 400.0, 0.0, 100.0 }, true, "Dead");
	fixture.AddDrone("Drone", mythos::FVector{ 300.0, 0.0, 100.0 });
	fixture.AddPlayer(1, 100.0f, 100.0f, "BP_Character_C", mythos::FVector{ 0.0, 40000.0, 100.0 }, true, "Far");

	CHECK(context.resolver.Resolve());
	CHECK(context.names.Attach(fixture.names.address()));

	const mythos::GameSnapshotPtr snapshot =
		context.collector.Capture(context.resolver.context(), mythos::ResolveStage::Ok,
		                          DefaultSettings(), 1, 0);

	CHECK(snapshot->valid);
	CHECK(snapshot->camera.valid);
	CHECK_EQ(snapshot->players.size(), static_cast<size_t>(1));
	CHECK_EQ(snapshot->players[0].name, std::string("Enemy"));
	CHECK_EQ(snapshot->counters.roster, 6);
	CHECK_EQ(snapshot->counters.self, 1);
	CHECK_EQ(snapshot->counters.teamFiltered, 1);
	CHECK_EQ(snapshot->counters.dead, 1);
	CHECK_EQ(snapshot->counters.drones, 1);
	CHECK_EQ(snapshot->counters.droneFiltered, 1);
	CHECK_EQ(snapshot->counters.tooFar, 1);
	CHECK_EQ(snapshot->counters.drawn, 1);
	CHECK_EQ(snapshot->counters.rejected(), 5);
}

MYTHOS_TEST(CaptureKeepsDronesWhenEnabled) {
	CollectorContext context;
	context.fixture.AddDrone("Drone", mythos::FVector{ 300.0, 0.0, 100.0 });

	CHECK(context.resolver.Resolve());
	CHECK(context.names.Attach(context.fixture.names.address()));

	mythos::CaptureSettings settings = DefaultSettings();
	settings.showDrones = true;
	settings.boxFromBones = false;

	const mythos::GameSnapshotPtr snapshot =
		context.collector.Capture(context.resolver.context(), mythos::ResolveStage::Ok, settings, 1, 0);

	CHECK_EQ(snapshot->players.size(), static_cast<size_t>(1));
	CHECK(snapshot->players[0].isDrone());
	CHECK_EQ(snapshot->players[0].kind, mythos::PlayerKind::Drone);
	CHECK(!snapshot->players[0].hasHealth);
	CHECK(snapshot->players[0].hasCapsule);
}

MYTHOS_TEST(CaptureRetainsAimCandidatesBeyondEspFilters) {
	CollectorContext context;
	context.fixture.AddPlayer(1, 80.0f, 100.0f, "BP_Character_C",
		                          mythos::FVector{ 500.0, 0.0, 100.0 }, true, "Enemy");
	context.fixture.AddPlayer(0, 100.0f, 100.0f, "BP_Character_C",
		                          mythos::FVector{ 500.0, 500.0, 100.0 }, true, "Team");
	context.fixture.AddPlayer(1, 0.0f, 100.0f, "BP_Character_C",
		                          mythos::FVector{ 400.0, 0.0, 100.0 }, true, "Dead");
	context.fixture.AddDrone("Drone", mythos::FVector{ 300.0, 0.0, 100.0 });
	context.fixture.AddPlayer(1, 100.0f, 100.0f, "BP_Character_C",
	                          mythos::FVector{ 0.0, 40000.0, 100.0 }, true, "Far");

	CHECK(context.resolver.Resolve());
	CHECK(context.names.Attach(context.fixture.names.address()));

	mythos::CaptureSettings settings = DefaultSettings();
	settings.retainAimCandidates = true;
	settings.boxFromBones = false;
	settings.skeleton = false;
	settings.headDot = false;

	const mythos::GameSnapshotPtr snapshot =
		context.collector.Capture(context.resolver.context(), mythos::ResolveStage::Ok,
		                          settings, 1, 0);

	CHECK(snapshot->valid);
	CHECK_EQ(snapshot->players.size(), static_cast<size_t>(5));
	CHECK_EQ(snapshot->counters.teamFiltered, 0);
	CHECK_EQ(snapshot->counters.dead, 0);
	CHECK_EQ(snapshot->counters.droneFiltered, 0);
	CHECK_EQ(snapshot->counters.tooFar, 0);
	CHECK_EQ(snapshot->counters.drawn, 5);
	CHECK(std::any_of(snapshot->players.begin(), snapshot->players.end(),
	                  [](const mythos::PlayerSnapshot& player) {
		                  return player.hasPose && player.headBone >= 0;
	                  }));
}

MYTHOS_TEST(CaptureHealthAndDistance) {
	CollectorContext context;
	context.fixture.AddPlayer(1, 42.0f, 120.0f, "BP_Character_C",
	                          mythos::FVector{ 1000.0, 0.0, 100.0 }, true, "Enemy");

	CHECK(context.resolver.Resolve());
	CHECK(context.names.Attach(context.fixture.names.address()));

	const mythos::GameSnapshotPtr snapshot =
		context.collector.Capture(context.resolver.context(), mythos::ResolveStage::Ok,
		                          DefaultSettings(), 1, 0);

	CHECK_EQ(snapshot->players.size(), static_cast<size_t>(1));
	const mythos::PlayerSnapshot& player = snapshot->players[0];
	CHECK(player.hasHealth);
	CHECK(std::abs(player.health - 42.0f) < 1e-3);
	CHECK(std::abs(player.maxHealth - 120.0f) < 1e-3);
	CHECK(std::abs(player.distanceMeters - 10.0) < 0.1);
	CHECK(player.hasTeam);
	CHECK_EQ(player.teamId, 1);
	CHECK(!player.sameTeam);
}

MYTHOS_TEST(CapturePoseAndSkeleton) {
	CollectorContext context;
	context.fixture.AddPlayer(1, 100.0f, 100.0f, "BP_Character_C",
	                          mythos::FVector{ 500.0, 0.0, 100.0 }, true, "Enemy");

	CHECK(context.resolver.Resolve());
	CHECK(context.names.Attach(context.fixture.names.address()));

	const mythos::GameSnapshotPtr snapshot =
		context.collector.Capture(context.resolver.context(), mythos::ResolveStage::Ok,
		                          DefaultSettings(), 1, 0);

	CHECK_EQ(snapshot->players.size(), static_cast<size_t>(1));
	const mythos::PlayerSnapshot& player = snapshot->players[0];
	CHECK(player.hasPose);
	CHECK(player.poseNamed);
	CHECK_EQ(player.bones.size(), static_cast<size_t>(9));
	CHECK_EQ(player.headBone, 4); // "head" bone in the fixture skeleton
	CHECK_EQ(player.bones[0].parent, -1);
	CHECK_EQ(player.bones[4].parent, 3);
	CHECK(player.bones[4].core != 0);
	CHECK(context.collector.skeletons().refSkeletonFound());
	CHECK_EQ(context.collector.skeletons().refSkeletonOffset(), 0);
	CHECK_EQ(context.collector.skeletons().namedCount(), 1);
}

MYTHOS_TEST(CaptureRejectsMismatchedMesh) {
	CollectorContext context;
	const uintptr_t playerState = context.fixture.AddPlayer(
		1, 100.0f, 100.0f, "BP_Character_C", mythos::FVector{ 500.0, 0.0, 100.0 }, true, "Enemy");
	(void)playerState;

	CHECK(context.resolver.Resolve());
	CHECK(context.names.Attach(context.fixture.names.address()));

	// Move the mesh component far away from its pawn root.
	// The pawn is the last roster entry's pawn.
	uintptr_t pawn = 0;
	CHECK(context.fixture.memory.readPointer(
		context.fixture.rosterEntries().back() + Offsets::PSPawn, pawn));
	uintptr_t mesh = 0;
	CHECK(context.fixture.memory.readPointer(pawn + Offsets::SkeletalMeshComponent, mesh));
	mythostest::WriteTransform(context.fixture.memory, mesh + Offsets::ComponentToWorld,
	                         mythos::FVector{ 999999.0, 0.0, 0.0 });

	const mythos::GameSnapshotPtr snapshot =
		context.collector.Capture(context.resolver.context(), mythos::ResolveStage::Ok,
		                          DefaultSettings(), 1, 0);
	CHECK_EQ(snapshot->players.size(), static_cast<size_t>(1));
	CHECK(!snapshot->players[0].hasPose);
	CHECK(context.collector.diagnostics().bones.badMesh >= 1);
}

MYTHOS_TEST(CaptureCameraFallbackFov) {
	CollectorContext context;
	context.fixture.ClearCamera();
	CHECK(context.resolver.Resolve());
	CHECK(context.names.Attach(context.fixture.names.address()));

	const mythos::GameSnapshotPtr snapshot =
		context.collector.Capture(context.resolver.context(), mythos::ResolveStage::Ok,
		                          DefaultSettings(), 1, 0);

	CHECK(snapshot->valid);
	CHECK(snapshot->camera.valid);
	CHECK(snapshot->camera.usedFallbackFov);
}

MYTHOS_TEST(CaptureInvalidWorldProducesInvalidSnapshot) {
	CollectorContext context;
	const mythos::WorldContext empty;
	const mythos::GameSnapshotPtr snapshot =
		context.collector.Capture(empty, mythos::ResolveStage::NoGWorld, DefaultSettings(), 1, 0);
	CHECK(!snapshot->valid);
	CHECK(!snapshot->camera.valid);
}

MYTHOS_TEST(SkeletonCacheSharedAcrossPawns) {
	CollectorContext context;
	context.fixture.AddPlayer(1, 100.0f, 100.0f, "BP_Character_C",
	                          mythos::FVector{ 500.0, 0.0, 100.0 }, true, "A");
	context.fixture.AddPlayer(1, 100.0f, 100.0f, "BP_Character_C",
	                          mythos::FVector{ 800.0, 0.0, 100.0 }, true, "B");

	CHECK(context.resolver.Resolve());
	CHECK(context.names.Attach(context.fixture.names.address()));

	const mythos::GameSnapshotPtr snapshot =
		context.collector.Capture(context.resolver.context(), mythos::ResolveStage::Ok,
		                          DefaultSettings(), 1, 0);
	CHECK_EQ(snapshot->players.size(), static_cast<size_t>(2));
	CHECK_EQ(context.collector.skeletons().size(), static_cast<size_t>(1));
}

MYTHOS_TEST(ClassifyByClassNameRules) {
	CHECK(mythos::ClassifyByClassName("BP_Drone_C") == mythos::PlayerKind::Drone);
	CHECK(mythos::ClassifyByClassName("Perk_Scanner") == mythos::PlayerKind::Drone);
	CHECK(mythos::ClassifyByClassName("BP_Character_C") == mythos::PlayerKind::Player);
	CHECK(mythos::ClassifyByClassName("Pawn") == mythos::PlayerKind::Player);
	CHECK(mythos::ClassifyByClassName("Actor") == mythos::PlayerKind::Unknown);
	CHECK(mythos::ClassifyByClassName("") == mythos::PlayerKind::Unknown);
	CHECK(mythos::ClassifyByClassName(nullptr) == mythos::PlayerKind::Unknown);
}

namespace {

struct FakeVisibilityProbe final : mythos::VisibilityProbe {
	bool activeFlag = true;
	bool result = false;
	mutable int calls = 0;

	[[nodiscard]] bool active() const override { return activeFlag; }
	[[nodiscard]] bool IsVisible(uintptr_t, const mythos::FVector&) const override {
		++calls;
		return result;
	}
};

} // namespace

MYTHOS_TEST(CaptureAppliesVisibilityProbe) {
	mythostest::WorldFixture fixture;
	mythos::NamePool names(fixture.memory);
	FakeVisibilityProbe probe;
	mythos::SnapshotCollector collector(fixture.memory, names, &probe);
	mythos::WorldResolver resolver{ fixture.memory, names };

	fixture.AddPlayer(1, 100.0f, 100.0f, "BP_Character_C",
	                  mythos::FVector{ 500.0, 0.0, 100.0 }, true, "Enemy");
	CHECK(resolver.Resolve());
	CHECK(names.Attach(fixture.names.address()));

	mythos::CaptureSettings settings = DefaultSettings();

	// Not requested: the probe is never consulted; visibility stays fail-open.
	{
		const mythos::GameSnapshotPtr snapshot =
			collector.Capture(resolver.context(), mythos::ResolveStage::Ok, settings, 1, 0);
		CHECK_EQ(probe.calls, 0);
		CHECK_EQ(snapshot->players.size(), static_cast<size_t>(1));
		CHECK(!snapshot->players.empty() && snapshot->players[0].visible);
		CHECK_EQ(snapshot->counters.occluded, 0);
	}

	// Requested but inactive: still fail-open.
	settings.visibility = true;
	probe.activeFlag = false;
	{
		const mythos::GameSnapshotPtr snapshot =
			collector.Capture(resolver.context(), mythos::ResolveStage::Ok, settings, 2, 0);
		CHECK_EQ(probe.calls, 0);
		CHECK_EQ(snapshot->players.size(), static_cast<size_t>(1));
		CHECK(!snapshot->players.empty() && snapshot->players[0].visible);
	}

	// Requested and active: the probe result is stamped into the snapshot.
	probe.activeFlag = true;
	probe.result = false;
	{
		const mythos::GameSnapshotPtr snapshot =
			collector.Capture(resolver.context(), mythos::ResolveStage::Ok, settings, 3, 0);
		CHECK_EQ(probe.calls, 1);
		CHECK_EQ(snapshot->players.size(), static_cast<size_t>(1));
		CHECK(!snapshot->players.empty() && !snapshot->players[0].visible);
		CHECK_EQ(snapshot->counters.occluded, 1);
	}
}
