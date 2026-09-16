// ============================================================================
// Runtime — NOVA's lifecycle and state machine.
//
// States: Starting -> WaitingForWindow -> Resolving -> Ready, plus
// OffsetsInvalid and Stopping. A 60 Hz worker samples the read-only world and
// publishes immutable snapshots; the overlay thread renders them. Shutdown is
// always orderly: stop/join workers, cancel queued game-thread tasks, flush
// settings, destroy ImGui/D3D/window resources, then stop.
// ============================================================================
#pragma once
#include "AimController.hpp"
#include "BuildIdentity.hpp"
#include "EngineCalls.hpp"
#include "EspRenderer.hpp"
#include "GameThread.hpp"
#include "NovaUi.hpp"
#include "OverlayWindow.hpp"
#include "ProcessMemory.hpp"
#include "SettingsStore.hpp"
#include "VisCheck.hpp"

#include "nova/Config.hpp"
#include "nova/NamePool.hpp"
#include "nova/RuntimeDiagnostics.hpp"
#include "nova/SnapshotCollector.hpp"
#include "nova/WorldResolver.hpp"

#include <Windows.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace nova_host {

class Runtime {
public:
	static Runtime& Instance();

	// Runs on the bootstrap thread (never inside DllMain's loader lock).
	// Returns the process exit code for FreeLibraryAndExitThread.
	int Run(HMODULE module);

	void RequestStop();

	// Idempotent, non-throwing teardown: stops and joins the worker, cancels
	// queued game-thread tasks, flushes settings, destroys the overlay and
	// closes the log. Must run before control leaves the runtime; safe to call
	// from an exception handler or a destructor.
	void Shutdown() noexcept;

private:
	struct PublishedFrame {
		nova::GameSnapshotPtr snapshot;
		nova::RuntimeDiagnostics diagnostics;
		nova::ResolverDiagnostics resolver;
		nova::CollectionDiagnostics collection;
		AimTelemetry aim;
		EngineCalls::Status engineCalls;
		VisCheck::Status vischeck;
	};

	Runtime() = default;
	~Runtime();

	void WorkerLoop();
	void WorkerLoopImpl();
	void MainLoop();
	void PublishFrame(PublishedFrame frame);
	[[nodiscard]] PublishedFrame AcquireFrame() const;

	[[nodiscard]] static HWND FindTargetWindow(DWORD processId);
	[[nodiscard]] static nova::CaptureSettings ToCaptureSettings(const nova::OverlayConfig& config);
	[[nodiscard]] nova::RuntimeState EvaluateState(const nova::WorldContext& world,
	                                               const nova::GameSnapshot& snapshot) const;

	std::atomic<bool> stop_{ false };
	bool shutdownDone_ = false;

	HWND targetWindow_ = nullptr;

	ProcessMemory memory_;
	std::unique_ptr<nova::NamePool> names_;
	std::unique_ptr<nova::WorldResolver> resolver_;
	std::unique_ptr<GameThreadExecutor> gameThread_;
	std::unique_ptr<EngineCalls> engineCalls_;
	std::unique_ptr<VisCheck> vischeck_;
	std::unique_ptr<nova::SnapshotCollector> collector_;
	std::unique_ptr<AimController> aim_;
	std::unique_ptr<OverlayWindow> overlay_;

	std::atomic<float> viewportWidth_{ 0.0f };
	std::atomic<float> viewportHeight_{ 0.0f };

	SettingsStore settings_;
	EspRenderer esp_;
	NovaUi ui_;
	UiState uiState_;

	BuildIdentity identity_;
	nova::BoneCounters renderBones_;

	mutable std::mutex frameMutex_;
	PublishedFrame published_;

	std::thread worker_;

	bool everReady_ = false;
	bool recovering_ = false;
	int  notReadyStreak_ = 0;
	nova::RuntimeState lastLoggedState_ = nova::RuntimeState::Starting;
	bool stateLogged_ = false;
	bool worldLogged_ = false;
	bool namesLogged_ = false;
};

} // namespace nova_host
