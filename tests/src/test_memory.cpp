#include "test_framework.h"

#include "fake_memory.h"

#include "Offsets.hpp"
#include "mythos/ReadOnlyMemory.hpp"
#include "mythos/UnrealTypes.hpp"

#include <cmath>
#include <cstdint>

MYTHOS_TEST(RangeGuardBoundaries) {
	CHECK(!mythos::IsPlausibleRange(0, 8));
	CHECK(!mythos::IsPlausibleRange(0x10000, 0));
	CHECK(mythos::IsPlausibleRange(0x10000, 8));
	CHECK(!mythos::IsPlausibleRange(mythos::kMaxUserAddress, 2));
	CHECK(mythos::IsPlausibleRange(mythos::kMaxUserAddress - 1, 1));
	CHECK(!mythos::IsPlausibleRange(mythos::kMinUserAddress - 1, 1));
	CHECK(!mythos::IsPlausiblePointer(0));
	CHECK(mythos::IsPlausiblePointer(0x10000));
}

MYTHOS_TEST(GuardedReadsFailClosed) {
	mythostest::FakeMemory memory;
	const uintptr_t region = memory.AddRegion(0x100000, 0x100);
	CHECK(memory.WriteUInt8(region + 0xFF, 0xAB));

	uint8_t value = 0;
	CHECK(memory.read(region + 0xFF, &value, 1));
	CHECK_EQ(value, 0xAB);

	// Crossing the end of the region must fail, not read adjacent memory.
	CHECK(!memory.read(region + 0xFF, &value, 4));
	CHECK(!memory.read(region - 1, &value, 1));
	CHECK(!memory.read(0xDEAD0000, &value, 1));
}

MYTHOS_TEST(ArrayViewValidatesEveryBound) {
	mythostest::FakeMemory memory;
	const uintptr_t array = memory.AddRegion(0x200000, 0x40);
	const uintptr_t data = memory.AddRegion(0x300000, 0x40);

	memory.WritePointer(array + 0x00, data);
	memory.WriteInt32(array + 0x08, 2);
	memory.WriteInt32(array + 0x0C, 4);

	const mythos::ArrayView valid = mythos::ReadArrayView(memory, array, 128);
	CHECK(valid.valid());
	CHECK_EQ(valid.count, 2);

	// Negative count.
	memory.WriteInt32(array + 0x08, -1);
	CHECK(!mythos::ReadArrayView(memory, array, 128).valid());

	// Count beyond the caller's cap.
	memory.WriteInt32(array + 0x08, 200);
	memory.WriteInt32(array + 0x0C, 200);
	CHECK(!mythos::ReadArrayView(memory, array, 128).valid());

	// Capacity smaller than the count.
	memory.WriteInt32(array + 0x08, 4);
	memory.WriteInt32(array + 0x0C, 2);
	CHECK(!mythos::ReadArrayView(memory, array, 128).valid());

	// Non-empty array with no data pointer.
	memory.WriteInt32(array + 0x08, 1);
	memory.WriteInt32(array + 0x0C, 1);
	memory.WritePointer(array + 0x00, 0);
	CHECK(!mythos::ReadArrayView(memory, array, 128).valid());

	// Empty array with capacity zero is valid.
	memory.WriteInt32(array + 0x08, 0);
	memory.WriteInt32(array + 0x0C, 0);
	CHECK(mythos::ReadArrayView(memory, array, 128).valid());
}

MYTHOS_TEST(ArrayElementBounds) {
	mythostest::FakeMemory memory;
	const uintptr_t array = memory.AddRegion(0x400000, 0x40);
	const uintptr_t data = memory.AddRegion(0x500000, 0x40);
	const uintptr_t first = memory.AddRegion(0x600000, 0x10);
	const uintptr_t second = memory.AddRegion(0x700000, 0x10);

	memory.WritePointer(array + 0x00, data);
	memory.WriteInt32(array + 0x08, 2);
	memory.WriteInt32(array + 0x0C, 2);
	memory.WritePointer(data + 0x00, first);
	memory.WritePointer(data + 0x08, second);

	const mythos::ArrayView view = mythos::ReadArrayView(memory, array, 128);
	uintptr_t element = 0;
	CHECK(mythos::ReadArrayElement(memory, view, 0, element));
	CHECK_EQ(element, first);
	CHECK(mythos::ReadArrayElement(memory, view, 1, element));
	CHECK_EQ(element, second);
	CHECK(!mythos::ReadArrayElement(memory, view, 2, element));
	CHECK(!mythos::ReadArrayElement(memory, view, -1, element));
}

MYTHOS_TEST(PinnedImageIdentityConflictMatrix) {
	const Offsets::Profile& profile = Offsets::kActiveProfile;
	CHECK(Offsets::HasPinnedImageIdentity(profile));
	CHECK_EQ(profile.steamAppId, 2406770u);
	CHECK_EQ(profile.steamBuild, 25368976ull);

	// The pinned image must be large enough to contain both global RVAs.
	CHECK(Offsets::Globals::GWorld < profile.knownImage.sizeOfImage);
	CHECK(Offsets::Globals::GNames < profile.knownImage.sizeOfImage);

	Offsets::ImageIdentity good{
		profile.knownImage.sizeOfImage,
		profile.knownImage.timeDateStamp,
		profile.knownImage.checkSum,
		true,
	};
	CHECK(!Offsets::ImageIdentityConflictsWithProfile(good, profile));

	Offsets::ImageIdentity wrongTimestamp = good;
	wrongTimestamp.timeDateStamp ^= 1u;
	CHECK(Offsets::ImageIdentityConflictsWithProfile(wrongTimestamp, profile));

	Offsets::ImageIdentity wrongSize = good;
	wrongSize.sizeOfImage += 0x1000;
	CHECK(Offsets::ImageIdentityConflictsWithProfile(wrongSize, profile));

	Offsets::ImageIdentity wrongChecksum = good;
	wrongChecksum.checkSum ^= 1u;
	CHECK(Offsets::ImageIdentityConflictsWithProfile(wrongChecksum, profile));

	// A pinned profile with an unreadable image fails closed.
	const Offsets::ImageIdentity unreadable;
	CHECK(Offsets::ImageIdentityConflictsWithProfile(unreadable, profile));

	// An unpinned profile never conflicts (unknown builds inject and gate).
	Offsets::Profile unpinned = profile;
	unpinned.knownImage = Offsets::ImageIdentity{};
	CHECK(!Offsets::HasPinnedImageIdentity(unpinned));
	CHECK(!Offsets::ImageIdentityConflictsWithProfile(unreadable, unpinned));
	CHECK(!Offsets::ImageIdentityConflictsWithProfile(wrongTimestamp, unpinned));
}

MYTHOS_TEST(TransformLayoutIsPinned) {
	static_assert(sizeof(mythos::FTransform) == 0x60, "FTransform must be 0x60 bytes");
	static_assert(offsetof(mythos::FTransform, translation) == 0x20, "translation at 0x20");
	static_assert(offsetof(mythos::FTransform, scale) == 0x40, "scale at 0x40");
	CHECK(true);
}

MYTHOS_TEST(TransformPositionAppliesScaleAndRotation) {
	mythos::FTransform transform;
	transform.rotation = mythos::FQuat{ 0.0, 0.0, 0.0, 1.0 };
	transform.translation = mythos::FVector{ 100.0, 0.0, 0.0 };
	transform.scale = mythos::FVector{ 2.0, 1.0, 1.0 };

	const mythos::FVector result = transform.transformPosition(mythos::FVector{ 10.0, 0.0, 0.0 });
	CHECK(std::abs(result.x - 120.0) < 1e-9);
	CHECK(std::abs(result.y - 0.0) < 1e-9);

	CHECK(mythos::TransformLooksSane(transform));

	transform.rotation.w = 0.0;
	CHECK(!mythos::TransformLooksSane(transform));
}
