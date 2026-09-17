#include "VisCheck.hpp"

#include "Offsets.hpp"

#include <Windows.h>

#include <cstdio>
#include <cstring>

namespace nova_host {
namespace {

constexpr std::size_t kMaxCacheEntries = 64;

using ProcessEventFn = void(__fastcall*)(void* self, void* function, void* params);

__declspec(noinline) bool SafeProcessEvent(ProcessEventFn function, void* self, void* target,
                                           void* params) {
	__try {
		function(self, target, params);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool ReadUInt64(const nova::ReadOnlyMemory& memory, uintptr_t address, uint64_t& out) {
	return memory.readRaw<uint64_t>(address, out);
}

} // namespace

VisCheck::VisCheck(const nova::ReadOnlyMemory& memory, const nova::NamePool& names)
	: memory_(memory), names_(names) {
	status_.maxTries = Offsets::VisCheck::MaxTries;
}

void VisCheck::SetEngineCallsEnabled(bool enabled) {
	if (engineCallsEnabled_ == enabled) return;

	engineCallsEnabled_ = enabled;
	status_.engineCallsEnabled = enabled;
	cache_.clear();
	status_.cacheSize = 0;

	if (!enabled) {
		status_.message = "engine calls disabled (enable in the Aim section)";
	} else if (status_.ready) {
		SetReadyMessage();
	} else {
		status_.message = "unsafe direct vischeck enabled; resolving";
	}
}

void VisCheck::ResetResolution() {
	processEvent_ = 0;
	function_ = 0;
	method_ = Method::None;
	params_ = ParamLayout{};
	tries_ = 0;
	lastTryMs_ = 0;
	cache_.clear();
	status_.ready = false;
	status_.method = Method::None;
	status_.tries = 0;
	status_.cacheSize = 0;
}

void VisCheck::OnWorldReset() {
	ResetResolution();
	playerController_ = 0;
	status_.message = "world changed; waiting for the local PlayerController";
}

void VisCheck::Tick(uintptr_t playerController) {
	status_.engineCallsEnabled = engineCallsEnabled_;
	if (!engineCallsEnabled_) {
		status_.message = "engine calls disabled (enable in the Aim section)";
		return;
	}

	if (nova::IsPlausiblePointer(playerController)) {
		if (playerController_ != 0 && playerController != playerController_) {
			ResetResolution();
			status_.message = "controller changed; re-resolving vischeck";
		}
		playerController_ = playerController;
	}

	if (status_.ready || !nova::IsPlausiblePointer(playerController)) return;

	const uint64_t now = GetTickCount64();
	if (tries_ >= Offsets::VisCheck::MaxTries) return;
	if (lastTryMs_ != 0 && now - lastTryMs_ < Offsets::VisCheck::RetryMs) return;
	lastTryMs_ = now;
	++tries_;
	status_.tries = tries_;

	Resolve(playerController);
}

void VisCheck::Resolve(uintptr_t playerController) {
	const uintptr_t base = memory_.module().base;
	if (base == 0) {
		status_.message = "game module not mapped";
		return;
	}
	if (!names_.ready()) {
		status_.message = "name pool unavailable; cannot resolve the vischeck function";
		return;
	}

	const uintptr_t processEvent = ResolveProcessEvent(base, playerController);
	if (processEvent == 0) {
		status_.message = "UObject::ProcessEvent not resolved";
		return;
	}

	uintptr_t cls = 0;
	if (!memory_.readPointer(playerController + Offsets::UObject::Class, cls) ||
	    !nova::IsPlausiblePointer(cls)) {
		status_.message = "local PlayerController class unavailable";
		return;
	}

	Method method = Method::LineOfSight;
	uintptr_t function = FindFunctionInClassChain(cls, Offsets::VisCheck::LineOfSightFunction);
	if (function == 0) {
		method = Method::RecentlyRendered;
		function = FindFunctionInClassChain(cls, Offsets::VisCheck::RenderFunction);
	}
	if (function == 0) {
		status_.message = "no reflected vischeck function on the controller hierarchy";
		return;
	}

	ParamLayout layout;
	if (!ReadParamLayout(function, layout, method)) {
		status_.message = "vischeck function parameter layout unexpected";
		return;
	}

	processEvent_ = processEvent;
	function_ = function;
	params_ = layout;
	method_ = method;
	status_.ready = true;
	status_.method = method;

	SetReadyMessage();
}

void VisCheck::SetReadyMessage() {
	char buffer[256] = {};
	std::snprintf(buffer, sizeof(buffer),
	              "%s via ProcessEvent on NOVA worker (unsafe direct call; fn 0x%llX, "
	              "params 0x%X%s)",
	              method_ == Method::LineOfSight ? "LineOfSightTo"
	                                             : "WasRecentlyRendered (render state)",
	              static_cast<unsigned long long>(function_), params_.size,
	              LooksLikeProcessEvent(processEvent_) ? ", verified" : "");
	status_.message = buffer;
}

bool VisCheck::IsExecutable(uintptr_t address, std::size_t size) const {
	MEMORY_BASIC_INFORMATION information{};
	if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &information, sizeof(information)) == 0) {
		return false;
	}
	if (information.State != MEM_COMMIT) return false;
	if (address + size >
	    reinterpret_cast<uintptr_t>(information.BaseAddress) + information.RegionSize) {
		return false;
	}
	const DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
	                         PAGE_EXECUTE_WRITECOPY;
	return (information.Protect & executable) != 0;
}

bool VisCheck::LooksLikeProcessEvent(uintptr_t function) const {
	if (function == 0 || !IsExecutable(function, 24)) return false;

	uint8_t bytes[24] = {};
	if (!memory_.read(function, bytes, sizeof(bytes))) return false;

	static const uint8_t kHead[15] = {
		0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
		0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC
	};
	if (std::memcmp(bytes, kHead, sizeof(kHead)) != 0) return false;
	return bytes[19] == 0x48 && bytes[20] == 0x8D && bytes[21] == 0x6C && bytes[22] == 0x24;
}

uintptr_t VisCheck::ResolveProcessEvent(uintptr_t base, uintptr_t playerController) const {
	const uintptr_t hint = base + Offsets::EngineCalls::ProcessEvent;
	if (LooksLikeProcessEvent(hint)) return hint;

	if (nova::IsPlausiblePointer(playerController)) {
		uintptr_t vtable = 0;
		if (memory_.readPointer(playerController, vtable) && nova::IsPlausiblePointer(vtable)) {
			const int indices[] = {
				Offsets::EngineCalls::ProcessEventIdx, 0x42, 0x43, 0x44
			};
			for (int index : indices) {
				uintptr_t function = 0;
				if (memory_.readPointer(vtable + static_cast<uintptr_t>(index) * sizeof(uintptr_t),
				                        function) &&
				    LooksLikeProcessEvent(function)) {
					return function;
				}
			}
		}
	}

	// Fail closed: an unverified RVA is never accepted. An offset drift must
	// disable the vischeck, not invoke an unrelated function.
	return 0;
}

uintptr_t VisCheck::FindFunctionInClassChain(uintptr_t cls, const char* want) const {
	for (int depth = 0; cls != 0 && depth < Offsets::VisCheck::MaxClassDepth; ++depth) {
		uintptr_t field = 0;
		if (memory_.readPointer(cls + Offsets::UStruct::Children, field)) {
			for (int i = 0; field != 0 && i < Offsets::VisCheck::MaxFields; ++i) {
				char name[128] = {};
				if (names_.ReadName(field + Offsets::UObject::Name, name, sizeof(name)) &&
				    name[0] != '\0' && _stricmp(name, want) == 0) {
					return field;
				}

				uintptr_t next = 0;
				if (!memory_.readPointer(field + Offsets::UField::Next, next) || next == field) break;
				field = next;
			}
		}

		uintptr_t super = 0;
		if (!memory_.readPointer(cls + Offsets::UStruct::SuperStruct, super) || super == cls) break;
		cls = super;
	}
	return 0;
}

bool VisCheck::ReadParamLayout(uintptr_t function, ParamLayout& layout, Method method) const {
	layout = ParamLayout{};

	uintptr_t field = 0;
	if (!memory_.readPointer(function + Offsets::UStruct::ChildProperties, field)) return false;

	for (int i = 0; field != 0 && i < Offsets::VisCheck::MaxProperties; ++i) {
		char name[128] = {};
		(void)names_.ReadName(field + Offsets::FField::Name, name, sizeof(name));

		uint64_t flags = 0;
		uint32_t offset = 0;
		uint32_t elementSize = 0;
		// Every metadata read must succeed: an unreadable field must not
		// acquire zero metadata and slip through the bounds checks below.
		if (!ReadUInt64(memory_, field + Offsets::Property::PropertyFlags, flags)) return false;
		if (!memory_.readValue<uint32_t>(field + Offsets::Property::Offset_Internal, offset)) {
			return false;
		}
		if (!memory_.readValue<uint32_t>(field + Offsets::Property::ElementSize, elementSize)) {
			return false;
		}

		if ((flags & Offsets::UFunction::CPF_ReturnParm) != 0) {
			layout.ret = static_cast<int>(offset);
			layout.retSize = elementSize >= 1 ? static_cast<int>(elementSize) : 1;
			if (elementSize == 1) {
				uint8_t byteOffset = 0;
				uint8_t mask = 0;
				if (!memory_.readValue<uint8_t>(field + Offsets::Property::BoolByteOffset,
				                                byteOffset)) {
					return false;
				}
				if (!memory_.readValue<uint8_t>(field + Offsets::Property::BoolFieldMask, mask)) {
					return false;
				}
				layout.ret += byteOffset;
				layout.retMask = mask != 0 ? mask : 0xFF;
			}
		} else if (_stricmp(name, "Other") == 0) {
			layout.other = static_cast<int>(offset);
		} else if (_stricmp(name, "ViewPoint") == 0) {
			layout.viewPoint = static_cast<int>(offset);
		} else if (_stricmp(name, "bAlternateChecks") == 0) {
			layout.altChecks = static_cast<int>(offset);
		} else if (_stricmp(name, "Tolerance") == 0) {
			layout.tolerance = static_cast<int>(offset);
		}

		const uint64_t end = static_cast<uint64_t>(offset) + static_cast<uint64_t>(elementSize);
		if (end > static_cast<uint64_t>(Offsets::VisCheck::MaxParams)) {
			return false; // corrupt property extent
		}
		if (end > static_cast<uint64_t>(layout.size)) {
			layout.size = static_cast<int>(end);
		}

		uintptr_t next = 0;
		if (!memory_.readPointer(field + Offsets::FField::Next, next) || next == field) break;
		field = next;
	}

	// Every reflected offset must actually fit its value in the parameter
	// block: an eight-byte object pointer, a 24-byte FVector, a float or the
	// return value. A corrupt layout is rejected instead of overrunning the
	// heap buffer that ProcessEvent will write into.
	const auto fits = [](int offset, long long width, int total) {
		return offset >= 0 && static_cast<long long>(offset) + width <= total;
	};
	if (layout.size <= 0 || layout.size > Offsets::VisCheck::MaxParams) return false;
	if (!fits(layout.ret, layout.retSize, layout.size)) return false;
	if (method == Method::LineOfSight) {
		if (!fits(layout.other, 8, layout.size)) return false;
		if (!fits(layout.viewPoint, 24, layout.size)) return false;
		if (!fits(layout.altChecks, 1, layout.size)) return false;
		return true;
	}
	if (!fits(layout.tolerance, 4, layout.size)) return false;
	return true;
}

bool VisCheck::Query(uintptr_t pawn, const nova::FVector& cameraLocation) const {
	if (!nova::IsPlausiblePointer(playerController_) || !nova::IsPlausiblePointer(pawn)) {
		return true;
	}
	if (processEvent_ == 0 || function_ == 0) return true;
	if (!engineCallsEnabled_) return true;

	// Reference-compatible execution: ProcessEvent is invoked synchronously
	// from NOVA's worker thread. This is intentionally unsafe and therefore
	// remains behind the explicit owner opt-in. The reflected layout checks
	// above bound every write, and SEH keeps a fault fail-open.
	alignas(8) uint8_t buffer[Offsets::VisCheck::MaxParams] = {};
	void* self = reinterpret_cast<void*>(playerController_);

	const ParamLayout& layout = params_;
	if (method_ == Method::LineOfSight) {
		if (layout.other < 0 || layout.viewPoint < 0 || layout.altChecks < 0) return true;

		const uint64_t other = static_cast<uint64_t>(pawn);
		std::memcpy(buffer + layout.other, &other, sizeof(other));

		const double viewPoint[3] = { cameraLocation.x, cameraLocation.y, cameraLocation.z };
		std::memcpy(buffer + layout.viewPoint, viewPoint, sizeof(viewPoint));
		buffer[static_cast<std::size_t>(layout.altChecks)] = 0;
	} else if (method_ == Method::RecentlyRendered) {
		if (layout.tolerance < 0) return true;

		const float tolerance = Offsets::VisCheck::RenderTolerance;
		std::memcpy(buffer + layout.tolerance, &tolerance, sizeof(tolerance));
		self = reinterpret_cast<void*>(pawn);
	} else {
		return true;
	}

	if (!SafeProcessEvent(reinterpret_cast<ProcessEventFn>(processEvent_), self,
	                      reinterpret_cast<void*>(function_), buffer)) {
		++status_.faults;
		return true;
	}
	return (buffer[static_cast<std::size_t>(layout.ret)] & layout.retMask) != 0;
}

bool VisCheck::IsVisible(uintptr_t pawn, const nova::FVector& cameraLocation) const {
	if (pawn == 0) return true;
	if (!status_.ready) return true;

	const uint64_t now = GetTickCount64();
	const auto cached = cache_.find(pawn);
	if (cached != cache_.end() &&
	    now - cached->second.timeMs < Offsets::VisCheck::CacheTtlMs) {
		return cached->second.visible;
	}

	const bool visible = Query(pawn, cameraLocation);
	if (cache_.size() >= kMaxCacheEntries) cache_.clear();
	cache_[pawn] = CacheEntry{ now, visible };

	++status_.calls;
	if (visible) {
		++status_.visible;
	} else {
		++status_.hidden;
	}
	status_.cacheSize = cache_.size();
	return visible;
}

} // namespace nova_host
