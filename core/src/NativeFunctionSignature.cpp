#include "nova/NativeFunctionSignature.hpp"

#include <cstring>

namespace nova {

bool MatchesVerifiedNativePrologue(const uint8_t* bytes, std::size_t size) {
	if (bytes == nullptr || size < Offsets::Signatures::NativeFunctionPrologueSize) {
		return false;
	}
	return std::memcmp(bytes, Offsets::Signatures::NativeFunctionPrologue,
	                   Offsets::Signatures::NativeFunctionPrologueSize) == 0;
}

} // namespace nova
