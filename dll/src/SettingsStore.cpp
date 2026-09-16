#include "SettingsStore.hpp"

#include "nova/Logging.hpp"

#include <cstdio>
#include <ctime>

namespace nova_host {
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

void SettingsStore::Initialize(const std::filesystem::path& path) {
	std::lock_guard<std::mutex> lock(mutex_);
	path_ = path;
	config_ = nova::LoadOverlayConfig(path_, &report_);
	nova::ClampOverlayConfig(config_);
	dirty_ = false;
	saveFailed_ = false;

	switch (report_.source) {
	case nova::ConfigSource::Loaded:
		nova::LogInfo("settings loaded from " + path_.string());
		break;
	case nova::ConfigSource::Migrated:
		nova::LogInfo("settings migrated to schema v" + std::to_string(nova::kConfigSchemaVersion));
		dirty_ = true;
		dirtySinceMs_ = 0;
		break;
	case nova::ConfigSource::RecoveredFromCorruption:
		nova::LogWarn("corrupt settings backed up to " + report_.backupPath.string() +
		              "; defaults restored");
		break;
	case nova::ConfigSource::Defaults:
		nova::LogInfo("settings defaults (" + report_.detail + ")");
		break;
	}
}

// Helper kept private via name convention (not part of the public API).
void SettingsStore::Update(const std::function<void(nova::OverlayConfig&)>& mutate) {
	std::lock_guard<std::mutex> lock(mutex_);
	nova::OverlayConfig draft = config_;
	mutate(draft);
	nova::ClampOverlayConfig(draft);
	config_ = draft;
	dirty_ = true;
	saveFailed_ = false;
	dirtySinceMs_ = 0; // set on the next Tick using the supplied clock
}

nova::OverlayConfig SettingsStore::Snapshot() const {
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
	if (nova::SaveOverlayConfig(path_, config_, &error)) {
		dirty_ = false;
		saveFailed_ = false;
		dirtySinceMs_ = 0;
		lastSaveMs_ = nowMs;
		lastSaveClock_ = ClockText();
		nova::LogInfo("settings saved");
	} else {
		dirty_ = false;
		saveFailed_ = true;
		dirtySinceMs_ = 0;
		lastError_ = error;
		nova::LogError("settings save failed: " + error);
	}
}

bool SettingsStore::Flush() {
	std::lock_guard<std::mutex> lock(mutex_);
	std::string error;
	if (nova::SaveOverlayConfig(path_, config_, &error)) {
		dirty_ = false;
		saveFailed_ = false;
		lastSaveClock_ = ClockText();
		nova::LogInfo("settings flushed on stop");
		return true;
	}
	lastError_ = error;
	nova::LogError("settings flush failed: " + error);
	return false;
}

std::string SettingsStore::StatusText() const {
	std::lock_guard<std::mutex> lock(mutex_);
	if (saveFailed_) return "Save failed";
	if (dirty_) return "Save pending";
	switch (report_.source) {
	case nova::ConfigSource::Loaded:
		return lastSaveClock_.empty() ? "Loaded from disk" : ("Saved " + lastSaveClock_);
	case nova::ConfigSource::Migrated:
		return "Migrated to schema v" + std::to_string(nova::kConfigSchemaVersion);
	case nova::ConfigSource::RecoveredFromCorruption:
		return "Recovered from corrupt file";
	case nova::ConfigSource::Defaults:
		return "Defaults in use";
	}
	return "Ready";
}

bool SettingsStore::savePending() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return dirty_;
}

} // namespace nova_host
