#include "Offsets.hpp"
#include "mythos/NativeFunctionSignature.hpp"

#include "test_framework.h"

#include <cstddef>
#include <cstring>
#include <vector>

MYTHOS_TEST(NativePrologueAcceptsVerifiedSignature) {
	CHECK_EQ(mythos::kVerifiedNativePrologueSize, static_cast<std::size_t>(31));
	CHECK_EQ(Offsets::Signatures::NativeFunctionPrologueSize, static_cast<std::size_t>(31));
	CHECK(mythos::MatchesVerifiedNativePrologue(Offsets::Signatures::NativeFunctionPrologue,
	                                          Offsets::Signatures::NativeFunctionPrologueSize));
}

MYTHOS_TEST(NativePrologueRejectsModifiedByte) {
	std::vector<uint8_t> bytes(Offsets::Signatures::NativeFunctionPrologue,
	                           Offsets::Signatures::NativeFunctionPrologue +
	                               Offsets::Signatures::NativeFunctionPrologueSize);

	// The embedded FunctionFlags test is byte 15..24; corrupt one byte of it.
	bytes[18] ^= 0xFF;
	CHECK(!mythos::MatchesVerifiedNativePrologue(bytes.data(), bytes.size()));
	bytes[18] ^= 0xFF;

	// Corrupt the first byte instead.
	bytes[0] ^= 0x01;
	CHECK(!mythos::MatchesVerifiedNativePrologue(bytes.data(), bytes.size()));
}

MYTHOS_TEST(NativePrologueRejectsObsoletePrologue) {
	// The legacy 15-byte head (40 55 56 57 ...) must not match the verified
	// current-build prologue.
	std::vector<uint8_t> bytes(Offsets::Signatures::NativeFunctionPrologueSize, 0);
	const uint8_t obsoleteHead[15] = {
		0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
		0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC
	};
	std::memcpy(bytes.data(), obsoleteHead, sizeof(obsoleteHead));
	CHECK(!mythos::MatchesVerifiedNativePrologue(bytes.data(), bytes.size()));
}

MYTHOS_TEST(NativePrologueRejectsShortBuffer) {
	uint8_t tiny[8] = {};
	CHECK(!mythos::MatchesVerifiedNativePrologue(tiny, sizeof(tiny)));
	CHECK(!mythos::MatchesVerifiedNativePrologue(nullptr, 0));
	CHECK(!mythos::MatchesVerifiedNativePrologue(Offsets::Signatures::NativeFunctionPrologue,
	                                           mythos::kVerifiedNativePrologueSize - 1));
}
