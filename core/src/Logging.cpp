#include "nova/Logging.hpp"

#include <Windows.h>

#include <chrono>
#include <cstdio>
#include <system_error>

namespace nova {
namespace {

std::string CurrentTimestamp() {
	const auto now = std::chrono::system_clock::now();
	const std::time_t time = std::chrono::system_clock::to_time_t(now);
	std::tm local{};
	localtime_s(&local, &time);
	const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
		now.time_since_epoch()).count() % 1000;

	char buffer[64] = {};
	std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
	              local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
	              local.tm_hour, local.tm_min, local.tm_sec,
	              static_cast<int>(millis));
	return buffer;
}

} // namespace

const char* LogLevelName(LogLevel level) {
	switch (level) {
	case LogLevel::Debug: return "DEBUG";
	case LogLevel::Info: return "INFO ";
	case LogLevel::Warn: return "WARN ";
	case LogLevel::Error: return "ERROR";
	}
	return "?????";
}

Logger& Logger::Instance() {
	static Logger instance;
	return instance;
}

std::filesystem::path Logger::DefaultLogDirectory() {
	wchar_t buffer[MAX_PATH] = {};
	const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
	if (length == 0 || length >= MAX_PATH) return std::filesystem::path(L"NOVA") / L"logs";
	return std::filesystem::path(buffer) / L"NOVA" / L"logs";
}

bool Logger::Open(const std::filesystem::path& directory, const std::string& buildFingerprint) {
	std::lock_guard<std::mutex> lock(mutex_);
	directory_ = directory;
	path_ = directory / L"nova.log";
	header_ = buildFingerprint;

	std::error_code code;
	std::filesystem::create_directories(directory_, code);

	OpenStream();
	if (!stream_.is_open()) return false;

	stream_ << "[" << CurrentTimestamp() << "] [" << LogLevelName(LogLevel::Info)
	        << "] session start build=" << header_ << "\n";
	stream_.flush();
	return true;
}

void Logger::OpenStream() {
	stream_.open(path_, std::ios::binary | std::ios::app);
	bytesWritten_ = 0;
	std::error_code code;
	if (std::filesystem::exists(path_, code)) {
		bytesWritten_ = std::filesystem::file_size(path_, code);
		if (code) bytesWritten_ = 0;
	}
}

void Logger::RotateIfNeeded() {
	if (!stream_.is_open()) return;
	if (bytesWritten_ < kMaxLogBytes) return;

	stream_.close();
	std::error_code code;
	std::filesystem::path rotated = path_;
	rotated += L".1";
	std::filesystem::remove(rotated, code);
	std::filesystem::rename(path_, rotated, code);
	OpenStream();
}

void Logger::Close() {
	std::lock_guard<std::mutex> lock(mutex_);
	if (stream_.is_open()) {
		stream_ << "[" << CurrentTimestamp() << "] [" << LogLevelName(LogLevel::Info)
		        << "] session end\n";
		stream_.close();
	}
}

void Logger::Write(LogLevel level, const std::string& message) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (!stream_.is_open()) return;

	const std::string line =
		"[" + CurrentTimestamp() + "] [" + LogLevelName(level) + "] " + message + "\n";
	stream_ << line;
	stream_.flush();
	bytesWritten_ += line.size();
	RotateIfNeeded();
}

void LogDebug(const std::string& message) { Logger::Instance().Write(LogLevel::Debug, message); }
void LogInfo(const std::string& message) { Logger::Instance().Write(LogLevel::Info, message); }
void LogWarn(const std::string& message) { Logger::Instance().Write(LogLevel::Warn, message); }
void LogError(const std::string& message) { Logger::Instance().Write(LogLevel::Error, message); }

} // namespace nova
