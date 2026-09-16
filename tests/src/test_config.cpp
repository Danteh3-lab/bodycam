#include "test_framework.h"

#include "nova/Config.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path MakeTempDirectory(const char* name) {
	const std::filesystem::path directory =
		std::filesystem::temp_directory_path() / "nova-tests" / name;
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

NOVA_TEST(ConfigRoundTrip) {
	nova::OverlayConfig config;
	config.espEnabled = true;
	config.reducedMotion = true;
	config.players.boxMode = static_cast<int>(nova::BoxMode::Corners);
	config.players.headDot = true;
	config.players.maxDistance = 250.0f;
	config.visuals.textScale = 1.25f;
	config.projection.axisOverride = 0;
	config.projection.fovScale = 1.2f;
	config.menu.x = 100.0f;
	config.menu.y = 50.0f;
	config.menu.section = 3;

	const std::string text = nova::SerializeOverlayConfig(config);
	nova::ConfigLoadReport report;
	const nova::OverlayConfig loaded = nova::DeserializeOverlayConfig(text, report);

	CHECK(report.source == nova::ConfigSource::Loaded);
	CHECK_EQ(loaded.espEnabled, true);
	CHECK_EQ(loaded.reducedMotion, true);
	CHECK_EQ(loaded.players.boxMode, static_cast<int>(nova::BoxMode::Corners));
	CHECK_EQ(loaded.players.headDot, true);
	CHECK(std::abs(loaded.players.maxDistance - 250.0f) < 1e-3);
	CHECK(std::abs(loaded.visuals.textScale - 1.25f) < 1e-3);
	CHECK_EQ(loaded.projection.axisOverride, 0);
	CHECK(std::abs(loaded.projection.fovScale - 1.2f) < 1e-3);
	CHECK(std::abs(loaded.menu.x - 100.0f) < 1e-3);
	CHECK_EQ(loaded.menu.section, 3);
}

NOVA_TEST(ConfigClampsInvalidValues) {
	nova::OverlayConfig config;
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

	nova::ClampOverlayConfig(config);

	CHECK_EQ(config.players.boxMode, static_cast<int>(nova::BoxMode::Full));
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

NOVA_TEST(ConfigCorruptionCreatesBackup) {
	const std::filesystem::path directory = MakeTempDirectory("corrupt");
	const std::filesystem::path path = directory / "settings.json";
	{
		std::ofstream stream(path, std::ios::binary);
		stream << "{ this is not json";
	}

	nova::ConfigLoadReport report;
	const nova::OverlayConfig config = nova::LoadOverlayConfig(path, &report);

	CHECK(report.source == nova::ConfigSource::RecoveredFromCorruption);
	CHECK(!report.backupPath.empty());
	CHECK(std::filesystem::exists(report.backupPath));
	CHECK(!std::filesystem::exists(path));
	CHECK_EQ(config.espEnabled, false);

	// The backup preserves the original bytes.
	const std::string backup = ReadFile(report.backupPath);
	CHECK(backup.find("this is not json") != std::string::npos);
}

NOVA_TEST(ConfigTypeCorruptionCreatesBackup) {
	const std::filesystem::path directory = MakeTempDirectory("type-corrupt");
	const std::filesystem::path path = directory / "settings.json";
	{
		// Valid JSON, wrong value types: nlohmann's value() throws.
		std::ofstream stream(path, std::ios::binary);
		stream << R"({"schema_version":1,"esp_enabled":"yes","players":{"box_scale":"big"}})";
	}

	nova::ConfigLoadReport report;
	const nova::OverlayConfig config = nova::LoadOverlayConfig(path, &report);

	CHECK(report.source == nova::ConfigSource::RecoveredFromCorruption);
	CHECK(!report.backupPath.empty());
	CHECK(std::filesystem::exists(report.backupPath));
	CHECK(!std::filesystem::exists(path));
	CHECK_EQ(config.espEnabled, false);
	CHECK(std::abs(config.players.boxScale - 1.0f) < 1e-3);
}

NOVA_TEST(ConfigTypeCorruptionNeverThrows) {
	nova::ConfigLoadReport report;
	const nova::OverlayConfig config = nova::DeserializeOverlayConfig(
		R"({"schema_version":1,"esp_enabled":42,"reduced_motion":[],"menu":{"section":"x"}})",
		report);
	CHECK(report.source == nova::ConfigSource::Defaults);
	CHECK_EQ(config.espEnabled, false);
	CHECK_EQ(config.menu.section, 0);
}

NOVA_TEST(ConfigFutureSchemaIsPreserved) {
	const std::filesystem::path directory = MakeTempDirectory("future-schema");
	const std::filesystem::path path = directory / "settings.json";
	{
		std::ofstream stream(path, std::ios::binary);
		stream << R"({"schema_version":99,"esp_enabled":true})";
	}

	nova::ConfigLoadReport report;
	const nova::OverlayConfig config = nova::LoadOverlayConfig(path, &report);
	CHECK(report.source == nova::ConfigSource::RecoveredFromCorruption);
	CHECK(std::filesystem::exists(report.backupPath));
	CHECK_EQ(config.espEnabled, false);
}

NOVA_TEST(ConfigMigratesOldSchema) {
	nova::ConfigLoadReport report;
	const nova::OverlayConfig config = nova::DeserializeOverlayConfig(
		R"({"schema_version":0,"esp_enabled":true,"players":{"name":false}})", report);

	CHECK(report.source == nova::ConfigSource::Migrated);
	CHECK_EQ(config.espEnabled, true);
	CHECK_EQ(config.players.name, false);
	CHECK_EQ(config.schemaVersion, nova::kConfigSchemaVersion);
}

NOVA_TEST(ConfigRejectsFutureSchema) {
	nova::ConfigLoadReport report;
	const nova::OverlayConfig config = nova::DeserializeOverlayConfig(
		R"({"schema_version":99,"esp_enabled":true})", report);

	CHECK(report.source == nova::ConfigSource::Defaults);
	CHECK_EQ(config.espEnabled, false);
}

NOVA_TEST(ConfigAtomicSaveAndLoad) {
	const std::filesystem::path directory = MakeTempDirectory("save");
	const std::filesystem::path path = directory / "settings.json";

	nova::OverlayConfig config;
	config.espEnabled = true;
	config.players.skeleton = true;
	config.players.maxDistance = 123.0f;

	std::string error;
	CHECK(nova::SaveOverlayConfig(path, config, &error));
	CHECK(std::filesystem::exists(path));
	CHECK(!std::filesystem::exists(path.string() + ".tmp"));

	nova::ConfigLoadReport report;
	const nova::OverlayConfig loaded = nova::LoadOverlayConfig(path, &report);
	CHECK(report.source == nova::ConfigSource::Loaded);
	CHECK_EQ(loaded.espEnabled, true);
	CHECK_EQ(loaded.players.skeleton, true);
	CHECK(std::abs(loaded.players.maxDistance - 123.0f) < 1e-3);
}

NOVA_TEST(ConfigMissingFileYieldsDefaults) {
	const std::filesystem::path directory = MakeTempDirectory("missing");
	nova::ConfigLoadReport report;
	const nova::OverlayConfig config =
		nova::LoadOverlayConfig(directory / "does-not-exist.json", &report);
	CHECK(report.source == nova::ConfigSource::Defaults);
	CHECK_EQ(config.schemaVersion, nova::kConfigSchemaVersion);
}
