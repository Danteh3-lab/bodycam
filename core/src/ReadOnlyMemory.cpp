#include "mythos/ReadOnlyMemory.hpp"

#include "Offsets.hpp"

namespace mythos {

bool IsPlausibleRange(uintptr_t address, size_t size) {
	if (size == 0) return false;
	if (address < kMinUserAddress) return false;
	if (address > kMaxUserAddress) return false;
	// No wrap-around past the end of the user address window.
	if (size - 1 > kMaxUserAddress - address) return false;
	return true;
}

ArrayView ReadArrayView(const ReadOnlyMemory& memory, uintptr_t address, int maxCount) {
	ArrayView view;

	uintptr_t data = 0;
	int32_t count = 0;
	int32_t capacity = 0;
	if (!memory.readValue<int32_t>(address + Offsets::Std::TArrayNum, count)) return view;
	if (!memory.readValue<int32_t>(address + Offsets::Std::TArrayMax, capacity)) return view;
	if (!memory.readValue<uintptr_t>(address + Offsets::Std::TArrayData, data)) return view;

	if (count < 0 || count > maxCount) return view;
	if (capacity < count || capacity > maxCount) return view;
	if (count > 0 && !IsPlausiblePointer(data)) return view;

	view.data = data;
	view.count = count;
	view.capacity = capacity;
	view.ok = true;
	return view;
}

bool ReadArrayElement(const ReadOnlyMemory& memory, const ArrayView& view,
                      int index, uintptr_t& out) {
	out = 0;
	if (!view.valid()) return false;
	if (index < 0 || index >= view.count) return false;
	const uintptr_t element = view.data + static_cast<uintptr_t>(index) * sizeof(uintptr_t);
	return memory.readPointer(element, out);
}

} // namespace mythos
