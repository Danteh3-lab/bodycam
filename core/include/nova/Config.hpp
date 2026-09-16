// ============================================================================
// OverlayConfig — versioned NOVA settings (schema v1).
//
// Persisted to %LOCALAPPDATA%\NOVA\settings.json with atomic replace.
// Invalid values are clamped on load; a corrupt file is preserved under a
// timestamped backup before defaults are restored.
// ============================================================================
#pragma once
#include <filesystem>
#include <string>

namespace nova {

inline constexpr int kConfigSchemaVersion = 1;

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
	float maxDistance = 300.0f;
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
	PlayerFeatureConfig  players;
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

struct ConfigLoadReport {
	ConfigSource source = ConfigSource::Defaults;
	std::filesystem::path backupPath;
	std::string detail;
};

[[nodiscard]] std::filesystem::path DefaultConfigPath();

// Clamps every value into its supported range.
void ClampOverlayConfig(OverlayConfig& config);

// Serialization helpers (schema v1).
[[nodiscard]] std::string SerializeOverlayConfig(const OverlayConfig& config);
[[nodiscard]] OverlayConfig DeserializeOverlayConfig(const std::string& text, ConfigLoadReport& report);

// Loads from disk; always returns a usable config.
[[nodiscard]] OverlayConfig LoadOverlayConfig(const std::filesystem::path& path,
                                              ConfigLoadReport* report = nullptr);

// Atomic save (temp file + replace). Returns false and fills `error` on failure.
bool SaveOverlayConfig(const std::filesystem::path& path, const OverlayConfig& config,
                       std::string* error = nullptr);

} // namespace nova
