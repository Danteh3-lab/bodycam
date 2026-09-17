// ============================================================================
// GameThreadExecutor — QUARANTINED engine interaction plumbing.
//
// Unreal engine functions and ProcessEvent require game-thread affinity. Those
// calls are kept behind the optional user-mode APC delivered to the
// window-owning thread (the process's game thread). Direct RotationInput and
// ControlRotation writes deliberately mirror bodycam-master's worker-thread
// implementation; they remain guarded and quarantined in EngineCalls, but do
// not depend on this optional APC path.
//
// Initialize() opens that thread and pins this module: once an APC can be
// queued, the module must never unmap while the game thread might still run it.
// The path is then proven with a no-op round trip. Execute() queues one task,
// waits a bounded time, and cancels the task if it has not started by the
// deadline. Anything unverified fails closed.
// ============================================================================
#pragma once
#include <Windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace nova_host {

// Bounded wait for one game-thread task. Small enough that a stalled path
// disables engine calls without disturbing the 60 Hz worker.
inline constexpr unsigned kEngineCallTimeoutMs = 20;

class GameThreadExecutor {
public:
	enum class Result {
		Completed,
		NotVerified,
		Busy,
		Timeout,
	};

	GameThreadExecutor() = default;
	~GameThreadExecutor();

	GameThreadExecutor(const GameThreadExecutor&) = delete;
	GameThreadExecutor& operator=(const GameThreadExecutor&) = delete;

	// Resolves the game thread from the target window, pins the module and
	// probes the APC path once. Safe to call before the worker thread starts.
	void Initialize(HWND targetWindow);

	// False once the path failed or timed out; latched for the process lifetime.
	[[nodiscard]] bool verified() const { return verified_.load() && !failed_.load(); }
	[[nodiscard]] bool failed() const { return failed_.load(); }
	[[nodiscard]] DWORD threadId() const { return threadId_; }
	[[nodiscard]] std::string message() const;

	// Queues `task` for the game thread and waits up to timeoutMs. Task
	// captures must stay valid even if the task runs after a timeout; a
	// timeout cancels the task if it has not started and latches failed().
	Result Execute(std::function<void()> task, unsigned timeoutMs);

	// Marks every queued-but-not-started task as cancelled; the drain skips
	// them. Tasks already executing cannot be recalled.
	void CancelPending();

private:
	struct QueuedTask {
		std::atomic<bool>     cancelled{ false };
		std::function<void()> run;
	};

	static void CALLBACK ApcThunk(ULONG_PTR parameter);
	void DrainOnGameThread();
	void SetMessage(const char* text);

	mutable std::mutex mutex_;
	std::condition_variable finished_;
	std::deque<std::shared_ptr<QueuedTask>> tasks_;
	bool taskFinished_ = true;
	std::atomic<bool> inFlight_{ false };
	std::atomic<bool> verified_{ false };
	std::atomic<bool> failed_{ false };
	DWORD threadId_ = 0;
	HANDLE threadHandle_ = nullptr;
	mutable std::mutex messageMutex_;
	std::string message_ = "game-thread path not initialized";
};

} // namespace nova_host
