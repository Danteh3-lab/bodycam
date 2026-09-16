#include "test_framework.h"

#include "fake_memory.h"
#include "fake_names.h"

#include "Offsets.hpp"
#include "nova/NamePool.hpp"

#include <cstring>

NOVA_TEST(NamePoolResolvesNarrowAndWide) {
	novatest::FakeMemory memory;
	novatest::FakeNamePool builder(memory);
	const uint32_t character = builder.AddName("BP_Character_C");
	const uint32_t wide = builder.AddWideName(L"WideName");

	nova::NamePool names(memory);
	CHECK(names.Attach(builder.address()));
	CHECK(names.ready());

	char buffer[64] = {};
	CHECK(names.Resolve(0, buffer, sizeof(buffer)));
	CHECK_EQ(std::strcmp(buffer, "None"), 0);

	CHECK(names.Resolve(character, buffer, sizeof(buffer)));
	CHECK_EQ(std::strcmp(buffer, "BP_Character_C"), 0);

	CHECK(names.Resolve(wide, buffer, sizeof(buffer)));
	CHECK_EQ(std::strcmp(buffer, "WideName"), 0);
}

NOVA_TEST(NamePoolRejectsBadIndexesAndPools) {
	novatest::FakeMemory memory;
	novatest::FakeNamePool builder(memory);
	builder.AddName("Player");

	nova::NamePool names(memory);
	CHECK(!names.ready());
	CHECK(names.Attach(builder.address()));
	CHECK(names.ready());

	char buffer[64] = {};
	// Block index far beyond the supported block count.
	CHECK(!names.Resolve(0x00FFFFFFu, buffer, sizeof(buffer)));
	CHECK(!names.Resolve(0xFFFF0000u, buffer, sizeof(buffer)));

	CHECK(!nova::NamePool::IsCertain(memory, 0));
	nova::NamePool unattached(memory);
	CHECK(!unattached.ready());
	CHECK(!unattached.Resolve(0, buffer, sizeof(buffer)));
}

NOVA_TEST(NamePoolObjectHelpers) {
	novatest::FakeMemory memory;
	novatest::FakeNamePool builder(memory);
	const uint32_t className = builder.AddName("BP_Character_C");
	const uint32_t objectName = builder.AddName("MyPawn");

	nova::NamePool names(memory);
	CHECK(names.Attach(builder.address()));

	const uintptr_t classObject = memory.AddRegion(0x800000, 0x40);
	memory.WriteValue<uint32_t>(classObject + Offsets::UObject::Name, className);

	const uintptr_t object = memory.AddRegion(0x900000, 0x40);
	memory.WriteValue<uint32_t>(object + Offsets::UObject::Name, objectName);
	memory.WritePointer(object + Offsets::UObject::Class, classObject);

	char buffer[64] = {};
	CHECK(names.ReadObjectName(object, buffer, sizeof(buffer)));
	CHECK_EQ(std::strcmp(buffer, "MyPawn"), 0);

	CHECK(names.ReadClassName(object, buffer, sizeof(buffer)));
	CHECK_EQ(std::strcmp(buffer, "BP_Character_C"), 0);

	CHECK(!names.ReadClassName(0xDEAD0000, buffer, sizeof(buffer)));
}

NOVA_TEST(ContainsCaseInsensitiveMatches) {
	CHECK(nova::ContainsCaseInsensitive("BP_Character_C", "character"));
	CHECK(nova::ContainsCaseInsensitive("DRONE_A", "drone"));
	CHECK(!nova::ContainsCaseInsensitive("BP_Character_C", "vehicle"));
	CHECK(!nova::ContainsCaseInsensitive(nullptr, "x"));
	CHECK(!nova::ContainsCaseInsensitive("abc", ""));
}

NOVA_TEST(NamePoolSignatureMatcher) {
	novatest::FakeMemory memory;
	novatest::FakeNamePool builder(memory);

	const uintptr_t code = memory.AddRegion(0x140000000ull + 0x1000, 0x200);
	memory.AddSection(code, 0x200, true, false);

	// Pattern: 74 09 4C 8D 05 rel32 EB 16 48 8D 0D rel32 E8 xx*4 4C 8B C0 C6 05 xx*4 01
	unsigned char pattern[33] = {
		0x74, 0x09, 0x4C, 0x8D, 0x05, 0, 0, 0, 0,
		0xEB, 0x16, 0x48, 0x8D, 0x0D, 0, 0, 0, 0,
		0xE8, 0x11, 0x22, 0x33, 0x44,
		0x4C, 0x8B, 0xC0, 0xC6, 0x05, 0x55, 0x66, 0x77, 0x88, 0x01,
	};

	const uintptr_t patternAddress = code + 0x40;
	const int32_t relative1 = static_cast<int32_t>(
		static_cast<intptr_t>(builder.address()) -
		static_cast<intptr_t>(patternAddress + 9));
	const int32_t relative2 = static_cast<int32_t>(
		static_cast<intptr_t>(builder.address()) -
		static_cast<intptr_t>(patternAddress + 18));
	std::memcpy(pattern + 5, &relative1, sizeof(relative1));
	std::memcpy(pattern + 14, &relative2, sizeof(relative2));
	memory.Write(patternAddress, pattern, sizeof(pattern));

	const uintptr_t found = nova::MatchNamePoolSignature(
		memory, pattern, sizeof(pattern), patternAddress);
	CHECK_EQ(found, builder.address());

	// A pattern whose two LEAs disagree must not match.
	pattern[16] = 0x01;
	memory.Write(patternAddress, pattern, sizeof(pattern));
	CHECK_EQ(nova::MatchNamePoolSignature(memory, pattern, sizeof(pattern), patternAddress), 0u);
}
