#include "GameThread.hpp"

#include <chrono>
#include <utility>

namespace mythos_host {
namespace {

constexpr unsigned kProbeTimeoutMs = 300;

} // namespace

GameThreadExecutor::~GameThreadExecutor() {
	if (threadHandle_ != nullptr) {
		CloseHandle(threadHandle_);
		threadHandle_ = nullptr;
	}
}

std::string GameThreadExecutor::message() const {
	std::lock_guard<std::mutex> lock(messageMutex_);
	return message_;
}

void GameThreadExecutor::SetMessage(const char* text) {
	std::lock_guard<std::mutex> lock(messageMutex_);
	message_ = text;
}

void GameThreadExecutor::Initialize(HWND targetWindow) {
	if (targetWindow == nullptr) {
		SetMessage("no target window");
		return;
	}
	if (threadHandle_ != nullptr || failed_.load() || verified_.load()) return;

	DWORD processId = 0;
	const DWORD threadId = GetWindowThreadProcessId(targetWindow, &processId);
	if (threadId == 0) {
		SetMessage("game thread id unavailable");
		return;
	}
	if (threadId == GetCurrentThreadId()) {
		SetMessage("target window is owned by MYTHOS");
		return;
	}

	threadHandle_ = OpenThread(THREAD_SET_CONTEXT, FALSE, threadId);
	if (threadHandle_ == nullptr) {
		SetMessage("OpenThread failed");
		return;
	}

	// Once an APC can be queued, this module must never unmap: the game thread
	// may execute the callback at any later alertable wait. Pin the module for
	// the rest of the process lifetime. If pinning fails, no APC is queued.
	HMODULE pinned = nullptr;
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
	                           GET_MODULE_HANDLE_EX_FLAG_PIN,
	                       reinterpret_cast<LPCWSTR>(&GameThreadExecutor::ApcThunk),
	                       &pinned) == FALSE) {
		CloseHandle(threadHandle_);
		threadHandle_ = nullptr;
		SetMessage("module pin failed; engine calls disabled");
		return;
	}

	threadId_ = threadId;
	const Result result = Execute([] {}, kProbeTimeoutMs);
	if (result == Result::Completed) {
		verified_ = true;
		SetMessage("game-thread path verified (module pinned)");
	} else {
		failed_ = true;
		SetMessage("game-thread path not verified; engine calls disabled");
	}
}

GameThreadExecutor::Result GameThreadExecutor::Execute(std::function<void()> task, unsigned timeoutMs) {
	if (threadHandle_ == nullptr || failed_.load()) return Result::NotVerified;
	if (inFlight_.exchange(true)) return Result::Busy;

	auto queued = std::make_shared<QueuedTask>();
	queued->run = std::move(task);
	{
		std::lock_guard<std::mutex> lock(mutex_);
		tasks_.push_back(queued);
		taskFinished_ = false;
	}

	if (QueueUserAPC(&GameThreadExecutor::ApcThunk, threadHandle_,
	                 reinterpret_cast<ULONG_PTR>(this)) == 0) {
		{
			std::lock_guard<std::mutex> lock(mutex_);
			tasks_.pop_back(); // only ours: the Busy gate allows one at a time
			taskFinished_ = true;
		}
		inFlight_ = false;
		failed_ = true;
		SetMessage("QueueUserAPC failed");
		return Result::NotVerified;
	}

	std::unique_lock<std::mutex> lock(mutex_);
	const bool finished = finished_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
	                                         [this] { return taskFinished_; });
	if (!finished) {
		// The task has not been drained (drain pops under this lock); cancel it
		// so a late delivery becomes a no-op, and latch failure so nothing else
		// is queued and no caller can fall back and double-apply.
		queued->cancelled = true;
		failed_ = true;
		lock.unlock();
		SetMessage("game-thread call timed out; engine calls disabled");
		return Result::Timeout;
	}
	inFlight_ = false;
	return Result::Completed;
}

void GameThreadExecutor::CancelPending() {
	std::lock_guard<std::mutex> lock(mutex_);
	for (const std::shared_ptr<QueuedTask>& task : tasks_) {
		task->cancelled = true;
	}
}

void CALLBACK GameThreadExecutor::ApcThunk(ULONG_PTR parameter) {
	reinterpret_cast<GameThreadExecutor*>(parameter)->DrainOnGameThread();
}

void GameThreadExecutor::DrainOnGameThread() {
	std::unique_lock<std::mutex> lock(mutex_);
	while (!tasks_.empty()) {
		std::shared_ptr<QueuedTask> task = std::move(tasks_.front());
		tasks_.pop_front();
		lock.unlock();
		if (!task->cancelled.load()) {
			try {
				task->run();
			} catch (...) {
				// An APC must never unwind into engine code.
			}
		}
		lock.lock();
	}
	taskFinished_ = true;
	finished_.notify_all();
}

} // namespace mythos_host
