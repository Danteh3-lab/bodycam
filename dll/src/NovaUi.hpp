// ============================================================================
// NovaUi — the five-section NOVA control panel.
//
// Sections: Overview, Players, Visuals, Overlay, Diagnostics.
// The header always shows search + clear, the master ESP toggle, the config
// status and a text-labelled runtime health indicator (never colour alone).
// ============================================================================
#pragma once
#include "Particles.hpp"
#include "SettingsStore.hpp"

#include "nova/Config.hpp"
#include "nova/Diagnostics.hpp"
#include "nova/RuntimeDiagnostics.hpp"
#include "nova/SnapshotCollector.hpp"
#include "nova/WorldResolver.hpp"

#include <string>

namespace nova_host {

struct UiState {
	bool menuVisible = true;
	char search[64] = {};
	int  searchShown = 0;
	int  section = 0;
	bool initialized = false;
};

class NovaUi {
public:
	void Draw(SettingsStore& store,
	          const nova::RuntimeDiagnostics& diagnostics,
	          const nova::ResolverDiagnostics& resolver,
	          const nova::CollectionDiagnostics& collection,
	          const nova::OverlayConfig& liveConfig,
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

	void DrawHeader(nova::OverlayConfig& config, const nova::RuntimeDiagnostics& diagnostics,
	                SettingsStore& store, UiState& state, bool* changed);
	void DrawOverview(const nova::RuntimeDiagnostics& diagnostics, const nova::OverlayConfig& config);
	void DrawPlayers(nova::OverlayConfig& config, bool* changed);
	void DrawVisuals(nova::OverlayConfig& config, bool* changed);
	void DrawOverlaySection(nova::OverlayConfig& config, const nova::RuntimeDiagnostics& diagnostics,
	                        bool* changed);
	void DrawDiagnostics(const nova::RuntimeDiagnostics& diagnostics,
	                     const nova::ResolverDiagnostics& resolver,
	                     const nova::CollectionDiagnostics& collection);

	static ImU32 HealthColor(const nova::RuntimeDiagnostics& diagnostics);

	ParticleField particles_;
	std::string   filter_;
	int           shownThisFrame_ = 0;
};

} // namespace nova_host
