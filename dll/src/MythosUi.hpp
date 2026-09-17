// ============================================================================
// MythosUi — the five-section MYTHOS control panel.
//
// Sections: Overview, Players, Visuals, Overlay, Diagnostics.
// The header always shows search + clear, the master ESP toggle, the config
// status and a text-labelled runtime health indicator (never colour alone).
// ============================================================================
#pragma once
#include "AimController.hpp"
#include "EngineCalls.hpp"
#include "Particles.hpp"
#include "SettingsStore.hpp"
#include "VisCheck.hpp"

#include "mythos/Config.hpp"
#include "mythos/Diagnostics.hpp"
#include "mythos/RuntimeDiagnostics.hpp"
#include "mythos/SnapshotCollector.hpp"
#include "mythos/WorldResolver.hpp"

#include <string>

namespace mythos_host {

struct UiState {
	bool menuVisible = true;
	char search[64] = {};
	int  searchShown = 0;
	int  section = 0;
	bool initialized = false;
};

class MythosUi {
public:
	void Draw(SettingsStore& store,
	          const mythos::RuntimeDiagnostics& diagnostics,
	          const mythos::ResolverDiagnostics& resolver,
	          const mythos::CollectionDiagnostics& collection,
	          const AimTelemetry& aim,
	          const EngineCalls::Status& engineCalls,
	          const VisCheck::Status& vischeck,
	          const mythos::OverlayConfig& liveConfig,
	          UiState& state,
	          bool animationsEnabled);

private:
	// Shared widgets.
	bool UiPass(const char* label);
	void UiHelp(const char* help);
	void UiLabel(const char* label, const char* help);
	bool UiToggle(const char* label, bool* value, const char* help = nullptr);
	bool UiSlider(const char* label, float* value, float low, float high,
	              const char* format, const char* help = nullptr);
	bool UiCombo(const char* label, int* value, const char* items, const char* help = nullptr);
	void UiGroup(const char* title);

	void DrawHeader(mythos::OverlayConfig& config, const mythos::RuntimeDiagnostics& diagnostics,
	                SettingsStore& store, UiState& state, bool* changed);
	void DrawOverview(const mythos::RuntimeDiagnostics& diagnostics, const mythos::OverlayConfig& config);
	void DrawPlayers(mythos::OverlayConfig& config, const VisCheck::Status& vischeck, bool* changed);
	void DrawAim(mythos::OverlayConfig& config, const AimTelemetry& aim,
	             const EngineCalls::Status& engineCalls, const VisCheck::Status& vischeck,
	             bool* changed);
	void DrawVisuals(mythos::OverlayConfig& config, bool* changed);
	void DrawOverlaySection(mythos::OverlayConfig& config, const mythos::RuntimeDiagnostics& diagnostics,
	                        bool* changed);
	void DrawDiagnostics(const mythos::RuntimeDiagnostics& diagnostics,
	                     const mythos::ResolverDiagnostics& resolver,
	                     const mythos::CollectionDiagnostics& collection,
	                     const AimTelemetry& aim,
	                     const VisCheck::Status& vischeck);

	static ImU32 HealthColor(const mythos::RuntimeDiagnostics& diagnostics);

	ParticleField particles_;
	std::string   filter_;
	int           shownThisFrame_ = 0;
};

} // namespace mythos_host
