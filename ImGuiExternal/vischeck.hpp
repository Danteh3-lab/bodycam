#pragma once
#include "include.h"
#include <cstring>

// Visibility check for the ESP and the aim assist.
//
// Instead of reimplementing a ray cast, the engine is asked to do it: the
// local AController::LineOfSightTo UFunction is resolved by name from the
// PlayerController's own class hierarchy and invoked through
// UObject::ProcessEvent with a parameter block laid out from the function's
// reflected properties. Names and property offsets are read from the running
// build, so the feature survives game patches that don't change reflection.
//
// If LineOfSightTo is not reflected, AActor::WasRecentlyRendered is used as a
// render-state fallback. If neither resolves, or a call faults, IsVisible()
// fails open (reports "visible") so nothing silently disappears from the ESP.

namespace VisCheck {

	enum class Method : int {
		None = 0,
		LineOfSight,
		RecentlyRendered,
	};

	inline constexpr DWORD64 kRVA_ProcessEvent = 0x014AB3A0;
	inline constexpr int     kProcessEventIndex = 0x4F; // [DUMP] UObject::ProcessEvent vtable slot

	inline constexpr uint64_t kCPF_ReturnParm = 0x0000000000000400;
	inline constexpr uintptr_t kPropBoolByteOffset = 0x49;
	inline constexpr uintptr_t kPropBoolFieldMask  = 0x4B;

	inline constexpr DWORD  kRetryMs = 2000;
	inline constexpr int    kMaxTries = 5;
	inline constexpr DWORD  kCacheTtlMs = 50;
	inline constexpr size_t kMaxCache = 64;
	inline constexpr int    kMaxParams = 0x200;
	inline constexpr int    kMaxClassDepth = 64;
	inline constexpr int    kMaxFields = 4096;
	inline constexpr int    kMaxProperties = 256;
	inline constexpr float  kRenderTolerance = 0.2f;

	using ProcessEventFn = void(__fastcall*)(void* self, void* function, void* params);

	struct ParamLayout {
		int other = -1;
		int viewPoint = -1;
		int altChecks = -1;
		int tolerance = -1;
		int ret = -1;
		uint8_t retMask = 0xFF;
		int size = 0;
	};

	struct State {
		bool ready = false;
		Method method = Method::None;
		void* processEvent = nullptr;
		uintptr_t function = 0;
		ParamLayout params;
		char status[192] = "not initialized";
		DWORD lastTry = 0;
		int tries = 0;
		int calls = 0;
		int visible = 0;
		int hidden = 0;
	};
	inline State g_State;

	struct CacheEntry {
		DWORD time = 0;
		bool visible = true;
	};
	inline std::unordered_map<uintptr_t, CacheEntry> g_Cache;

	__declspec(noinline) inline bool SafeProcessEvent(void* self, void* function, void* params) {
		__try {
			reinterpret_cast<ProcessEventFn>(g_State.processEvent)(self, function, params);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	inline bool LooksLikeProcessEvent(uintptr_t fn) {
		if (!fn || !GameCalls::IsExecutable(fn, 24)) return false;
		uint8_t b[24] = {};
		if (!readBytes(fn, b, sizeof(b))) return false;

		static const uint8_t kHead[15] = {
			0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
			0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC
		};
		if (memcmp(b, kHead, sizeof(kHead)) != 0) return false;
		return b[19] == 0x48 && b[20] == 0x8D && b[21] == 0x6C && b[22] == 0x24;
	}

	inline uintptr_t ResolveProcessEvent(uintptr_t base, uintptr_t playerController) {
		const uintptr_t hint = base + static_cast<uintptr_t>(kRVA_ProcessEvent);
		if (LooksLikeProcessEvent(hint)) return hint;

		if (IsValidPtr(playerController)) {
			uintptr_t vtbl = 0;
			if (readPtr(playerController, vtbl) && IsValidPtr(vtbl)) {
				const int indices[] = { kProcessEventIndex, 0x42, 0x43, 0x44 };
				for (int idx : indices) {
					uintptr_t fn = 0;
					if (readPtr(vtbl + static_cast<uintptr_t>(idx) * sizeof(uintptr_t), fn) &&
					    LooksLikeProcessEvent(fn))
						return fn;
				}
			}
		}

		if (GameCalls::IsExecutable(hint, 24)) return hint;
		return 0;
	}

	inline uintptr_t FindFunctionInClassChain(uintptr_t cls, const char* want) {
		for (int depth = 0; cls && depth < kMaxClassDepth; ++depth) {
			uintptr_t field = 0;
			if (readPtr(cls + offset::ustruct_children, field)) {
				for (int i = 0; field && i < kMaxFields; ++i) {
					char name[128] = "";
					if (Names::ReadFName(field + offset::uobject_name, name, sizeof(name)) &&
					    name[0] && _stricmp(name, want) == 0)
						return field;

					uintptr_t next = 0;
					if (!readPtr(field + offset::ufield_next, next) || next == field) break;
					field = next;
				}
			}

			uintptr_t super = 0;
			if (!readPtr(cls + offset::ustruct_super_struct, super) || super == cls) break;
			cls = super;
		}
		return 0;
	}

	inline bool ReadParamLayout(uintptr_t fn, ParamLayout& p, Method method) {
		p = ParamLayout{};

		uintptr_t field = 0;
		if (!readPtr(fn + offset::ustruct_child_props, field)) return false;

		for (int i = 0; field && i < kMaxProperties; ++i) {
			char name[128] = "";
			Names::ReadFName(field + offset::ffield_name, name, sizeof(name));

			uint64_t flags = 0;
			uint32_t off = 0, size = 0;
			read<uint64_t>(field + offset::fprop_flags, flags);
			read<uint32_t>(field + offset::fprop_offset, off);
			read<uint32_t>(field + offset::fprop_element_size, size);

			if (flags & kCPF_ReturnParm) {
				p.ret = static_cast<int>(off);
				if (size == 1) {
					uint8_t byteOffset = 0, mask = 0;
					read<uint8_t>(field + kPropBoolByteOffset, byteOffset);
					read<uint8_t>(field + kPropBoolFieldMask, mask);
					p.ret += byteOffset;
					p.retMask = mask ? mask : 0xFF;
				}
			}
			else if (_stricmp(name, "Other") == 0)              p.other = static_cast<int>(off);
			else if (_stricmp(name, "ViewPoint") == 0)          p.viewPoint = static_cast<int>(off);
			else if (_stricmp(name, "bAlternateChecks") == 0)   p.altChecks = static_cast<int>(off);
			else if (_stricmp(name, "Tolerance") == 0)          p.tolerance = static_cast<int>(off);

			const uint32_t end = off + size;
			if (end > static_cast<uint32_t>(p.size)) p.size = static_cast<int>(end);

			uintptr_t next = 0;
			if (!readPtr(field + offset::ffield_next, next) || next == field) break;
			field = next;
		}

		if (p.size <= 0 || p.size > kMaxParams || p.ret < 0) return false;
		if (method == Method::LineOfSight)
			return p.other >= 0 && p.viewPoint >= 0 && p.altChecks >= 0;
		return p.tolerance >= 0;
	}

	inline void Resolve() {
		if (g_State.ready) return;

		const uintptr_t pc = adresses.player_controller;
		if (!IsValidPtr(pc)) {
			strcpy_s(g_State.status, "waiting for local player controller");
			return;
		}

		const DWORD now = GetTickCount();
		if (g_State.tries >= kMaxTries) return;
		if (g_State.lastTry != 0 && now - g_State.lastTry < kRetryMs) return;
		g_State.lastTry = now;
		++g_State.tries;

		const uintptr_t base = GameModuleBase();
		if (!base) { strcpy_s(g_State.status, "game module not found (not injected?)"); return; }

		if (!Names::Init()) {
			strcpy_s(g_State.status, "name pool unavailable; cannot resolve the vischeck function");
			return;
		}

		const uintptr_t pe = ResolveProcessEvent(base, pc);
		if (!pe) { strcpy_s(g_State.status, "UObject::ProcessEvent not resolved"); return; }

		uintptr_t cls = 0;
		if (!readPtr(pc + offset::uobject_class, cls) || !IsValidPtr(cls)) {
			strcpy_s(g_State.status, "local PlayerController class unavailable");
			return;
		}

		Method method = Method::LineOfSight;
		uintptr_t fn = FindFunctionInClassChain(cls, "LineOfSightTo");
		if (!fn) {
			method = Method::RecentlyRendered;
			fn = FindFunctionInClassChain(cls, "WasRecentlyRendered");
		}
		if (!fn) {
			strcpy_s(g_State.status, "no reflected vischeck function on the controller hierarchy");
			return;
		}

		ParamLayout layout;
		if (!ReadParamLayout(fn, layout, method)) {
			strcpy_s(g_State.status, "vischeck function parameter layout unexpected");
			return;
		}

		g_State.processEvent = reinterpret_cast<void*>(pe);
		g_State.function = fn;
		g_State.params = layout;
		g_State.method = method;
		g_State.ready = true;
		sprintf_s(g_State.status, "%s via ProcessEvent (fn 0x%llX, params 0x%X%s)",
		          method == Method::LineOfSight ? "LineOfSightTo" : "WasRecentlyRendered (render state)",
		          static_cast<unsigned long long>(fn), layout.size,
		          LooksLikeProcessEvent(pe) ? ", verified" : ", hint");
	}

	inline bool Query(uintptr_t pawn) {
		const uintptr_t pc = adresses.player_controller;
		if (!IsValidPtr(pc) || !IsValidPtr(pawn)) return true;
		if (!g_State.processEvent || !g_State.function) return true;

		alignas(8) uint8_t buf[kMaxParams] = {};
		const ParamLayout& p = g_State.params;
		void* self = reinterpret_cast<void*>(pc);

		if (g_State.method == Method::LineOfSight) {
			if (p.other < 0 || p.viewPoint < 0 || p.altChecks < 0 || p.ret < 0) return true;
			const uint64_t other = static_cast<uint64_t>(pawn);
			memcpy(buf + p.other, &other, sizeof(other));
			const double vp[3] = { g_View.Location.x, g_View.Location.y, g_View.Location.z };
			memcpy(buf + p.viewPoint, vp, sizeof(vp));
			buf[p.altChecks] = 0;
		}
		else if (g_State.method == Method::RecentlyRendered) {
			if (p.tolerance < 0 || p.ret < 0) return true;
			const float tolerance = kRenderTolerance;
			memcpy(buf + p.tolerance, &tolerance, sizeof(tolerance));
			self = reinterpret_cast<void*>(pawn);
		}
		else {
			return true;
		}

		if (!SafeProcessEvent(self, reinterpret_cast<void*>(g_State.function), buf))
			return true;
		return (buf[p.ret] & p.retMask) != 0;
	}

	inline bool IsVisible(uintptr_t pawn) {
		if (!pawn) return true;
		if (!g_State.ready) {
			Resolve();
			if (!g_State.ready) return true;
		}

		const DWORD now = GetTickCount();
		auto it = g_Cache.find(pawn);
		if (it != g_Cache.end() && now - it->second.time < kCacheTtlMs)
			return it->second.visible;

		const bool visible = Query(pawn);
		if (g_Cache.size() >= kMaxCache) g_Cache.clear();
		g_Cache[pawn] = CacheEntry{ now, visible };

		++g_State.calls;
		if (visible) ++g_State.visible;
		else ++g_State.hidden;
		return visible;
	}

	inline bool Active() { return g_State.ready; }

}
