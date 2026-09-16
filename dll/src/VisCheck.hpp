// ============================================================================
// VisCheck — QUARANTINED engine interaction: visibility check.
//
// Instead of reimplementing a ray cast, the engine is asked to do it: the
// local PlayerController's LineOfSightTo UFunction is located by name on its
// class hierarchy and invoked through UObject::ProcessEvent with a parameter
// block laid out from the function's reflected properties. If LineOfSightTo is
// not reflected, WasRecentlyRendered is used as a render-state fallback.
//
// Everything fails open (reports "visible") when the check cannot be resolved
// or faults, so nothing silently disappears. Implements the core
// nova::VisibilityProbe interface; this is one of the only NOVA.dll modules
// allowed to call engine functions.
// ============================================================================
#pragma once
#include "GameThread.hpp"

#include "nova/NamePool.hpp"
#include "nova/ReadOnlyMemory.hpp"
#include "nova/UnrealTypes.hpp"
#include "nova/VisibilityProbe.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace nova_host {

class VisCheck final : public nova::VisibilityProbe {
public:
	enum class Method : int {
		None = 0,
		LineOfSight,
		RecentlyRendered,
	};

	struct Status {
		bool        ready = false;
		bool        enginePathAvailable = false;
		Method      method = Method::None;
		int         tries = 0;
		int         maxTries = 0;
		int         calls = 0;
		int         visible = 0;
		int         hidden = 0;
		std::size_t cacheSize = 0;
		std::string message = "not initialized";
	};

	VisCheck(const nova::ReadOnlyMemory& memory, const nova::NamePool& names,
	         GameThreadExecutor& gameThread);

	// Attempts resolution against the local PlayerController. Call once per
	// worker tick; bounded retries, then it stays quiet.
	void Tick(uintptr_t playerController);

	// Drops the resolved function, cached controller and visibility cache after
	// a map transition or any world reset.
	void OnWorldReset();

	// Owner opt-in for engine calls (ProcessEvent). While disabled the check
	// reports unavailable and never resolves or queries.
	void SetEngineCallsEnabled(bool enabled) { engineCallsEnabled_ = enabled; }
	[[nodiscard]] bool engineCallsEnabled() const { return engineCallsEnabled_; }

	[[nodiscard]] bool active() const override {
		return engineCallsEnabled_ && status_.ready && gameThread_.verified();
	}
	[[nodiscard]] bool IsVisible(uintptr_t pawn,
	                             const nova::FVector& cameraLocation) const override;

	[[nodiscard]] const Status& status() const { return status_; }

private:
	struct ParamLayout {
		int     other = -1;
		int     viewPoint = -1;
		int     altChecks = -1;
		int     tolerance = -1;
		int     ret = -1;
		int     retSize = 1;
		uint8_t retMask = 0xFF;
		int     size = 0;
	};

	struct CacheEntry {
		uint64_t timeMs = 0;
		bool     visible = true;
	};

	void Resolve(uintptr_t playerController);
	void ResetResolution();
	[[nodiscard]] uintptr_t ResolveProcessEvent(uintptr_t base, uintptr_t playerController) const;
	[[nodiscard]] uintptr_t FindFunctionInClassChain(uintptr_t cls, const char* want) const;
	[[nodiscard]] bool ReadParamLayout(uintptr_t function, ParamLayout& layout, Method method) const;
	[[nodiscard]] bool Query(uintptr_t pawn, const nova::FVector& cameraLocation) const;
	[[nodiscard]] bool LooksLikeProcessEvent(uintptr_t function) const;
	[[nodiscard]] bool IsExecutable(uintptr_t address, std::size_t size) const;

	const nova::ReadOnlyMemory& memory_;
	const nova::NamePool&       names_;
	GameThreadExecutor&         gameThread_;
	uintptr_t  playerController_ = 0;
	uintptr_t  processEvent_ = 0;
	uintptr_t  function_ = 0;
	Method     method_ = Method::None;
	ParamLayout params_;
	uint64_t   lastTryMs_ = 0;
	int        tries_ = 0;
	bool       engineCallsEnabled_ = false;
	mutable Status status_;
	mutable std::unordered_map<uintptr_t, CacheEntry> cache_;
};

} // namespace nova_host
