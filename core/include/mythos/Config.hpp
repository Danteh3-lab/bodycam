// ============================================================================
// OverlayConfig — versioned MYTHOS settings (schema v2).
//
// Persisted to %LOCALAPPDATA%\MYTHOS\settings.json with atomic replace.
// Invalid values are clamped on load; a corrupt file is preserved under a
// timestamped backup before defaults are restored.
// ============================================================================
#pragma once
#include <filesystem>
#include <string>

namespace mythos {

inline constexpr int kConfigSchemaVersion = 2;

enum class BoxMode : int {
	None = 0,
	Full = 1,
	Corners = 2,
};

struct PlayerFeatureConfig {
	int   boxMode = static_cast<int>(BoxMode::Full);
	bool  boxFromBones = true;
	float boxScale = 1.0f;
	bool  name = true;
	bool  health = true;
	bool  distance = true;
	bool  skeleton = false;
	bool  headDot = false;
	float headDotSize = 4.0f;
	bool  snapline = false;
	bool  showEnemy = true;
	bool  showTeam = false;
	bool  showDrones = true;
	bool  hideDead = true;
	bool  visibleOnly = false;
	bool  dimOccluded = false;
	float maxDistance = 300.0f;
};

struct AimConfig {
	bool  enabled = false;
	bool  ignoreTeam = true;
	bool  visibleOnly = false;
	float fov = 150.0f;      // screen pixels around the crosshair
	float smooth = 5.0f;     // higher is slower; 1 closes the gap per tick
	int   method = 1;        // 0 engine call (opt-in), 1 rotation input, 2 control rotation
	int   boneMode = 0;      // 0 head bone, 1 mid-height
	float maxStep = 25.0f;   // hard cap in degrees per tick
	bool  drawFov = true;
	bool  drawTarget = false;

	bool  softAim = false;
	float softFov = 120.0f;
	float softSmooth = 1.0f;
	bool  softHeadOnly = true;
};

struct VisualStyleConfig {
	bool  outline = true;
	float outlineExtra = 2.0f;
	float lineThickness = 1.0f;
	float textScale = 1.0f;
};

struct ProjectionConfig {
	int   axisOverride = 1; // -1 auto, 0 Y-FOV, 1 X-FOV, 2 MajorAxis
	float fovScale = 1.150f;
	float fallbackFov = 90.0f;
};

struct MenuConfig {
	float x = -1.0f; // negative = centered on first use
	float y = -1.0f;
	int   section = 0;
};

struct OverlayConfig {
	int                  schemaVersion = kConfigSchemaVersion;
	bool                 espEnabled = false;
	bool                 reducedMotion = false;
	// Off by default: engine function calls (AddYawInput/AddPitchInput and the
	// ProcessEvent vischeck) can re-enter engine code at an unsafe phase. Only
	// the owner can accept that risk.
	bool                 unsafeEngineCalls = false;
	PlayerFeatureConfig  players;
	AimConfig            aim;
	VisualStyleConfig    visuals;
	ProjectionConfig     projection;
	MenuConfig           menu;
};

enum class ConfigSource {
	Defaults,
	Loaded,
	RecoveredFromCorruption,
	Migrated,
};

enum class ConfigImportStatus {
	NotNeeded,
	Imported,
	InvalidSource,
	Failed,
};

struct ConfigLoadReport {
	ConfigSource source = ConfigSource::Defaults;
	std::filesystem::path backupPath;
	std::string detail;
};

[[nodiscard]] std::filesystem::path DefaultConfigPath();

// Clamps every value into its supported range.
void ClampOverlayConfig(OverlayConfig& config);

// Serialization helpers (schema v2; older schema versions migrate on read).
[[nodiscard]] std::string SerializeOverlayConfig(const OverlayConfig& config);
[[nodiscard]] OverlayConfig DeserializeOverlayConfig(const std::string& text, ConfigLoadReport& report);

// Loads from disk; always returns a usable config.
[[nodiscard]] OverlayConfig LoadOverlayConfig(const std::filesystem::path& path,
                                              ConfigLoadReport* report = nullptr);

// On first run, import a valid legacy settings file into `destination`.
// The source is read only and is never renamed, deleted, backed up, or
// otherwise modified. The destination is written atomically through the same
// path used for normal MYTHOS saves.
[[nodiscard]] ConfigImportStatus ImportOverlayConfigIfMissing(
	const std::filesystem::path& source, const std::filesystem::path& destination,
	std::string* detail = nullptr);

// Atomic save (temp file + replace). Returns false and fills `error` on failure.
bool SaveOverlayConfig(const std::filesystem::path& path, const OverlayConfig& config,
                       std::string* error = nullptr);

} // namespace mythos
