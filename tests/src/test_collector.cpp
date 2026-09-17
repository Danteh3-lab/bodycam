#include "test_framework.h"

#include "world_fixture.h"

#include "Offsets.hpp"
#include "nova/NamePool.hpp"
#include "nova/SnapshotCollector.hpp"
#include "nova/WorldResolver.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

struct CollectorContext {
	CollectorContext() : names(fixture.memory), collector(fixture.memory, names) {}

	novatest::WorldFixture fixture;
	nova::NamePool names;
	nova::SnapshotCollector collector;
	nova::WorldResolver resolver{ fixture.memory, names };
};

nova::CaptureSettings DefaultSettings() {
	nova::CaptureSettings settings;
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

NOVA_TEST(CaptureFiltersEveryCategory) {
	CollectorContext context;
	novatest::WorldFixture& fixture = context.fixture;

	fixture.AddPlayer(1, 80.0f, 100.0f, "BP_Character_C", nova::FVector{ 500.0, 0.0, 100.0 }, true, "Enemy");
	fixture.AddPlayer(0, 100.0f, 100.0f, "BP_Character_C", nova::FVector{ 500.0, 500.0, 100.0 }, true, "Team");
	fixture.AddPlayer(1, 0.0f, 100.0f, "BP_Character_C", nova::FVector{ 400.0, 0.0, 100.0 }, true, "Dead");
	fixture.AddDrone("Drone", nova::FVector{ 300.0, 0.0, 100.0 });
	fixture.AddPlayer(1, 100.0f, 100.0f, "BP_Character_C", nova::FVector{ 0.0, 40000.0, 100.0 }, true, "Far");

	CHECK(context.resolver.Resolve());
	CHECK(context.names.Attach(fixture.names.address()));

	const nova::GameSnapshotPtr snapshot =
		context.collector.Capture(context.resolver.context(), nova::ResolveStage::Ok,
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

NOVA_TEST(CaptureKeepsDronesWhenEnabled) {
	CollectorContext context;
	context.fixture.AddDrone("Drone", nova::FVector{ 300.0, 0.0, 100.0 });

	CHECK(context.resolver.Resolve());
	CHECK(context.names.Attach(context.fixture.names.address()));

	nova::CaptureSettings settings = DefaultSettings();
	settings.showDrones = true;
	settings.boxFromBones = false;

	const nova::GameSnapshotPtr snapshot =
		context.collector.Capture(context.resolver.context(), nova::ResolveStage::Ok, settings, 1, 0);

	CHECK_EQ(snapshot->players.size(), static_cast<size_t>(1));
	CHECK(snapshot->players[0].isDrone());
	CHECK_EQ(snapshot->players[0].kind, nova::PlayerKind::Drone);
	CHECK(!snapshot->players[0].hasHealth);
	CHECK(snapshot->players[0].hasCapsule);
}

NOVA_TEST(CaptureRetainsAimCandidatesBeyondEspFilters) {
	CollectorContext context;
	context.fixture.AddPlayer(1, 80.0f, 100.0f, "BP_Character_C",
		                          nova::FVector{ 500.0, 0.0, 100.0 }, true, "Enemy");
	context.fixture.AddPlayer(0, 100.0f, 100.0f, "BP_Character_C",
		                          nova::FVector{ 500.0, 500.0, 100.0 }, true, "Team");
	context.fixture.AddPlayer(1, 0.0f, 100.0f, "BP_Character_C",
		                          nova::FVector{ 400.0, 0.0, 100.0 }, true, "Dead");
	context.fixture.AddDrone("Drone", nova::FVector{ 300.0, 0.0, 100.0 });
	context.fixture.AddPlayer(1, 100.0f, 100.0f, "BP_Character_C",
	                          nova::FVector{ 0.0, 40000.0, 100.0 }, true, "Far");

	CHECK(context.resolver.Resolve());
	CHECK(context.names.Attach(context.fixture.names.address()));

	nova::CaptureSettings settings = DefaultSettings();
	settings.retainAimCandidates = true;
	settings.boxFromBones = false;
	settings.skeleton = false;
	settings.headDot = false;

	const nova::GameSnapshotPtr snapshot =
		context.collector.Capture(context.resolver.context(), nova::ResolveStage::Ok,
		                          settings, 1, 0);

	CHECK(snapshot->valid);
	CHECK_EQ(snapshot->players.size(), static_cast<size_t>(5));
	CHECK_EQ(snapshot->counters.teamFiltered, 0);
	CHECK_EQ(snapshot->counters.dead, 0);
	CHECK_EQ(snapshot->counters.droneFiltered, 0);
	CHECK_EQ(snapshot->counters.tooFar, 0);
	CHECK_EQ(snapshot->counters.drawn, 5);
	CHECK(std::any_of(snapshot->players.begin(), snapshot->players.end(),
	                  [](const nova::PlayerSnapshot& player) {
		                  return player.hasPose && player.headBone >= 0;
	                  }));
}

NOVA_TEST(CaptureHealthAndDistance) {
	CollectorContext context;
	context.fixture.AddPlayer(1, 42.0f, 120.0f, "BP_Character_C",
	                          nova::FVector{ 1000.0, 0.0, 100.0 }, true, "Enemy");

	CHECK(context.resolver.Resolve());
	CHECK(context.names.Attach(context.fixture.names.address()));

	const nova::GameSnapshotPtr snapshot =
		context.collector.Capture(context.resolver.context(), nova::ResolveStage::Ok,
		                          DefaultSettings(), 1, 0);

	CHECK_EQ(snapshot->players.size(), static_cast<size_t>(1));
	const nova::PlayerSnapshot& player = snapshot->players[0];
	CHECK(player.hasHealth);
	CHECK(std::abs(player.health - 42.0f) < 1e-3);
	CHECK(std::abs(player.maxHealth - 120.0f) < 1e-3);
	CHECK(std::abs(player.distanceMeters - 10.0) < 0.1);
	CHECK(player.hasTeam);
	CHECK_EQ(player.teamId, 1);
	CHECK(!player.sameTeam);
}

NOVA_TEST(CapturePoseAndSkeleton) {
	CollectorContext context;
	context.fixture.AddPlayer(1, 100.0f, 100.0f, "BP_Character_C",
	                          nova::FVector{ 500.0, 0.0, 100.0 }, true, "Enemy");

	CHECK(context.resolver.Resolve());
	CHECK(context.names.Attach(context.fixture.names.address()));

	const nova::GameSnapshotPtr snapshot =
		context.collector.Capture(context.resolver.context(), nova::ResolveStage::Ok,
		                          DefaultSettings(), 1, 0);

	CHECK_EQ(snapshot->players.size(), static_cast<size_t>(1));
	const nova::PlayerSnapshot& player = snapshot->players[0];
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

NOVA_TEST(CaptureRejectsMismatchedMesh) {
	CollectorContext context;
	const uintptr_t playerState = context.fixture.AddPlayer(
		1, 100.0f, 100.0f, "BP_Character_C", nova::FVector{ 500.0, 0.0, 100.0 }, true, "Enemy");
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
	novatest::WriteTransform(context.fixture.memory, mesh + Offsets::ComponentToWorld,
	                         nova::FVector{ 999999.0, 0.0, 0.0 });

	const nova::GameSnapshotPtr snapshot =
		context.collector.Capture(context.resolver.context(), nova::ResolveStage::Ok,
		                          DefaultSettings(), 1, 0);
	CHECK_EQ(snapshot->players.size(), static_cast<size_t>(1));
	CHECK(!snapshot->players[0].hasPose);
	CHECK(context.collector.diagnostics().bones.badMesh >= 1);
}

NOVA_TEST(CaptureCameraFallbackFov) {
	CollectorContext context;
	context.fixture.ClearCamera();
	CHECK(context.resolver.Resolve());
	CHECK(context.names.Attach(context.fixture.names.address()));

	const nova::GameSnapshotPtr snapshot =
		context.collector.Capture(context.resolver.context(), nova::ResolveStage::Ok,
		                          DefaultSettings(), 1, 0);

	CHECK(snapshot->valid);
	CHECK(snapshot->camera.valid);
	CHECK(snapshot->camera.usedFallbackFov);
}

NOVA_TEST(CaptureInvalidWorldProducesInvalidSnapshot) {
	CollectorContext context;
	const nova::WorldContext empty;
	const nova::GameSnapshotPtr snapshot =
		context.collector.Capture(empty, nova::ResolveStage::NoGWorld, DefaultSettings(), 1, 0);
	CHECK(!snapshot->valid);
	CHECK(!snapshot->camera.valid);
}

NOVA_TEST(SkeletonCacheSharedAcrossPawns) {
	CollectorContext context;
	context.fixture.AddPlayer(1, 100.0f, 100.0f, "BP_Character_C",
	                          nova::FVector{ 500.0, 0.0, 100.0 }, true, "A");
	context.fixture.AddPlayer(1, 100.0f, 100.0f, "BP_Character_C",
	                          nova::FVector{ 800.0, 0.0, 100.0 }, true, "B");

	CHECK(context.resolver.Resolve());
	CHECK(context.names.Attach(context.fixture.names.address()));

	const nova::GameSnapshotPtr snapshot =
		context.collector.Capture(context.resolver.context(), nova::ResolveStage::Ok,
		                          DefaultSettings(), 1, 0);
	CHECK_EQ(snapshot->players.size(), static_cast<size_t>(2));
	CHECK_EQ(context.collector.skeletons().size(), static_cast<size_t>(1));
}

NOVA_TEST(ClassifyByClassNameRules) {
	CHECK(nova::ClassifyByClassName("BP_Drone_C") == nova::PlayerKind::Drone);
	CHECK(nova::ClassifyByClassName("Perk_Scanner") == nova::PlayerKind::Drone);
	CHECK(nova::ClassifyByClassName("BP_Character_C") == nova::PlayerKind::Player);
	CHECK(nova::ClassifyByClassName("Pawn") == nova::PlayerKind::Player);
	CHECK(nova::ClassifyByClassName("Actor") == nova::PlayerKind::Unknown);
	CHECK(nova::ClassifyByClassName("") == nova::PlayerKind::Unknown);
	CHECK(nova::ClassifyByClassName(nullptr) == nova::PlayerKind::Unknown);
}

namespace {

struct FakeVisibilityProbe final : nova::VisibilityProbe {
	bool activeFlag = true;
	bool result = false;
	mutable int calls = 0;

	[[nodiscard]] bool active() const override { return activeFlag; }
	[[nodiscard]] bool IsVisible(uintptr_t, const nova::FVector&) const override {
		++calls;
		return result;
	}
};

} // namespace

NOVA_TEST(CaptureAppliesVisibilityProbe) {
	novatest::WorldFixture fixture;
	nova::NamePool names(fixture.memory);
	FakeVisibilityProbe probe;
	nova::SnapshotCollector collector(fixture.memory, names, &probe);
	nova::WorldResolver resolver{ fixture.memory, names };

	fixture.AddPlayer(1, 100.0f, 100.0f, "BP_Character_C",
	                  nova::FVector{ 500.0, 0.0, 100.0 }, true, "Enemy");
	CHECK(resolver.Resolve());
	CHECK(names.Attach(fixture.names.address()));

	nova::CaptureSettings settings = DefaultSettings();

	// Not requested: the probe is never consulted; visibility stays fail-open.
	{
		const nova::GameSnapshotPtr snapshot =
			collector.Capture(resolver.context(), nova::ResolveStage::Ok, settings, 1, 0);
		CHECK_EQ(probe.calls, 0);
		CHECK_EQ(snapshot->players.size(), static_cast<size_t>(1));
		CHECK(!snapshot->players.empty() && snapshot->players[0].visible);
		CHECK_EQ(snapshot->counters.occluded, 0);
	}

	// Requested but inactive: still fail-open.
	settings.visibility = true;
	probe.activeFlag = false;
	{
		const nova::GameSnapshotPtr snapshot =
			collector.Capture(resolver.context(), nova::ResolveStage::Ok, settings, 2, 0);
		CHECK_EQ(probe.calls, 0);
		CHECK_EQ(snapshot->players.size(), static_cast<size_t>(1));
		CHECK(!snapshot->players.empty() && snapshot->players[0].visible);
	}

	// Requested and active: the probe result is stamped into the snapshot.
	probe.activeFlag = true;
	probe.result = false;
	{
		const nova::GameSnapshotPtr snapshot =
			collector.Capture(resolver.context(), nova::ResolveStage::Ok, settings, 3, 0);
		CHECK_EQ(probe.calls, 1);
		CHECK_EQ(snapshot->players.size(), static_cast<size_t>(1));
		CHECK(!snapshot->players.empty() && !snapshot->players[0].visible);
		CHECK_EQ(snapshot->counters.occluded, 1);
	}
}
