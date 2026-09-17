// ============================================================================
// Theme — MYTHOS's rounded dark identity and font set.
//
// These C++ tokens are the runtime source of truth; DESIGN.md mirrors the
// exact values and rationale. Segoe UI for controls, Bahnschrift for
// headings, Consolas for diagnostics.
// ============================================================================
#pragma once
#include <imgui.h>

namespace mythos_host::theme {

// ---- Colour tokens --------------------------------------------------------
inline constexpr ImU32 kSurface0   = IM_COL32(13, 11, 19, 246);  // window background #0D0B13
inline constexpr ImU32 kSurface1   = IM_COL32(20, 17, 29, 255);  // child / panel #14111D
inline constexpr ImU32 kSurface2   = IM_COL32(30, 26, 42, 255);  // frames, buttons #1E1A2A
inline constexpr ImU32 kSurface3   = IM_COL32(43, 36, 59, 255);  // hovered frames #2B243B
inline constexpr ImU32 kBorder     = IM_COL32(66, 55, 84, 190);  // #423754
inline constexpr ImU32 kText       = IM_COL32(236, 239, 242, 255);
inline constexpr ImU32 kTextDim    = IM_COL32(164, 156, 177, 255); // #A49CB1
inline constexpr ImU32 kTextFaint  = IM_COL32(112, 103, 125, 255); // #70677D
inline constexpr ImU32 kAccent     = IM_COL32(177, 144, 255, 255); // oracle indigo #B190FF
inline constexpr ImU32 kAccentDim  = IM_COL32(105, 81, 159, 255);  // #69519F
inline constexpr ImU32 kSuccess    = IM_COL32(74, 210, 118, 255);
inline constexpr ImU32 kWarn       = IM_COL32(255, 176, 46, 255);
inline constexpr ImU32 kDanger     = IM_COL32(255, 76, 76, 255);

// ---- ESP semantic colours -------------------------------------------------
inline constexpr ImU32 kEspEnemy   = IM_COL32(255, 62, 62, 255);   // red
inline constexpr ImU32 kEspTeam    = IM_COL32(82, 150, 255, 255);  // blue
inline constexpr ImU32 kEspDrone   = IM_COL32(0, 220, 255, 255);   // cyan
inline constexpr ImU32 kEspOccluded = IM_COL32(158, 158, 158, 255); // dimmed occluded
inline constexpr ImU32 kEspInfo    = IM_COL32(220, 220, 220, 255); // neutral text
inline constexpr ImU32 kEspOutline = IM_COL32(0, 0, 0, 255);

// ---- Metrics --------------------------------------------------------------
inline constexpr float kWindowRounding = 12.0f;
inline constexpr float kChildRounding  = 8.0f;
inline constexpr float kFrameRounding  = 6.0f;
inline constexpr float kPopupRounding  = 8.0f;
inline constexpr float kLabelWidth     = 170.0f;

struct FontSet {
	ImFont* body = nullptr;
	ImFont* heading = nullptr;
	ImFont* mono = nullptr;
	bool systemFonts = false;
};

// Applies style colours/rounding. Call once after the ImGui context exists.
void Apply();

// Loads the system font set. Returns false when only the built-in default
// font could be used (headings/mono then share the default face).
bool LoadFonts(float baseSize, float dpiScale);

[[nodiscard]] FontSet& Fonts();

void PushHeading(float scale = 1.0f);
void PushMono(float scale = 1.0f);
void Pop();

// Convenience colour helpers.
[[nodiscard]] ImVec4 ToVec4(ImU32 color);

} // namespace mythos_host::theme
