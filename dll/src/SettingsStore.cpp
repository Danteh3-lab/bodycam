#include "SettingsStore.hpp"

#include "mythos/Logging.hpp"

#include <cstdio>
#include <ctime>

namespace mythos_host {
namespace {

std::string ClockText() {
	const std::time_t now = std::time(nullptr);
	std::tm local{};
	localtime_s(&local, &now);
	char buffer[32] = {};
	std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d",
	              local.tm_hour, local.tm_min, local.tm_sec);
	return buffer;
}

} // namespace

void SettingsStore::Initialize(const std::filesystem::path& path,
                               const std::filesystem::path& legacySource) {
	std::lock_guard<std::mutex> lock(mutex_);
	path_ = path;
	importStatus_ = mythos::ImportOverlayConfigIfMissing(legacySource, path_, &importDetail_);
	config_ = mythos::LoadOverlayConfig(path_, &report_);
	mythos::ClampOverlayConfig(config_);
	dirty_ = false;
	saveFailed_ = false;
	if (importStatus_ == mythos::ConfigImportStatus::Imported) {
		mythos::LogInfo("Imported NOVA settings");
	} else if (importStatus_ == mythos::ConfigImportStatus::InvalidSource) {
		mythos::LogWarn("legacy NOVA settings invalid; defaults in use: " + importDetail_);
	} else if (importStatus_ == mythos::ConfigImportStatus::Failed) {
		mythos::LogWarn("legacy settings import failed; defaults or existing settings in use: " + importDetail_);
	}

	switch (report_.source) {
	case mythos::ConfigSource::Loaded:
		mythos::LogInfo("settings loaded from " + path_.string());
		break;
	case mythos::ConfigSource::Migrated:
		mythos::LogInfo("settings migrated to schema v" + std::to_string(mythos::kConfigSchemaVersion));
		dirty_ = true;
		dirtySinceMs_ = 0;
		break;
	case mythos::ConfigSource::RecoveredFromCorruption:
		mythos::LogWarn("corrupt settings backed up to " + report_.backupPath.string() +
		              "; defaults restored");
		break;
	case mythos::ConfigSource::Defaults:
		mythos::LogInfo("settings defaults (" + report_.detail + ")");
		break;
	}
}

// Helper kept private via name convention (not part of the public API).
void SettingsStore::Update(const std::function<void(mythos::OverlayConfig&)>& mutate) {
	std::lock_guard<std::mutex> lock(mutex_);
	mythos::OverlayConfig draft = config_;
	mutate(draft);
	mythos::ClampOverlayConfig(draft);
	config_ = draft;
	dirty_ = true;
	saveFailed_ = false;
	dirtySinceMs_ = 0; // set on the next Tick using the supplied clock
}

mythos::OverlayConfig SettingsStore::Snapshot() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return config_;
}

void SettingsStore::Tick(uint64_t nowMs) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (!dirty_) return;

	if (dirtySinceMs_ == 0) {
		dirtySinceMs_ = nowMs;
		return;
	}
	if (nowMs - dirtySinceMs_ < kDebounceMs) return;

	std::string error;
	if (mythos::SaveOverlayConfig(path_, config_, &error)) {
		dirty_ = false;
		saveFailed_ = false;
		dirtySinceMs_ = 0;
		lastSaveMs_ = nowMs;
		lastSaveClock_ = ClockText();
		mythos::LogInfo("settings saved");
	} else {
		dirty_ = false;
		saveFailed_ = true;
		dirtySinceMs_ = 0;
		lastError_ = error;
		mythos::LogError("settings save failed: " + error);
	}
}

bool SettingsStore::Flush() {
	std::lock_guard<std::mutex> lock(mutex_);
	std::string error;
	if (mythos::SaveOverlayConfig(path_, config_, &error)) {
		dirty_ = false;
		saveFailed_ = false;
		lastSaveClock_ = ClockText();
		mythos::LogInfo("settings flushed on stop");
		return true;
	}
	lastError_ = error;
	mythos::LogError("settings flush failed: " + error);
	return false;
}

std::string SettingsStore::StatusText() const {
	std::lock_guard<std::mutex> lock(mutex_);
	if (saveFailed_) return "Save failed";
	if (dirty_) return "Save pending";
	if (importStatus_ == mythos::ConfigImportStatus::Imported) return "Imported NOVA settings";
	if (importStatus_ == mythos::ConfigImportStatus::InvalidSource) return "Legacy NOVA settings invalid";
	if (importStatus_ == mythos::ConfigImportStatus::Failed) return "Legacy settings import failed";
	switch (report_.source) {
	case mythos::ConfigSource::Loaded:
		return lastSaveClock_.empty() ? "Loaded from disk" : ("Saved " + lastSaveClock_);
	case mythos::ConfigSource::Migrated:
		return "Migrated to schema v" + std::to_string(mythos::kConfigSchemaVersion);
	case mythos::ConfigSource::RecoveredFromCorruption:
		return "Recovered from corrupt file";
	case mythos::ConfigSource::Defaults:
		return "Defaults in use";
	}
	return "Ready";
}

bool SettingsStore::savePending() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return dirty_;
}

} // namespace mythos_host
