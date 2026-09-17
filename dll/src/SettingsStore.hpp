// ============================================================================
// SettingsStore — owns the live OverlayConfig.
//
// Edits apply immediately to the live config; persistence is debounced by
// 500 ms and flushed on stop. A corrupt file is backed up by the core
// loader before defaults take over.
// ============================================================================
#pragma once
#include "mythos/Config.hpp"

#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

namespace mythos_host {

class SettingsStore {
public:
	void Initialize(const std::filesystem::path& path, const std::filesystem::path& legacySource);

	// Thread-safe copy for the worker thread.
	[[nodiscard]] mythos::OverlayConfig Snapshot() const;

	// Immediate mutation + mark dirty.
	void Update(const std::function<void(mythos::OverlayConfig&)>& mutate);

	// Debounced persistence; call once per UI frame with a monotonic clock.
	void Tick(uint64_t nowMs);

	// Unconditional save (stop path).
	bool Flush();

	[[nodiscard]] std::string StatusText() const;
	[[nodiscard]] bool savePending() const;

private:
	mutable std::mutex mutex_;
	mythos::OverlayConfig config_;
	mythos::ConfigLoadReport report_;
	mythos::ConfigImportStatus importStatus_ = mythos::ConfigImportStatus::NotNeeded;
	std::string importDetail_;
	std::filesystem::path path_;

	bool dirty_ = false;
	bool saveFailed_ = false;
	uint64_t dirtySinceMs_ = 0;
	uint64_t lastSaveMs_ = 0;
	std::string lastSaveClock_;
	std::string lastError_;

	static constexpr uint64_t kDebounceMs = 500;
};

} // namespace mythos_host
