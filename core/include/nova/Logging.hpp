// ============================================================================
// Logging — bounded local logs under %LOCALAPPDATA%\NOVA\logs.
//
// Records lifecycle stages, timings, failures and build fingerprints. Never
// player names or gameplay data (callers must not pass them).
// ============================================================================
#pragma once
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace nova {

enum class LogLevel {
	Debug,
	Info,
	Warn,
	Error,
};

[[nodiscard]] const char* LogLevelName(LogLevel level);

class Logger {
public:
	static Logger& Instance();

	// Opens (or rotates) nova.log inside `directory`. `buildFingerprint` is
	// written once as the session header.
	bool Open(const std::filesystem::path& directory, const std::string& buildFingerprint);
	void Close();

	void Write(LogLevel level, const std::string& message);

	[[nodiscard]] bool open() const { return stream_.is_open(); }
	[[nodiscard]] const std::filesystem::path& path() const { return path_; }

	[[nodiscard]] static std::filesystem::path DefaultLogDirectory();

	// Bounded output: rotate to nova.log.1 once the file exceeds this size.
	static constexpr std::uintmax_t kMaxLogBytes = 512 * 1024;

private:
	Logger() = default;

	void RotateIfNeeded();
	void OpenStream();

	std::mutex mutex_;
	std::ofstream stream_;
	std::filesystem::path directory_;
	std::filesystem::path path_;
	std::uintmax_t bytesWritten_ = 0;
	std::string header_;
};

// Convenience wrappers.
void LogDebug(const std::string& message);
void LogInfo(const std::string& message);
void LogWarn(const std::string& message);
void LogError(const std::string& message);

} // namespace nova
