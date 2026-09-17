#include "mythos/Config.hpp"

#include <nlohmann/json.hpp>

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace mythos {
namespace {

using nlohmann::json;

std::string TimestampForBackup() {
	const auto now = std::chrono::system_clock::now();
	const std::time_t time = std::chrono::system_clock::to_time_t(now);
	std::tm local{};
	localtime_s(&local, &time);
	char buffer[32] = {};
	std::snprintf(buffer, sizeof(buffer), "%04d%02d%02d-%02d%02d%02d",
	              local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
	              local.tm_hour, local.tm_min, local.tm_sec);
	return buffer;
}

float ClampFloat(float value, float low, float high, float fallback) {
	if (!std::isfinite(value)) return fallback;
	return (std::max)(low, (std::min)(high, value));
}

int ClampInt(int value, int low, int high, int fallback) {
	if (value < low || value > high) return fallback;
	return value;
}

} // namespace

std::filesystem::path DefaultConfigPath() {
	wchar_t buffer[MAX_PATH] = {};
	const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
	if (length == 0 || length >= MAX_PATH) return std::filesystem::path(L"MYTHOS") / L"settings.json";
	return std::filesystem::path(buffer) / L"MYTHOS" / L"settings.json";
}

void ClampOverlayConfig(OverlayConfig& config) {
	if (config.schemaVersion <= 0) config.schemaVersion = kConfigSchemaVersion;
	config.schemaVersion = (std::min)(config.schemaVersion, kConfigSchemaVersion);

	config.players.boxMode = ClampInt(config.players.boxMode, 0, 2, static_cast<int>(BoxMode::Full));
	config.players.boxScale = ClampFloat(config.players.boxScale, 0.5f, 2.0f, 1.0f);
	config.players.headDotSize = ClampFloat(config.players.headDotSize, 1.0f, 15.0f, 4.0f);
	config.players.maxDistance = ClampFloat(config.players.maxDistance, 10.0f, 1000.0f, 300.0f);

	// Visibility modes are mutually exclusive: visible-only already hides what
	// dim-occluded would grey out, so visible-only wins.
	if (config.players.visibleOnly && config.players.dimOccluded) {
		config.players.dimOccluded = false;
	}

	config.aim.fov = ClampFloat(config.aim.fov, 10.0f, 600.0f, 150.0f);
	config.aim.smooth = ClampFloat(config.aim.smooth, 1.0f, 20.0f, 5.0f);
	config.aim.method = ClampInt(config.aim.method, 0, 2, 1);
	config.aim.boneMode = ClampInt(config.aim.boneMode, 0, 1, 0);
	config.aim.maxStep = ClampFloat(config.aim.maxStep, 1.0f, 90.0f, 25.0f);
	config.aim.softFov = ClampFloat(config.aim.softFov, 10.0f, 600.0f, 120.0f);
	config.aim.softSmooth = ClampFloat(config.aim.softSmooth, 1.0f, 10.0f, 1.0f);

	config.visuals.outlineExtra = ClampFloat(config.visuals.outlineExtra, 0.5f, 5.0f, 2.0f);
	config.visuals.lineThickness = ClampFloat(config.visuals.lineThickness, 0.5f, 5.0f, 1.0f);
	config.visuals.textScale = ClampFloat(config.visuals.textScale, 0.6f, 2.5f, 1.0f);

	config.projection.axisOverride = ClampInt(config.projection.axisOverride, -1, 2, 1);
	config.projection.fovScale = ClampFloat(config.projection.fovScale, 0.5f, 2.5f, 1.150f);
	config.projection.fallbackFov = ClampFloat(config.projection.fallbackFov, 20.0f, 170.0f, 90.0f);

	config.menu.section = ClampInt(config.menu.section, 0, 5, 0);
	if (!std::isfinite(config.menu.x) || !std::isfinite(config.menu.y)) {
		config.menu.x = -1.0f;
		config.menu.y = -1.0f;
	} else if (config.menu.x >= 0.0f && config.menu.y >= 0.0f) {
		config.menu.x = ClampFloat(config.menu.x, 0.0f, 20000.0f, 0.0f);
		config.menu.y = ClampFloat(config.menu.y, 0.0f, 20000.0f, 0.0f);
	} else {
		config.menu.x = -1.0f;
		config.menu.y = -1.0f;
	}
}

std::string SerializeOverlayConfig(const OverlayConfig& config) {
	json root;
	root["schema_version"] = kConfigSchemaVersion;
	root["esp_enabled"] = config.espEnabled;
	root["reduced_motion"] = config.reducedMotion;
	root["unsafe_engine_calls"] = config.unsafeEngineCalls;

	json& players = root["players"];
	players["box_mode"] = config.players.boxMode;
	players["box_from_bones"] = config.players.boxFromBones;
	players["box_scale"] = config.players.boxScale;
	players["name"] = config.players.name;
	players["health"] = config.players.health;
	players["distance"] = config.players.distance;
	players["skeleton"] = config.players.skeleton;
	players["head_dot"] = config.players.headDot;
	players["head_dot_size"] = config.players.headDotSize;
	players["snapline"] = config.players.snapline;
	players["show_enemy"] = config.players.showEnemy;
	players["show_team"] = config.players.showTeam;
	players["show_drones"] = config.players.showDrones;
	players["hide_dead"] = config.players.hideDead;
	players["visible_only"] = config.players.visibleOnly;
	players["dim_occluded"] = config.players.dimOccluded;
	players["max_distance"] = config.players.maxDistance;

	json& aim = root["aim"];
	aim["enabled"] = config.aim.enabled;
	aim["ignore_team"] = config.aim.ignoreTeam;
	aim["visible_only"] = config.aim.visibleOnly;
	aim["fov"] = config.aim.fov;
	aim["smooth"] = config.aim.smooth;
	aim["method"] = config.aim.method;
	aim["bone_mode"] = config.aim.boneMode;
	aim["max_step"] = config.aim.maxStep;
	aim["draw_fov"] = config.aim.drawFov;
	aim["draw_target"] = config.aim.drawTarget;
	aim["soft_aim"] = config.aim.softAim;
	aim["soft_fov"] = config.aim.softFov;
	aim["soft_smooth"] = config.aim.softSmooth;
	aim["soft_head_only"] = config.aim.softHeadOnly;

	json& visuals = root["visuals"];
	visuals["outline"] = config.visuals.outline;
	visuals["outline_extra"] = config.visuals.outlineExtra;
	visuals["line_thickness"] = config.visuals.lineThickness;
	visuals["text_scale"] = config.visuals.textScale;

	json& projection = root["projection"];
	projection["axis_override"] = config.projection.axisOverride;
	projection["fov_scale"] = config.projection.fovScale;
	projection["fallback_fov"] = config.projection.fallbackFov;

	json& menu = root["menu"];
	menu["x"] = config.menu.x;
	menu["y"] = config.menu.y;
	menu["section"] = config.menu.section;

	return root.dump(2);
}

OverlayConfig DeserializeOverlayConfig(const std::string& text, ConfigLoadReport& report) {
	report = ConfigLoadReport{};
	OverlayConfig config;

	// Parsing AND typed extraction are wrapped: valid JSON with a wrong value
	// type (e.g. "esp_enabled": "yes") throws from nlohmann's value() just like
	// a syntax error, and must resolve to defaults rather than escaping into
	// the host process.
	try {
		const json root = json::parse(text);

		if (!root.is_object()) {
			report.source = ConfigSource::Defaults;
			report.detail = "Settings root is not an object.";
			return config;
		}

		const int version = root.value("schema_version", 0);
		if (version > kConfigSchemaVersion) {
			report.source = ConfigSource::Defaults;
			report.detail = "Settings were written by a newer MYTHOS version.";
			return config;
		}
		report.source = version < kConfigSchemaVersion ? ConfigSource::Migrated
		                                               : ConfigSource::Loaded;

		config.schemaVersion = kConfigSchemaVersion;
		config.espEnabled = root.value("esp_enabled", config.espEnabled);
		config.reducedMotion = root.value("reduced_motion", config.reducedMotion);
		config.unsafeEngineCalls = root.value("unsafe_engine_calls", config.unsafeEngineCalls);

		if (root.contains("players") && root["players"].is_object()) {
			const json& players = root["players"];
			config.players.boxMode = players.value("box_mode", config.players.boxMode);
			config.players.boxFromBones = players.value("box_from_bones", config.players.boxFromBones);
			config.players.boxScale = players.value("box_scale", config.players.boxScale);
			config.players.name = players.value("name", config.players.name);
			config.players.health = players.value("health", config.players.health);
			config.players.distance = players.value("distance", config.players.distance);
			config.players.skeleton = players.value("skeleton", config.players.skeleton);
			config.players.headDot = players.value("head_dot", config.players.headDot);
			config.players.headDotSize = players.value("head_dot_size", config.players.headDotSize);
			config.players.snapline = players.value("snapline", config.players.snapline);
			config.players.showEnemy = players.value("show_enemy", config.players.showEnemy);
			config.players.showTeam = players.value("show_team", config.players.showTeam);
			config.players.showDrones = players.value("show_drones", config.players.showDrones);
			config.players.hideDead = players.value("hide_dead", config.players.hideDead);
			config.players.visibleOnly = players.value("visible_only", config.players.visibleOnly);
			config.players.dimOccluded = players.value("dim_occluded", config.players.dimOccluded);
			config.players.maxDistance = players.value("max_distance", config.players.maxDistance);
		}

		if (root.contains("aim") && root["aim"].is_object()) {
			const json& aim = root["aim"];
			config.aim.enabled = aim.value("enabled", config.aim.enabled);
			config.aim.ignoreTeam = aim.value("ignore_team", config.aim.ignoreTeam);
			config.aim.visibleOnly = aim.value("visible_only", config.aim.visibleOnly);
			config.aim.fov = aim.value("fov", config.aim.fov);
			config.aim.smooth = aim.value("smooth", config.aim.smooth);
			config.aim.method = aim.value("method", config.aim.method);
			config.aim.boneMode = aim.value("bone_mode", config.aim.boneMode);
			config.aim.maxStep = aim.value("max_step", config.aim.maxStep);
			config.aim.drawFov = aim.value("draw_fov", config.aim.drawFov);
			config.aim.drawTarget = aim.value("draw_target", config.aim.drawTarget);
			config.aim.softAim = aim.value("soft_aim", config.aim.softAim);
			config.aim.softFov = aim.value("soft_fov", config.aim.softFov);
			config.aim.softSmooth = aim.value("soft_smooth", config.aim.softSmooth);
			config.aim.softHeadOnly = aim.value("soft_head_only", config.aim.softHeadOnly);
		}

		if (root.contains("visuals") && root["visuals"].is_object()) {
			const json& visuals = root["visuals"];
			config.visuals.outline = visuals.value("outline", config.visuals.outline);
			config.visuals.outlineExtra = visuals.value("outline_extra", config.visuals.outlineExtra);
			config.visuals.lineThickness = visuals.value("line_thickness", config.visuals.lineThickness);
			config.visuals.textScale = visuals.value("text_scale", config.visuals.textScale);
		}

		if (root.contains("projection") && root["projection"].is_object()) {
			const json& projection = root["projection"];
			config.projection.axisOverride = projection.value("axis_override", config.projection.axisOverride);
			config.projection.fovScale = projection.value("fov_scale", config.projection.fovScale);
			config.projection.fallbackFov = projection.value("fallback_fov", config.projection.fallbackFov);
		}

		if (root.contains("menu") && root["menu"].is_object()) {
			const json& menu = root["menu"];
			config.menu.x = menu.value("x", config.menu.x);
			config.menu.y = menu.value("y", config.menu.y);
			config.menu.section = menu.value("section", config.menu.section);
		}

		ClampOverlayConfig(config);
		return config;
	} catch (const std::exception& exception) {
		// Syntax errors and type errors both land here. Returning a fresh
		// default config lets LoadOverlayConfig preserve the bad file.
		report = ConfigLoadReport{};
		report.source = ConfigSource::Defaults;
		report.detail = std::string("JSON parse failed: ") + exception.what();
		return OverlayConfig{};
	} catch (...) {
		report = ConfigLoadReport{};
		report.source = ConfigSource::Defaults;
		report.detail = "JSON parse failed: unknown error";
		return OverlayConfig{};
	}
}

OverlayConfig LoadOverlayConfig(const std::filesystem::path& path, ConfigLoadReport* report) {
	ConfigLoadReport local;
	OverlayConfig config;

	std::error_code code;
	if (!std::filesystem::exists(path, code)) {
		local.source = ConfigSource::Defaults;
		local.detail = "No settings file yet.";
		if (report != nullptr) *report = local;
		return config;
	}

	std::ifstream stream(path, std::ios::binary);
	if (!stream) {
		local.source = ConfigSource::Defaults;
		local.detail = "Settings file could not be opened.";
		if (report != nullptr) *report = local;
		return config;
	}

	std::ostringstream buffer;
	buffer << stream.rdbuf();
	stream.close();

	config = DeserializeOverlayConfig(buffer.str(), local);

	if (local.source == ConfigSource::Defaults && !local.detail.empty()) {
		// The file existed but could not produce a usable config (bad syntax,
		// bad types, or a newer schema). Preserve it before defaults take over.
		std::filesystem::path backup = path;
		backup += ".corrupt-" + TimestampForBackup() + ".json";
		std::error_code renameError;
		std::filesystem::rename(path, backup, renameError);
		if (!renameError) {
			local.source = ConfigSource::RecoveredFromCorruption;
			local.backupPath = backup;
		} else {
			local.detail += " (backup failed: " + renameError.message() + ")";
		}
	}

	if (report != nullptr) *report = local;
	return config;
}

ConfigImportStatus ImportOverlayConfigIfMissing(const std::filesystem::path& source,
                                                const std::filesystem::path& destination,
                                                std::string* detail) {
	if (detail != nullptr) detail->clear();

	std::error_code code;
	const bool destinationExists = std::filesystem::exists(destination, code);
	if (code) {
		if (detail != nullptr) *detail = "destination existence check failed: " + code.message();
		return ConfigImportStatus::Failed;
	}
	if (destinationExists) return ConfigImportStatus::NotNeeded;

	code.clear();
	const bool sourceExists = std::filesystem::exists(source, code);
	if (code) {
		if (detail != nullptr) *detail = "legacy settings existence check failed: " + code.message();
		return ConfigImportStatus::Failed;
	}
	if (!sourceExists) return ConfigImportStatus::NotNeeded;

	std::ifstream stream(source, std::ios::binary);
	if (!stream) {
		if (detail != nullptr) *detail = "legacy settings file could not be opened";
		return ConfigImportStatus::InvalidSource;
	}
	std::ostringstream buffer;
	buffer << stream.rdbuf();
	stream.close();

	ConfigLoadReport sourceReport;
	OverlayConfig config = DeserializeOverlayConfig(buffer.str(), sourceReport);
	if (sourceReport.source == ConfigSource::Defaults && !sourceReport.detail.empty()) {
		if (detail != nullptr) *detail = sourceReport.detail;
		return ConfigImportStatus::InvalidSource;
	}

	std::string saveError;
	if (!SaveOverlayConfig(destination, config, &saveError)) {
		if (detail != nullptr) *detail = saveError;
		return ConfigImportStatus::Failed;
	}
	if (detail != nullptr) *detail = "legacy settings copied atomically";
	return ConfigImportStatus::Imported;
}

bool SaveOverlayConfig(const std::filesystem::path& path, const OverlayConfig& config,
                       std::string* error) {
	OverlayConfig clamped = config;
	ClampOverlayConfig(clamped);

	std::error_code code;
	const std::filesystem::path directory = path.parent_path();
	if (!directory.empty()) {
		std::filesystem::create_directories(directory, code);
		if (code) {
			if (error != nullptr) *error = "create_directories failed: " + code.message();
			return false;
		}
	}

	const std::filesystem::path temporary = path.string() + ".tmp";
	{
		std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
		if (!stream) {
			if (error != nullptr) *error = "temp file could not be created";
			return false;
		}
		stream << SerializeOverlayConfig(clamped);
		stream.flush();
		if (!stream) {
			if (error != nullptr) *error = "temp file write failed";
			return false;
		}
	}

	if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		if (error != nullptr) {
			*error = "MoveFileEx failed with error " + std::to_string(GetLastError());
		}
		std::error_code removeError;
		std::filesystem::remove(temporary, removeError);
		return false;
	}
	return true;
}

} // namespace mythos
