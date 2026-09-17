#include "mythos/NamePool.hpp"

#include "Offsets.hpp"

#include <Windows.h>

#include <cctype>
#include <cstring>

namespace mythos {
namespace {

constexpr size_t kTemporaryChars = 256;

void CopyNarrow(const char* source, char* out, size_t outSize) {
	if (out == nullptr || outSize == 0) return;
	size_t i = 0;
	for (; i + 1 < outSize && source[i] != '\0'; ++i) out[i] = source[i];
	out[i] = '\0';
}

} // namespace

bool NamePool::Attach(uintptr_t poolAddress) {
	if (!IsPlausible(memory_, poolAddress)) return false;
	pool_ = poolAddress;
	return true;
}

bool NamePool::ResolveIn(uintptr_t poolAddress, uint32_t comparisonIndex,
                         char* out, size_t outSize) const {
	if (out == nullptr || outSize == 0) return false;
	out[0] = '\0';
	if (!IsPlausiblePointer(poolAddress)) return false;

	const uint32_t block = comparisonIndex >> 16;
	const uint32_t entryOffset = comparisonIndex & 0xFFFFu;
	if (block > static_cast<uint32_t>(Offsets::NamePool::MaxBlockIndex)) return false;

	uintptr_t blockPointer = 0;
	if (!memory_.readPointer(poolAddress + Offsets::NamePool::BlocksOffset +
	                         static_cast<uintptr_t>(block) * sizeof(uintptr_t),
	                         blockPointer)) {
		return false;
	}

	const uintptr_t entry = blockPointer + static_cast<uintptr_t>(entryOffset) * 2;
	uint16_t header = 0;
	if (!memory_.readValue<uint16_t>(entry, header)) return false;

	const bool wide = (header & 1u) != 0;
	const int length = header >> 6;
	if (length <= 0 || length > Offsets::NamePool::MaxNameChars) return false;

	size_t capacity = static_cast<size_t>(length);
	if (capacity > outSize - 1) capacity = outSize - 1;
	if (capacity > kTemporaryChars - 1) capacity = kTemporaryChars - 1;
	if (capacity == 0) return false;

	if (wide) {
		wchar_t buffer[kTemporaryChars] = {};
		if (!memory_.readBytes(entry + 2, buffer, capacity * sizeof(wchar_t))) return false;
		const int converted = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(capacity),
		                                          out, static_cast<int>(outSize) - 1,
		                                          nullptr, nullptr);
		if (converted <= 0) {
			out[0] = '\0';
			return false;
		}
		out[converted] = '\0';
	} else {
		if (!memory_.readBytes(entry + 2, out, capacity)) {
			out[0] = '\0';
			return false;
		}
		out[capacity] = '\0';
	}
	return out[0] != '\0';
}

bool NamePool::Resolve(uint32_t comparisonIndex, char* out, size_t outSize) const {
	if (out == nullptr || outSize == 0) return false;
	out[0] = '\0';
	if (!ready()) return false;
	return ResolveIn(pool_, comparisonIndex, out, outSize);
}

bool NamePool::ReadName(uintptr_t nameAddress, char* out, size_t outSize) const {
	if (out == nullptr || outSize == 0) return false;
	out[0] = '\0';
	uint32_t index = 0;
	if (!memory_.readValue<uint32_t>(nameAddress, index)) return false;
	return Resolve(index, out, outSize);
}

bool NamePool::ReadObjectName(uintptr_t object, char* out, size_t outSize) const {
	if (out == nullptr || outSize == 0) return false;
	out[0] = '\0';
	if (!IsPlausiblePointer(object)) return false;
	return ReadName(object + Offsets::UObject::Name, out, outSize);
}

bool NamePool::ReadClassName(uintptr_t object, char* out, size_t outSize) const {
	if (out == nullptr || outSize == 0) return false;
	out[0] = '\0';
	uintptr_t objectClass = 0;
	if (!memory_.readPointer(object + Offsets::UObject::Class, objectClass)) return false;
	return ReadName(objectClass + Offsets::UObject::Name, out, outSize);
}

bool NamePool::IsPlausible(const ReadOnlyMemory& memory, uintptr_t poolAddress) {
	if (!IsPlausiblePointer(poolAddress) || (poolAddress & 7) != 0) return false;

	uintptr_t block0 = 0;
	if (!memory.readPointer(poolAddress + Offsets::NamePool::BlocksOffset, block0)) return false;

	char temporary[64] = {};
	return NamePool(memory).ResolveIn(poolAddress, 0, temporary, sizeof(temporary));
}

bool NamePool::IsCertain(const ReadOnlyMemory& memory, uintptr_t poolAddress) {
	if (!IsPlausible(memory, poolAddress)) return false;
	char temporary[64] = {};
	if (!NamePool(memory).ResolveIn(poolAddress, 0, temporary, sizeof(temporary))) return false;
	return std::strcmp(temporary, "None") == 0;
}

uintptr_t MatchNamePoolSignature(const ReadOnlyMemory& memory, const uint8_t* data, size_t size,
                                 uintptr_t address) {
	constexpr size_t patternLength = static_cast<size_t>(Offsets::Signatures::FNamePoolLen);
	if (data == nullptr || size < patternLength) return 0;

	const size_t last = size - patternLength;
	for (size_t i = 0; i <= last; ++i) {
		const uint8_t* b = data + i;
		if (b[0] != 0x74 || b[1] != 0x09) continue;
		if (b[2] != 0x4C || b[3] != 0x8D || b[4] != 0x05) continue;
		if (b[9] != 0xEB || b[10] != 0x16) continue;
		if (b[11] != 0x48 || b[12] != 0x8D || b[13] != 0x0D) continue;
		if (b[18] != 0xE8) continue;
		if (b[23] != 0x4C || b[24] != 0x8B || b[25] != 0xC0) continue;
		if (b[26] != 0xC6 || b[27] != 0x05) continue;
		if (b[32] != 0x01) continue;

		int32_t relative1 = 0;
		int32_t relative2 = 0;
		std::memcpy(&relative1, b + Offsets::Signatures::FNamePoolRel1, sizeof(relative1));
		std::memcpy(&relative2, b + Offsets::Signatures::FNamePoolRel2, sizeof(relative2));

		const uintptr_t target1 = address + i + 9 + static_cast<intptr_t>(relative1);
		const uintptr_t target2 = address + i + 18 + static_cast<intptr_t>(relative2);
		if (target1 != target2) continue;
		if (!NamePool::IsPlausible(memory, target1)) continue;
		return target1;
	}
	return 0;
}

bool ContainsCaseInsensitive(const char* haystack, const char* needle) {
	if (haystack == nullptr || needle == nullptr || *needle == '\0') return false;
	for (const char* h = haystack; *h != '\0'; ++h) {
		const char* a = h;
		const char* b = needle;
		while (*a != '\0' && *b != '\0' &&
		       std::tolower(static_cast<unsigned char>(*a)) == *b) {
			++a;
			++b;
		}
		if (*b == '\0') return true;
	}
	return false;
}

} // namespace mythos
