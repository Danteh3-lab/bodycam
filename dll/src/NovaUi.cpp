#include "NovaUi.hpp"

#include "Theme.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace nova_host {
namespace {

const char* const kSections[] = { "Overview", "Players", "Visuals", "Overlay", "Diagnostics" };
constexpr int kSectionCount = static_cast<int>(sizeof(kSections) / sizeof(kSections[0]));

void LowerCopy(const char* source, char* out, size_t outSize) {
	size_t i = 0;
	for (; source[i] != '\0' && i + 1 < outSize; ++i) {
		out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(source[i])));
	}
	out[i] = '\0';
}

} // namespace

bool NovaUi::UiPass(const char* label) {
	if (label == nullptr) return true;
	if (filter_.empty()) {
		++shownThisFrame_;
		return true;
	}

	char haystack[192] = {};
	char needle[192] = {};
	LowerCopy(label, haystack, sizeof(haystack));
	LowerCopy(filter_.c_str(), needle, sizeof(needle));
	if (std::strstr(haystack, needle) != nullptr) {
		++shownThisFrame_;
		return true;
	}
	return false;
}

void NovaUi::UiHelp(const char* help) {
	if (help == nullptr || *help == '\0') return;
	ImGui::SameLine(0.0f, 4.0f);
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered()) {
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
		ImGui::TextUnformatted(help);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

void NovaUi::UiLabel(const char* label, const char* help) {
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(label);
	UiHelp(help);
	ImGui::SameLine(theme::kLabelWidth);
}

bool NovaUi::UiToggle(const char* label, bool* value, const char* help) {
	if (!UiPass(label)) return false;
	ImGui::PushID(label);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(label);
	UiHelp(help);
	ImGui::SameLine(theme::kLabelWidth);
	const bool result = ImGui::Checkbox("##value", value);
	ImGui::PopID();
	return result;
}

bool NovaUi::UiSlider(const char* label, float* value, float low, float high,
                      const char* format, const char* help) {
	if (!UiPass(label)) return false;
	ImGui::PushID(label);
	UiLabel(label, help);
	ImGui::SetNextItemWidth(-1.0f);
	const bool result = ImGui::SliderFloat("##value", value, low, high, format);
	ImGui::PopID();
	return result;
}

bool NovaUi::UiCombo(const char* label, int* value, const char* items, const char* help) {
	if (!UiPass(label)) return false;
	ImGui::PushID(label);
	UiLabel(label, help);
	ImGui::SetNextItemWidth(-1.0f);
	const bool result = ImGui::Combo("##value", value, items);
	ImGui::PopID();
	return result;
}

void NovaUi::UiGroup(const char* title) {
	ImGui::Dummy(ImVec2(0.0f, 4.0f));
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kAccent));
	theme::PushHeading(0.95f);
	ImGui::TextUnformatted(title);
	theme::Pop();
	ImGui::PopStyleColor();
	ImGui::Separator();
	ImGui::Dummy(ImVec2(0.0f, 2.0f));
}

ImU32 NovaUi::HealthColor(const nova::RuntimeDiagnostics& diagnostics) {
	if (!diagnostics.rendererOk) return theme::kDanger;
	switch (diagnostics.state) {
	case nova::RuntimeState::Ready:
		return diagnostics.entities.roster > 0 ? theme::kSuccess : theme::kWarn;
	case nova::RuntimeState::Resolving:
		return theme::kWarn;
	case nova::RuntimeState::OffsetsInvalid:
		return theme::kDanger;
	case nova::RuntimeState::WaitingForWindow:
		return theme::kTextDim;
	case nova::RuntimeState::Starting:
	case nova::RuntimeState::Stopping:
		return theme::kAccent;
	}
	return theme::kTextDim;
}

void NovaUi::DrawHeader(nova::OverlayConfig& config, const nova::RuntimeDiagnostics& diagnostics,
                        SettingsStore& store, UiState& state, bool* changed) {
	const float windowWidth = ImGui::GetWindowWidth();
	const float toggleWidth = 130.0f;
	const float clearWidth = 64.0f;

	ImGui::SetNextItemWidth(240.0f);
	ImGui::InputTextWithHint("##search", "Search options...", state.search, sizeof(state.search));
	ImGui::SameLine();
	ImGui::BeginDisabled(state.search[0] == '\0');
	if (ImGui::Button("Clear", ImVec2(clearWidth, 0.0f))) {
		state.search[0] = '\0';
		state.searchShown = 0;
	}
	ImGui::EndDisabled();

	ImGui::SameLine(windowWidth - ImGui::GetStyle().WindowPadding.x - toggleWidth);
	{
		const bool on = config.espEnabled;
		ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.13f, 0.55f, 0.32f, 1.0f)
		                                          : ImVec4(0.42f, 0.13f, 0.14f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? ImVec4(0.17f, 0.65f, 0.38f, 1.0f)
		                                                 : ImVec4(0.52f, 0.17f, 0.18f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, on ? ImVec4(0.10f, 0.45f, 0.26f, 1.0f)
		                                                : ImVec4(0.34f, 0.10f, 0.11f, 1.0f));
		if (ImGui::Button(on ? "ESP: ON" : "ESP: OFF", ImVec2(toggleWidth, 0.0f))) {
			config.espEnabled = !config.espEnabled;
			*changed = true;
		}
		ImGui::PopStyleColor(3);
	}

	// Health indicator: colour + text, never colour alone.
	const ImU32 healthColor = HealthColor(diagnostics);
	const ImVec2 cursor = ImGui::GetCursorScreenPos();
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddCircleFilled(ImVec2(cursor.x + 6.0f, cursor.y + ImGui::GetTextLineHeight() * 0.5f),
	                          5.0f, healthColor, 16);
	ImGui::Dummy(ImVec2(14.0f, 0.0f));
	ImGui::SameLine();
	ImGui::Text("Runtime: %s", nova::RuntimeStateName(diagnostics.state));
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	ImGui::TextDisabled("%s", store.StatusText().c_str());
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	ImGui::TextDisabled("%.0f FPS", diagnostics.overlayFps);
	if (diagnostics.recovering) {
		ImGui::SameLine();
		ImGui::TextColored(theme::ToVec4(theme::kWarn), "| recovering after map change");
	}

	ImGui::Separator();
}

void NovaUi::DrawOverview(const nova::RuntimeDiagnostics& diagnostics,
                          const nova::OverlayConfig& config) {
	UiGroup("Status");
	ImGui::TextWrapped("%s", nova::RuntimeStateDescription(diagnostics.state));
	if (!diagnostics.rendererOk) {
		ImGui::TextColored(theme::ToVec4(theme::kDanger),
		                   "Renderer failure: the overlay device could not be recovered.");
	} else if (diagnostics.state == nova::RuntimeState::WaitingForWindow) {
		ImGui::TextDisabled("Waiting for a world to load. The main menu has no roster yet.");
	} else if (diagnostics.state == nova::RuntimeState::Resolving) {
		ImGui::TextDisabled("Chain: %s", nova::ResolveStageName(diagnostics.stage));
	} else if (diagnostics.state == nova::RuntimeState::OffsetsInvalid) {
		ImGui::TextColored(theme::ToVec4(theme::kDanger),
		                   "Offsets do not match this build. ESP stays disabled.");
	} else if (diagnostics.state == nova::RuntimeState::Ready &&
	           diagnostics.entities.roster == 0) {
		ImGui::TextColored(theme::ToVec4(theme::kWarn), "Empty roster: no players visible.");
	}

	UiGroup("Session");
	ImGui::Text("Build: %s", diagnostics.buildIdentity.c_str());
	ImGui::Text("Snapshot: #%llu  age %.0f ms", static_cast<unsigned long long>(diagnostics.sequence),
	            static_cast<double>(diagnostics.snapshotAgeMs));
	ImGui::Text("Overlay FPS: %.0f", diagnostics.overlayFps);

	UiGroup("Keys");
	ImGui::Text("INSERT  toggle this menu");
	ImGui::Text("DELETE  unload NOVA cleanly");

	UiGroup("Quick actions");
	ImGui::TextDisabled("Master toggle: %s", config.espEnabled ? "ON" : "OFF");
	ImGui::TextDisabled("Configure features in Players and Visuals.");
}

void NovaUi::DrawPlayers(nova::OverlayConfig& config, bool* changed) {
	nova::PlayerFeatureConfig& players = config.players;

	UiGroup("Box");
	if (UiCombo("Bounding box", &players.boxMode, "None\0Full\0Corners\0",
	            "Shape drawn around each player.")) {
		*changed = true;
	}
	if (UiToggle("Fit box to pose", &players.boxFromBones,
	             "Builds the box from bone positions so it follows crouching and prone.\n"
	             "Falls back to the collision capsule when the pose is unavailable.")) {
		*changed = true;
	}
	if (UiSlider("Box size", &players.boxScale, 0.5f, 2.0f, "%.2fx",
	             "Multiplier applied after the box is computed.")) {
		*changed = true;
	}

	UiGroup("Info");
	if (UiToggle("Name", &players.name, "Player name read from the PlayerState.")) *changed = true;
	if (UiToggle("Health", &players.health,
	             "Health bar on the left plus the numeric value below.")) {
		*changed = true;
	}
	if (UiToggle("Distance", &players.distance, "Distance in metres from your camera.")) {
		*changed = true;
	}
	if (UiToggle("Snapline", &players.snapline,
	             "Line from the bottom of the screen to the player.")) {
		*changed = true;
	}

	UiGroup("Skeleton");
	if (UiToggle("Skeleton", &players.skeleton,
	             "Draws bone links from the model's own reference skeleton.")) {
		*changed = true;
	}
	if (UiToggle("Head dot", &players.headDot,
	             "Circle on the head bone identified from the reference pose.")) {
		*changed = true;
	}
	ImGui::BeginDisabled(!players.headDot);
	if (UiSlider("Head dot size", &players.headDotSize, 1.0f, 15.0f, "%.0f px")) *changed = true;
	ImGui::EndDisabled();

	UiGroup("Filters");
	if (UiToggle("Show enemies", &players.showEnemy, nullptr)) *changed = true;
	if (UiToggle("Show teammates", &players.showTeam,
	             "Teams come from PlayerState::TeamId.")) {
		*changed = true;
	}
	if (UiToggle("Show drones", &players.showDrones,
	             "Drones are detected by class name and labelled [DRONE].")) {
		*changed = true;
	}
	if (UiToggle("Hide dead", &players.hideDead,
	             "Skips players whose health is zero. Players with unreadable health stay visible.")) {
		*changed = true;
	}
	if (UiSlider("Max distance", &players.maxDistance, 10.0f, 1000.0f, "%.0f m")) *changed = true;
}

void NovaUi::DrawVisuals(nova::OverlayConfig& config, bool* changed) {
	nova::VisualStyleConfig& visuals = config.visuals;
	nova::ProjectionConfig& projection = config.projection;

	UiGroup("Style");
	if (UiToggle("Outline", &visuals.outline,
	             "Draws everything twice: black and thicker first, then in colour.")) {
		*changed = true;
	}
	ImGui::BeginDisabled(!visuals.outline);
	if (UiSlider("Outline width", &visuals.outlineExtra, 0.5f, 5.0f, "%.1f px")) *changed = true;
	ImGui::EndDisabled();
	if (UiSlider("Line thickness", &visuals.lineThickness, 0.5f, 5.0f, "%.1f px")) *changed = true;
	if (UiSlider("Text size", &visuals.textScale, 0.6f, 2.5f, "%.2fx",
	             "Scales the font itself, not a bitmap.")) {
		*changed = true;
	}

	UiGroup("Projection");
	{
		// Combo index 0..3 maps to ProjectionSettings -1..2 (0 == Auto).
		int axisSelection = projection.axisOverride + 1;
		if (axisSelection < 0) axisSelection = 0;
		if (axisSelection > 3) axisSelection = 3;
		if (UiCombo("FOV axis", &axisSelection,
		            "Auto (read from game)\0Force Y-FOV\0Force X-FOV\0Force MajorAxis\0",
		            "Force X-FOV matches Bodycam. Wrong axis makes boxes drift outward near "
		            "the screen edges.")) {
			projection.axisOverride = axisSelection - 1;
			*changed = true;
		}
	}
	if (UiSlider("FOV scale", &projection.fovScale, 0.5f, 2.5f, "%.3f",
	             "Fine correction for the projection scale. Raise it if boxes drift outward.")) {
		*changed = true;
	}
	if (UiSlider("FOV fallback", &projection.fallbackFov, 20.0f, 170.0f, "%.0f",
	             "Used only when both camera caches fail to provide a sane FOV.")) {
		*changed = true;
	}

	UiGroup("Preview palette");
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float height = 18.0f;
		const float swatch = 90.0f;
		const ImU32 colors[5] = {
			theme::kEspEnemy, theme::kEspTeam, theme::kEspDrone,
			theme::kSuccess, theme::kWarn,
		};
		const char* labels[5] = { "enemy", "team", "drone", "health", "warning" };
		for (int i = 0; i < 5; ++i) {
			const ImVec2 min(origin.x + static_cast<float>(i) * (swatch + 6.0f), origin.y);
			drawList->AddRectFilled(min, ImVec2(min.x + swatch, min.y + height), colors[i], 4.0f);
		}
		ImGui::Dummy(ImVec2(5.0f * (swatch + 6.0f), height));
		theme::PushMono(0.9f);
		for (int i = 0; i < 5; ++i) {
			if (i > 0) ImGui::SameLine(static_cast<float>(i) * (swatch + 6.0f));
			ImGui::TextDisabled("%s", labels[i]);
		}
		theme::Pop();
	}
}

void NovaUi::DrawOverlaySection(nova::OverlayConfig& config,
                                const nova::RuntimeDiagnostics& diagnostics, bool* changed) {
	UiGroup("Overlay");
	ImGui::Text("Toggle menu: INSERT");
	ImGui::Text("Unload:      DELETE");
	ImGui::Text("Overlay FPS: %.0f", diagnostics.overlayFps);
	ImGui::Text("Snapshot age: %.0f ms", static_cast<double>(diagnostics.snapshotAgeMs));
	ImGui::TextWrapped("The ESP draws on the background draw list, so this menu always stays "
	                   "on top. Windowed and borderless windowed modes are supported; exclusive "
	                   "fullscreen is not.");

	UiGroup("Motion & accessibility");
	if (UiToggle("Reduce motion", &config.reducedMotion,
	             "Disables the particle field and all decorative animation.")) {
		*changed = true;
	}
	ImGui::TextWrapped("When Windows client-area animations are off, the particle field is "
	                   "disabled automatically regardless of this setting.");
}

void NovaUi::DrawDiagnostics(const nova::RuntimeDiagnostics& diagnostics,
                             const nova::ResolverDiagnostics& resolver,
                             const nova::CollectionDiagnostics& collection) {
	UiGroup("Resolver");
	ImGui::Text("State: %s", nova::RuntimeStateName(diagnostics.state));
	ImGui::Text("Chain: %s", nova::ResolveStageName(diagnostics.stage));
	ImGui::Text("Names ready: %s", diagnostics.namesReady ? "yes" : "no");
	ImGui::Text("World anchor RVA: 0x%llX  (%s)",
	            static_cast<unsigned long long>(diagnostics.anchorRva),
	            resolver.worldFromRva ? "known RVA" : "scan");
	ImGui::Text("Anchor slot: 0x%llX", static_cast<unsigned long long>(resolver.anchorSlot));
	ImGui::Text("UWorld: 0x%llX", static_cast<unsigned long long>(diagnostics.worldPointer));
	ImGui::Text("Candidates checked: %d   full scans: %d", resolver.candidates, resolver.fullScans);
	ImGui::Text("Last scan: %u ms   total: %u ms", resolver.lastScanMs, resolver.totalScanMs);
	ImGui::Text("Map recoveries: %d   re-anchors: %d", resolver.rescues, resolver.reanchors);
	ImGui::Text("Read failures: %u", diagnostics.readFailures);

	UiGroup("Entities");
	ImGui::Text("roster %d   drawn %d", collection.entities.roster, collection.entities.drawn);
	ImGui::Text("self %d   team %d   dead %d   no-health %d",
	            collection.entities.self, collection.entities.teamFiltered,
	            collection.entities.dead, collection.entities.noHealth);
	ImGui::Text("no-position %d   too-far %d", collection.entities.noPosition,
	            collection.entities.tooFar);
	ImGui::Text("drones %d   drone-filtered %d", collection.entities.drones,
	            collection.entities.droneFiltered);

	UiGroup("Skeletons");
	ImGui::Text("Cache entries: %d", diagnostics.skeletonCacheSize);
	ImGui::Text("Named: %d   body bones (sum): %d", diagnostics.skeletonNamed,
	            diagnostics.skeletonCoreBones);
	if (diagnostics.refSkeletonOffset >= 0) {
		ImGui::Text("RefSkeleton offset: 0x%X", diagnostics.refSkeletonOffset);
	} else {
		ImGui::TextDisabled("RefSkeleton offset: not found yet");
	}
	ImGui::Text("no-mesh %d  no-pose %d  no-asset %d  no-hierarchy %d  bad-mesh %d",
	            collection.bones.noMesh, collection.bones.noPose, collection.bones.noAsset,
	            collection.bones.noHierarchy, collection.bones.badMesh);
	ImGui::Text("Skeletons drawn: %d", collection.bones.skeletonsDrawn);

	UiGroup("Logs");
	ImGui::TextWrapped("Lifecycle, timings and failures are written to the NOVA log directory. "
	                   "No player names or gameplay data are recorded.");
}

void NovaUi::Draw(SettingsStore& store, const nova::RuntimeDiagnostics& diagnostics,
                  const nova::ResolverDiagnostics& resolver,
                  const nova::CollectionDiagnostics& collection,
                  const nova::OverlayConfig& liveConfig, UiState& state, bool animationsEnabled) {
	nova::OverlayConfig config = liveConfig;
	bool changed = false;

	if (!state.initialized) {
		state.section = config.menu.section;
		state.initialized = true;
	}
	filter_ = state.search;
	shownThisFrame_ = 0;

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImVec2 defaultSize(760.0f, 520.0f);
	if (config.menu.x < 0.0f || config.menu.y < 0.0f) {
		ImGui::SetNextWindowPos(
			ImVec2(viewport->WorkPos.x + (viewport->WorkSize.x - defaultSize.x) * 0.5f,
			       viewport->WorkPos.y + (viewport->WorkSize.y - defaultSize.y) * 0.5f),
			ImGuiCond_FirstUseEver);
	} else {
		ImGui::SetNextWindowPos(ImVec2(config.menu.x, config.menu.y), ImGuiCond_FirstUseEver);
	}
	ImGui::SetNextWindowSize(defaultSize, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(640.0f, 420.0f), ImVec2(FLT_MAX, FLT_MAX));

	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
	if (!ImGui::Begin("NOVA", nullptr, flags)) {
		ImGui::End();
		return;
	}

	const ImVec2 windowPosition = ImGui::GetWindowPos();
	const ImVec2 windowSize = ImGui::GetWindowSize();
	particles_.Draw(ImGui::GetWindowDrawList(), windowPosition,
	                ImVec2(windowPosition.x + windowSize.x, windowPosition.y + windowSize.y),
	                ImGui::GetIO().DeltaTime, animationsEnabled && !config.reducedMotion);

	DrawHeader(config, diagnostics, store, state, &changed);
	if (config.menu.section != state.section) {
		config.menu.section = state.section;
		changed = true;
	}

	ImGui::BeginChild("##nav", ImVec2(150.0f, 0.0f), ImGuiChildFlags_Borders);
	for (int i = 0; i < kSectionCount; ++i) {
		if (ImGui::Selectable(kSections[i], state.section == i, 0, ImVec2(0.0f, 26.0f))) {
			state.section = i;
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);

	if (state.section == 0) {
		DrawOverview(diagnostics, config);
	} else if (state.section == 1) {
		DrawPlayers(config, &changed);
	} else if (state.section == 2) {
		DrawVisuals(config, &changed);
	} else if (state.section == 3) {
		DrawOverlaySection(config, diagnostics, &changed);
	} else {
		DrawDiagnostics(diagnostics, resolver, collection);
	}
	state.searchShown = shownThisFrame_;

	if (state.search[0] != '\0' && shownThisFrame_ == 0 && state.section != 0 &&
	    state.section != 4) {
		ImGui::TextDisabled("No options match \"%s\".", state.search);
	}

	ImGui::EndChild();
	ImGui::End();

	if (changed) {
		store.Update([&config](nova::OverlayConfig& target) { target = config; });
	}

	// Persist the window position (ImGui coordinates are relative to the game
	// client area, which is exactly how they are restored).
	if (windowPosition.x >= 0.0f && windowPosition.y >= 0.0f &&
	    (std::fabs(windowPosition.x - liveConfig.menu.x) > 1.0f ||
	     std::fabs(windowPosition.y - liveConfig.menu.y) > 1.0f)) {
		const float x = windowPosition.x;
		const float y = windowPosition.y;
		store.Update([x, y](nova::OverlayConfig& target) {
			target.menu.x = x;
			target.menu.y = y;
		});
	}
}

} // namespace nova_host
