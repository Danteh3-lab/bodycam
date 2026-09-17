#include "test_framework.h"

#include "world_fixture.h"

#include "Offsets.hpp"
#include "mythos/NamePool.hpp"
#include "mythos/WorldResolver.hpp"

#include <cstring>

MYTHOS_TEST(ResolveFromKnownRva) {
	mythostest::WorldFixture fixture;
	mythos::NamePool names(fixture.memory);
	mythos::WorldResolver resolver(fixture.memory, names);

	CHECK(resolver.Resolve());
	CHECK(resolver.stage() == mythos::ResolveStage::Ok);
	CHECK(resolver.context().valid);
	CHECK(resolver.context().proven);
	CHECK_EQ(resolver.context().world, fixture.world());
	CHECK_EQ(resolver.context().playerController, fixture.playerController());
	CHECK_EQ(resolver.context().cameraManager, fixture.cameraManager());
	CHECK_EQ(resolver.context().playerCount, 1);
	CHECK_EQ(resolver.context().localTeam, 0);
	CHECK(resolver.context().acknowledgedPawn == fixture.localPawn());
	CHECK(resolver.diagnostics().worldFromRva);
	CHECK_EQ(resolver.diagnostics().readFailures, 0u);
}

MYTHOS_TEST(UnprovenWorldIsReported) {
	mythostest::WorldFixture fixture;
	fixture.MakeWorldUnproven();

	mythos::NamePool names(fixture.memory);
	mythos::WorldResolver resolver(fixture.memory, names);

	CHECK(resolver.Resolve());
	CHECK(resolver.stage() == mythos::ResolveStage::WorldUnproven);
	CHECK(!resolver.context().proven);
}

MYTHOS_TEST(ChainStageFailures) {
	{
		mythostest::WorldFixture fixture;
		const uintptr_t value = 0;
		fixture.memory.WritePointer(fixture.gworldSlot(), value);
		mythos::NamePool names(fixture.memory);
		mythos::WorldResolver resolver(fixture.memory, names);
		CHECK(!resolver.Resolve());
		CHECK(resolver.stage() == mythos::ResolveStage::NoGWorld);
	}
	{
		mythostest::WorldFixture fixture;
		fixture.memory.WritePointer(fixture.localPlayer() + Offsets::LPPlayerController, 0);
		mythos::NamePool names(fixture.memory);
		mythos::WorldResolver resolver(fixture.memory, names);
		CHECK(!resolver.Resolve());
		CHECK(resolver.stage() == mythos::ResolveStage::NoPlayerController);
	}
	{
		mythostest::WorldFixture fixture;
		fixture.memory.WritePointer(fixture.playerController() + Offsets::CameraManager, 0);
		mythos::NamePool names(fixture.memory);
		mythos::WorldResolver resolver(fixture.memory, names);
		CHECK(!resolver.Resolve());
		CHECK(resolver.stage() == mythos::ResolveStage::NoCameraManager);
	}
	{
		mythostest::WorldFixture fixture;
		fixture.memory.WritePointer(fixture.world() + Offsets::GameState, 0);
		mythos::NamePool names(fixture.memory);
		mythos::WorldResolver resolver(fixture.memory, names);
		CHECK(!resolver.Resolve());
		CHECK(resolver.stage() == mythos::ResolveStage::NoGameState);
	}
	{
		mythostest::WorldFixture fixture;
		fixture.memory.WriteInt32(fixture.gameState() + Offsets::PlayerArray + 0x08, -1);
		mythos::NamePool names(fixture.memory);
		mythos::WorldResolver resolver(fixture.memory, names);
		CHECK(!resolver.Resolve());
		CHECK(resolver.stage() == mythos::ResolveStage::NoPlayerArray);
	}
	{
		mythostest::WorldFixture fixture;
		fixture.memory.WritePointer(fixture.playerController() + Offsets::AcknowledgedPawn, 0);
		mythos::NamePool names(fixture.memory);
		mythos::WorldResolver resolver(fixture.memory, names);
		CHECK(!resolver.Resolve());
		CHECK(resolver.stage() == mythos::ResolveStage::NoLocalPawn);
	}
}

MYTHOS_TEST(LocalPlayerElementFailure) {
	mythostest::WorldFixture fixture;
	const uintptr_t emptyArray = fixture.memory.Allocate(0x10);
	fixture.memory.WritePointer(emptyArray, 0);
	fixture.memory.WritePointer(fixture.gameInstance() + Offsets::LocalPlayer + 0x00, emptyArray);
	fixture.memory.WriteInt32(fixture.gameInstance() + Offsets::LocalPlayer + 0x08, 1);
	fixture.memory.WriteInt32(fixture.gameInstance() + Offsets::LocalPlayer + 0x0C, 1);

	mythos::NamePool names(fixture.memory);
	mythos::WorldResolver resolver(fixture.memory, names);
	CHECK(!resolver.Resolve());
	CHECK(resolver.stage() == mythos::ResolveStage::NoLocalPlayer);
}

MYTHOS_TEST(WorldAnchorFallbackScanFindsSlot) {
	mythostest::WorldFixture fixture;

	// Move the world pointer off the known slot into the data section.
	const uintptr_t dataSection = fixture.dataSectionBase();
	fixture.memory.WritePointer(dataSection + 0x800, fixture.world());
	fixture.memory.WritePointer(fixture.gworldSlot(), 0);

	mythos::NamePool names(fixture.memory);
	mythos::ResolverOptions options;
	options.useKnownRva = false;
	mythos::WorldResolver resolver(fixture.memory, names, options);

	CHECK(!resolver.Resolve());
	CHECK(resolver.stage() == mythos::ResolveStage::NoGWorld);

	CHECK(resolver.PumpFallback(0));
	CHECK(resolver.stage() == mythos::ResolveStage::Ok);
	CHECK(!resolver.diagnostics().worldFromRva);
	CHECK_EQ(resolver.diagnostics().anchorSlot, dataSection + 0x800);
	CHECK(resolver.diagnostics().fullScans >= 1);
}

MYTHOS_TEST(RescanRequestDoesNotRestartScanMidPass) {
	// Regression: Resolve() failing on every tick used to re-arm a rescan and
	// reset the scan cursor before it could ever reach the end of the data
	// sections. With a dead known-RVA slot the fallback must still complete.
	mythostest::WorldFixture fixture;

	const uintptr_t bigStart = fixture.moduleBase() + 0x09200000;
	const size_t bigSize = 0x40000; // 256 KiB, several chunks
	fixture.memory.AddRegion(bigStart, bigSize);
	fixture.memory.AddSection(bigStart, bigSize, false, true);
	fixture.memory.WritePointer(bigStart + 0x20000, fixture.world());

	// Kill the known-RVA anchor so Resolve() fails every tick.
	fixture.memory.WritePointer(fixture.gworldSlot(), 0);

	mythos::NamePool names(fixture.memory);
	mythos::ResolverOptions options;
	options.fallbackStepBytes = Offsets::Scan::ChunkBytes; // one chunk per call
	mythos::WorldResolver resolver(fixture.memory, names, options);

	bool found = false;
	for (int tick = 0; tick < 64 && !found; ++tick) {
		(void)resolver.Resolve();
		found = resolver.PumpFallback(static_cast<uint64_t>(tick) * 10);
	}
	CHECK(found);
	CHECK(resolver.stage() == mythos::ResolveStage::Ok);
	CHECK_EQ(resolver.context().world, fixture.world());
}

MYTHOS_TEST(KnownNamePoolRvaResolvesWithWorld) {
	mythostest::WorldFixture fixture;

	// Place a valid pool at the known GNames RVA.
	const uintptr_t hint = fixture.moduleBase() + Offsets::Globals::GNames;
	fixture.memory.AddRegion(hint - 0x40, 0x100);
	const uintptr_t block = fixture.memory.Allocate(0x1000);
	const uint16_t header = static_cast<uint16_t>(4 << 6); // "None"
	fixture.memory.WriteValue<uint16_t>(block, header);
	fixture.memory.Write(block + 2, "None", 4);
	fixture.memory.WritePointer(hint + Offsets::NamePool::BlocksOffset, block);

	mythos::NamePool names(fixture.memory);
	mythos::WorldResolver resolver(fixture.memory, names);

	CHECK(resolver.Resolve());
	CHECK(names.ready());
	CHECK_EQ(names.poolAddress(), hint);
	CHECK(resolver.diagnostics().namesFromRva);
}

MYTHOS_TEST(NamePoolSignatureFallback) {
	mythostest::WorldFixture fixture;

	// Fabricate the FNamePool signature in the code section.
	const uintptr_t code = fixture.moduleBase() + 0x1000;
	unsigned char pattern[33] = {
		0x74, 0x09, 0x4C, 0x8D, 0x05, 0, 0, 0, 0,
		0xEB, 0x16, 0x48, 0x8D, 0x0D, 0, 0, 0, 0,
		0xE8, 0x11, 0x22, 0x33, 0x44,
		0x4C, 0x8B, 0xC0, 0xC6, 0x05, 0x55, 0x66, 0x77, 0x88, 0x01,
	};
	const uintptr_t patternAddress = code + 0x100;
	const int32_t relative1 = static_cast<int32_t>(
		static_cast<intptr_t>(fixture.names.address()) -
		static_cast<intptr_t>(patternAddress + 9));
	const int32_t relative2 = static_cast<int32_t>(
		static_cast<intptr_t>(fixture.names.address()) -
		static_cast<intptr_t>(patternAddress + 18));
	std::memcpy(pattern + 5, &relative1, sizeof(relative1));
	std::memcpy(pattern + 14, &relative2, sizeof(relative2));
	fixture.memory.Write(patternAddress, pattern, sizeof(pattern));

	mythos::NamePool names(fixture.memory);
	mythos::WorldResolver resolver(fixture.memory, names);
	CHECK(!names.ready());

	CHECK(resolver.PumpFallback(0));
	CHECK(names.ready());
	CHECK_EQ(names.poolAddress(), fixture.names.address());
	CHECK(!resolver.diagnostics().namesFromRva);
}

MYTHOS_TEST(OuterWorldRescueAndReanchor) {
	mythostest::WorldFixture fixture;
	fixture.MakeWorldUnproven();

	const uintptr_t alternateWorld = fixture.CreateAlternateWorld(true);
	// Place the alternate world pointer in the data section so the re-anchor
	// search can find its slot.
	const uintptr_t dataSection = fixture.dataSectionBase();
	fixture.memory.WritePointer(dataSection + 0x400, alternateWorld);

	mythos::NamePool names(fixture.memory);
	mythos::WorldResolver resolver(fixture.memory, names);

	CHECK(resolver.Resolve());
	CHECK(resolver.stage() == mythos::ResolveStage::Ok);
	CHECK_EQ(resolver.context().world, alternateWorld);
	CHECK_EQ(resolver.diagnostics().rescues, 1);

	CHECK(resolver.PumpFallback(0));
	CHECK_EQ(resolver.diagnostics().reanchors, 1);
}

MYTHOS_TEST(MapTransitionKeepsResolving) {
	mythostest::WorldFixture fixture;
	mythos::NamePool names(fixture.memory);
	mythos::WorldResolver resolver(fixture.memory, names);

	CHECK(resolver.Resolve());
	resolver.OnMapTransition();
	CHECK(resolver.Resolve());
	CHECK(resolver.stage() == mythos::ResolveStage::Ok);
	CHECK_EQ(resolver.diagnostics().failStreak, 0);
}

MYTHOS_TEST(OffsetsInvalidFailsClosed) {
	mythostest::WorldFixture fixture;
	mythos::NamePool names(fixture.memory);
	mythos::WorldResolver resolver(fixture.memory, names);

	resolver.MarkOffsetsInvalid();
	CHECK(!resolver.Resolve());
	CHECK(resolver.stage() == mythos::ResolveStage::OffsetsInvalid);
	CHECK(!resolver.PumpFallback(0));
	CHECK(!resolver.PumpFallback(100000));
	CHECK(resolver.stage() == mythos::ResolveStage::OffsetsInvalid);
}
