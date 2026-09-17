#include "test_framework.h"

#include "mythos/Config.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path MakeTempDirectory(const char* name) {
	const std::filesystem::path directory =
		std::filesystem::temp_directory_path() / "mythos-tests" / name;
	std::error_code code;
	std::filesystem::remove_all(directory, code);
	std::filesystem::create_directories(directory, code);
	return directory;
}

std::string ReadFile(const std::filesystem::path& path) {
	std::ifstream stream(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(stream)),
	                   std::istreambuf_iterator<char>());
}

} // namespace

MYTHOS_TEST(ConfigRoundTrip) {
	mythos::OverlayConfig config;
	config.espEnabled = true;
	config.reducedMotion = true;
	config.players.boxMode = static_cast<int>(mythos::BoxMode::Corners);
	config.players.headDot = true;
	config.players.maxDistance = 250.0f;
	config.visuals.textScale = 1.25f;
	config.projection.axisOverride = 0;
	config.projection.fovScale = 1.2f;
	config.menu.x = 100.0f;
	config.menu.y = 50.0f;
	config.menu.section = 3;

	const std::string text = mythos::SerializeOverlayConfig(config);
	mythos::ConfigLoadReport report;
	const mythos::OverlayConfig loaded = mythos::DeserializeOverlayConfig(text, report);

	CHECK(report.source == mythos::ConfigSource::Loaded);
	CHECK_EQ(loaded.espEnabled, true);
	CHECK_EQ(loaded.reducedMotion, true);
	CHECK_EQ(loaded.players.boxMode, static_cast<int>(mythos::BoxMode::Corners));
	CHECK_EQ(loaded.players.headDot, true);
	CHECK(std::abs(loaded.players.maxDistance - 250.0f) < 1e-3);
	CHECK(std::abs(loaded.visuals.textScale - 1.25f) < 1e-3);
	CHECK_EQ(loaded.projection.axisOverride, 0);
	CHECK(std::abs(loaded.projection.fovScale - 1.2f) < 1e-3);
	CHECK(std::abs(loaded.menu.x - 100.0f) < 1e-3);
	CHECK_EQ(loaded.menu.section, 3);
}

MYTHOS_TEST(ConfigClampsInvalidValues) {
	mythos::OverlayConfig config;
	config.players.boxMode = 99;
	config.players.boxScale = -5.0f;
	config.players.headDotSize = 0.0f;
	config.players.maxDistance = 100000.0f;
	config.visuals.lineThickness = 50.0f;
	config.visuals.textScale = 0.0f;
	config.projection.axisOverride = 7;
	config.projection.fovScale = 100.0f;
	config.projection.fallbackFov = 5.0f;
	config.menu.section = 42;
	config.menu.x = -500.0f;
	config.menu.y = 123.0f;

	mythos::ClampOverlayConfig(config);

	CHECK_EQ(config.players.boxMode, static_cast<int>(mythos::BoxMode::Full));
	CHECK(std::abs(config.players.boxScale - 0.5f) < 1e-3);
	CHECK(std::abs(config.players.headDotSize - 1.0f) < 1e-3);
	CHECK(std::abs(config.players.maxDistance - 1000.0f) < 1e-3);
	CHECK(std::abs(config.visuals.lineThickness - 5.0f) < 1e-3);
	CHECK(std::abs(config.visuals.textScale - 0.6f) < 1e-3);
	CHECK_EQ(config.projection.axisOverride, 1);
	CHECK(std::abs(config.projection.fovScale - 2.5f) < 1e-3);
	CHECK(std::abs(config.projection.fallbackFov - 20.0f) < 1e-3);
	CHECK_EQ(config.menu.section, 0);
	CHECK(config.menu.x < 0.0f);
	CHECK(config.menu.y < 0.0f);
}

MYTHOS_TEST(ConfigAimRoundTripAndClamps) {
	mythos::OverlayConfig config;
	config.aim.enabled = true;
	config.aim.softAim = true;
	config.aim.visibleOnly = true;
	config.aim.method = 1;
	config.aim.boneMode = 1;
	config.aim.fov = 222.0f;
	config.aim.softFov = 66.0f;
	config.aim.maxStep = 7.0f;
	config.players.visibleOnly = true;
	config.unsafeEngineCalls = true;

	const std::string text = mythos::SerializeOverlayConfig(config);
	mythos::ConfigLoadReport report;
	const mythos::OverlayConfig loaded = mythos::DeserializeOverlayConfig(text, report);

	CHECK(report.source == mythos::ConfigSource::Loaded);
	CHECK_EQ(loaded.aim.enabled, true);
	CHECK_EQ(loaded.aim.softAim, true);
	CHECK_EQ(loaded.aim.visibleOnly, true);
	CHECK_EQ(loaded.aim.method, 1);
	CHECK_EQ(loaded.aim.boneMode, 1);
	CHECK(std::abs(loaded.aim.fov - 222.0f) < 1e-3);
	CHECK(std::abs(loaded.aim.softFov - 66.0f) < 1e-3);
	CHECK(std::abs(loaded.aim.maxStep - 7.0f) < 1e-3);
	CHECK_EQ(loaded.players.visibleOnly, true);
	CHECK_EQ(loaded.players.dimOccluded, false);
	CHECK_EQ(loaded.unsafeEngineCalls, true);

	// Visibility modes are mutually exclusive: visible-only wins on load too.
	mythos::OverlayConfig both;
	both.players.visibleOnly = true;
	both.players.dimOccluded = true;
	mythos::ClampOverlayConfig(both);
	CHECK_EQ(both.players.visibleOnly, true);
	CHECK_EQ(both.players.dimOccluded, false);

	mythos::OverlayConfig clamped;
	clamped.aim.method = 9;
	clamped.aim.boneMode = 9;
	clamped.aim.fov = 1.0f;
	clamped.aim.smooth = 0.0f;
	clamped.aim.maxStep = 999.0f;
	clamped.aim.softFov = 1.0f;
	clamped.aim.softSmooth = 99.0f;
	mythos::ClampOverlayConfig(clamped);
	CHECK_EQ(clamped.aim.method, 1);
	CHECK_EQ(clamped.aim.boneMode, 0);
	CHECK(std::abs(clamped.aim.fov - 10.0f) < 1e-3);
	CHECK(std::abs(clamped.aim.smooth - 1.0f) < 1e-3);
	CHECK(std::abs(clamped.aim.maxStep - 90.0f) < 1e-3);
	CHECK(std::abs(clamped.aim.softFov - 10.0f) < 1e-3);
	CHECK(std::abs(clamped.aim.softSmooth - 10.0f) < 1e-3);
}

MYTHOS_TEST(ConfigCorruptionCreatesBackup) {
	const std::filesystem::path directory = MakeTempDirectory("corrupt");
	const std::filesystem::path path = directory / "settings.json";
	{
		std::ofstream stream(path, std::ios::binary);
		stream << "{ this is not json";
	}

	mythos::ConfigLoadReport report;
	const mythos::OverlayConfig config = mythos::LoadOverlayConfig(path, &report);

	CHECK(report.source == mythos::ConfigSource::RecoveredFromCorruption);
	CHECK(!report.backupPath.empty());
	CHECK(std::filesystem::exists(report.backupPath));
	CHECK(!std::filesystem::exists(path));
	CHECK_EQ(config.espEnabled, false);

	// The backup preserves the original bytes.
	const std::string backup = ReadFile(report.backupPath);
	CHECK(backup.find("this is not json") != std::string::npos);
}

MYTHOS_TEST(ConfigTypeCorruptionCreatesBackup) {
	const std::filesystem::path directory = MakeTempDirectory("type-corrupt");
	const std::filesystem::path path = directory / "settings.json";
	{
		// Valid JSON, wrong value types: nlohmann's value() throws.
		std::ofstream stream(path, std::ios::binary);
		stream << R"({"schema_version":1,"esp_enabled":"yes","players":{"box_scale":"big"}})";
	}

	mythos::ConfigLoadReport report;
	const mythos::OverlayConfig config = mythos::LoadOverlayConfig(path, &report);

	CHECK(report.source == mythos::ConfigSource::RecoveredFromCorruption);
	CHECK(!report.backupPath.empty());
	CHECK(std::filesystem::exists(report.backupPath));
	CHECK(!std::filesystem::exists(path));
	CHECK_EQ(config.espEnabled, false);
	CHECK(std::abs(config.players.boxScale - 1.0f) < 1e-3);
}

MYTHOS_TEST(ConfigTypeCorruptionNeverThrows) {
	mythos::ConfigLoadReport report;
	const mythos::OverlayConfig config = mythos::DeserializeOverlayConfig(
		R"({"schema_version":1,"esp_enabled":42,"reduced_motion":[],"menu":{"section":"x"}})",
		report);
	CHECK(report.source == mythos::ConfigSource::Defaults);
	CHECK_EQ(config.espEnabled, false);
	CHECK_EQ(config.menu.section, 0);
}

MYTHOS_TEST(ConfigFutureSchemaIsPreserved) {
	const std::filesystem::path directory = MakeTempDirectory("future-schema");
	const std::filesystem::path path = directory / "settings.json";
	{
		std::ofstream stream(path, std::ios::binary);
		stream << R"({"schema_version":99,"esp_enabled":true})";
	}

	mythos::ConfigLoadReport report;
	const mythos::OverlayConfig config = mythos::LoadOverlayConfig(path, &report);
	CHECK(report.source == mythos::ConfigSource::RecoveredFromCorruption);
	CHECK(std::filesystem::exists(report.backupPath));
	CHECK_EQ(config.espEnabled, false);
}

MYTHOS_TEST(ConfigMigratesOldSchema) {
	mythos::ConfigLoadReport report;
	const mythos::OverlayConfig config = mythos::DeserializeOverlayConfig(
		R"({"schema_version":0,"esp_enabled":true,"players":{"name":false}})", report);

	CHECK(report.source == mythos::ConfigSource::Migrated);
	CHECK_EQ(config.espEnabled, true);
	CHECK_EQ(config.players.name, false);
	CHECK_EQ(config.schemaVersion, mythos::kConfigSchemaVersion);
}

MYTHOS_TEST(ConfigRejectsFutureSchema) {
	mythos::ConfigLoadReport report;
	const mythos::OverlayConfig config = mythos::DeserializeOverlayConfig(
		R"({"schema_version":99,"esp_enabled":true})", report);

	CHECK(report.source == mythos::ConfigSource::Defaults);
	CHECK_EQ(config.espEnabled, false);
}

MYTHOS_TEST(ConfigAtomicSaveAndLoad) {
	const std::filesystem::path directory = MakeTempDirectory("save");
	const std::filesystem::path path = directory / "settings.json";

	mythos::OverlayConfig config;
	config.espEnabled = true;
	config.players.skeleton = true;
	config.players.maxDistance = 123.0f;

	std::string error;
	CHECK(mythos::SaveOverlayConfig(path, config, &error));
	CHECK(std::filesystem::exists(path));
	CHECK(!std::filesystem::exists(path.string() + ".tmp"));

	mythos::ConfigLoadReport report;
	const mythos::OverlayConfig loaded = mythos::LoadOverlayConfig(path, &report);
	CHECK(report.source == mythos::ConfigSource::Loaded);
	CHECK_EQ(loaded.espEnabled, true);
	CHECK_EQ(loaded.players.skeleton, true);
	CHECK(std::abs(loaded.players.maxDistance - 123.0f) < 1e-3);
}

MYTHOS_TEST(ConfigMissingFileYieldsDefaults) {
	const std::filesystem::path directory = MakeTempDirectory("missing");
	mythos::ConfigLoadReport report;
	const mythos::OverlayConfig config =
		mythos::LoadOverlayConfig(directory / "does-not-exist.json", &report);
	CHECK(report.source == mythos::ConfigSource::Defaults);
	CHECK_EQ(config.schemaVersion, mythos::kConfigSchemaVersion);
}

MYTHOS_TEST(ConfigImportValidLegacyPreservesSource) {
	const std::filesystem::path directory = MakeTempDirectory("import-valid");
	const std::filesystem::path legacy = directory / "NOVA" / "settings.json";
	const std::filesystem::path destination = directory / "MYTHOS" / "settings.json";
	std::error_code code;
	std::filesystem::create_directories(legacy.parent_path(), code);
	const std::string original = R"({"schema_version":1,"esp_enabled":true,"players":{"name":false}})";
	{
		std::ofstream stream(legacy, std::ios::binary);
		stream << original;
	}

	std::string detail;
	CHECK(mythos::ImportOverlayConfigIfMissing(legacy, destination, &detail) ==
	      mythos::ConfigImportStatus::Imported);
	CHECK(std::filesystem::exists(destination));
	CHECK_EQ(ReadFile(legacy), original);
	mythos::ConfigLoadReport report;
	const mythos::OverlayConfig imported = mythos::LoadOverlayConfig(destination, &report);
	CHECK(report.source == mythos::ConfigSource::Loaded);
	CHECK_EQ(imported.espEnabled, true);
	CHECK_EQ(imported.players.name, false);
	CHECK(!detail.empty());
}

MYTHOS_TEST(ConfigImportExistingDestinationWins) {
	const std::filesystem::path directory = MakeTempDirectory("import-existing");
	const std::filesystem::path legacy = directory / "NOVA" / "settings.json";
	const std::filesystem::path destination = directory / "MYTHOS" / "settings.json";
	std::error_code code;
	std::filesystem::create_directories(legacy.parent_path(), code);
	mythos::OverlayConfig oldConfig;
	oldConfig.espEnabled = true;
	CHECK(mythos::SaveOverlayConfig(legacy, oldConfig));
	mythos::OverlayConfig current;
	current.players.skeleton = true;
	CHECK(mythos::SaveOverlayConfig(destination, current));
	const std::string before = ReadFile(destination);

	CHECK(mythos::ImportOverlayConfigIfMissing(legacy, destination) ==
	      mythos::ConfigImportStatus::NotNeeded);
	CHECK_EQ(ReadFile(destination), before);
}

MYTHOS_TEST(ConfigImportMissingLegacyIsNotNeeded) {
	const std::filesystem::path directory = MakeTempDirectory("import-missing");
	const std::filesystem::path destination = directory / "MYTHOS" / "settings.json";
	CHECK(mythos::ImportOverlayConfigIfMissing(directory / "NOVA" / "settings.json", destination) ==
	      mythos::ConfigImportStatus::NotNeeded);
	CHECK(!std::filesystem::exists(destination));
}

MYTHOS_TEST(ConfigImportCorruptLegacyDoesNotMutateSource) {
	const std::filesystem::path directory = MakeTempDirectory("import-corrupt");
	const std::filesystem::path legacy = directory / "NOVA" / "settings.json";
	const std::filesystem::path destination = directory / "MYTHOS" / "settings.json";
	std::error_code code;
	std::filesystem::create_directories(legacy.parent_path(), code);
	const std::string original = "{not valid json";
	{
		std::ofstream stream(legacy, std::ios::binary);
		stream << original;
	}
	std::string detail;
	CHECK(mythos::ImportOverlayConfigIfMissing(legacy, destination, &detail) ==
	      mythos::ConfigImportStatus::InvalidSource);
	CHECK(!std::filesystem::exists(destination));
	CHECK_EQ(ReadFile(legacy), original);
	CHECK(!detail.empty());
}

MYTHOS_TEST(ConfigImportIsAtomicAndMigratesSchema) {
	const std::filesystem::path directory = MakeTempDirectory("import-migrate");
	const std::filesystem::path legacy = directory / "NOVA" / "settings.json";
	const std::filesystem::path destination = directory / "MYTHOS" / "settings.json";
	std::error_code code;
	std::filesystem::create_directories(legacy.parent_path(), code);
	{
		std::ofstream stream(legacy, std::ios::binary);
		stream << R"({"schema_version":0,"esp_enabled":true,"aim":{"fov":222}})";
	}
	CHECK(mythos::ImportOverlayConfigIfMissing(legacy, destination) ==
	      mythos::ConfigImportStatus::Imported);
	CHECK(std::filesystem::exists(destination));
	CHECK(!std::filesystem::exists(destination.string() + ".tmp"));
	mythos::ConfigLoadReport report;
	const mythos::OverlayConfig imported = mythos::LoadOverlayConfig(destination, &report);
	CHECK(report.source == mythos::ConfigSource::Loaded);
	CHECK_EQ(imported.espEnabled, true);
	CHECK(std::abs(imported.aim.fov - 222.0f) < 1e-3);
}
