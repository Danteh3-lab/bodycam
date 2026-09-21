#include "Runtime.hpp"

#include "Platform.hpp"

#include "Offsets.hpp"
#include "mythos/Logging.hpp"

#include <imgui.h>

#include <chrono>
#include <cstdio>
#include <string>

namespace mythos_host {
namespace {

constexpr uint64_t kWorkerIntervalMs = 16;   // ~60 Hz sampling
constexpr int      kRecoverAfterTicks = 30;  // ~0.5 s before declaring recovery
constexpr size_t   kEngineScanStepBytes = 1024 * 1024; // bounded per-tick code scan

struct WindowSearch {
	DWORD   processId = 0;
	HWND    best = nullptr;
	int64_t bestArea = 0;
};

BOOL CALLBACK EnumWindowsCallback(HWND window, LPARAM parameter) {
	auto* search = reinterpret_cast<WindowSearch*>(parameter);

	DWORD processId = 0;
	GetWindowThreadProcessId(window, &processId);
	if (processId != search->processId) return TRUE;
	if (!IsWindowVisible(window)) return TRUE;
	if (GetWindow(window, GW_OWNER) != nullptr) return TRUE;

	const LONG_PTR extendedStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
	if ((extendedStyle & WS_EX_TOOLWINDOW) != 0) return TRUE;

	wchar_t className[64] = {};
	GetClassNameW(window, className, ARRAYSIZE(className));
	if (wcscmp(className, OverlayWindow::ClassName()) == 0) return TRUE;

	RECT client{};
	if (!GetClientRect(window, &client)) return TRUE;
	const int64_t area = static_cast<int64_t>(client.right - client.left) *
	                     static_cast<int64_t>(client.bottom - client.top);
	if (area <= 0) return TRUE;

	if (area > search->bestArea) {
		search->bestArea = area;
		search->best = window;
	}
	return TRUE;
}

} // namespace

Runtime& Runtime::Instance() {
	static Runtime instance;
	return instance;
}

Runtime::~Runtime() {
	Shutdown();
}

void Runtime::Shutdown() noexcept {
	if (shutdownDone_) return;
	shutdownDone_ = true;

	stop_ = true;

	try {
		if (worker_.joinable()) worker_.join();
	} catch (...) {
		// Joining cannot fail in practice; never let teardown escape.
	}
	try {
		// Any queued game-thread task becomes a no-op; the module is pinned, so
		// a late APC cannot call into unmapped code.
		if (gameThread_ != nullptr) gameThread_->CancelPending();
	} catch (...) {
	}
	try {
		settings_.Flush();
	} catch (...) {
	}
	try {
		if (overlay_ != nullptr) overlay_->Shutdown();
	} catch (...) {
	}
	try {
		mythos::LogInfo("MYTHOS stopped (module pinned; restart the game to inject again)");
		mythos::Logger::Instance().Close();
	} catch (...) {
	}
}

HWND Runtime::FindTargetWindow(DWORD processId) {
	WindowSearch search;
	search.processId = processId;
	EnumWindows(&EnumWindowsCallback, reinterpret_cast<LPARAM>(&search));
	return search.best;
}

mythos::CaptureSettings Runtime::ToCaptureSettings(const mythos::OverlayConfig& config) {
	mythos::CaptureSettings capture;
	capture.name = config.players.name;
	capture.health = config.players.health;
	capture.distance = config.players.distance;
	capture.skeleton = config.players.skeleton;
	capture.headDot = config.players.headDot;
	capture.boxFromBones = config.players.boxFromBones;
	capture.showEnemy = config.players.showEnemy;
	capture.showTeam = config.players.showTeam;
	capture.showDrones = config.players.showDrones;
	capture.hideDead = config.players.hideDead;
	capture.visibility = config.players.visibleOnly || config.players.dimOccluded ||
	                     config.aim.visibleOnly;
	capture.retainAimCandidates = config.aim.enabled || config.aim.softAim;
	capture.maxDistanceMeters = capture.retainAimCandidates
		? 0.0
		: static_cast<double>(config.players.maxDistance);
	return capture;
}

mythos::RuntimeState Runtime::EvaluateState(const mythos::WorldContext& world,
                                          const mythos::GameSnapshot& snapshot) const {
	if (!world.valid) return mythos::RuntimeState::Resolving;
	const bool ready = world.proven && names_->ready() && snapshot.valid && snapshot.camera.valid;
	return ready ? mythos::RuntimeState::Ready : mythos::RuntimeState::Resolving;
}

void Runtime::PublishFrame(PublishedFrame frame) {
	std::lock_guard<std::mutex> lock(frameMutex_);
	published_ = std::move(frame);
}

Runtime::PublishedFrame Runtime::AcquireFrame() const {
	std::lock_guard<std::mutex> lock(frameMutex_);
	return published_;
}

void Runtime::RequestStop() {
	stop_ = true;
}

int Runtime::Run(HMODULE module) {
	(void)module; // the bootstrap thread owns the runtime lifetime; Run only needs it

	// RAII teardown: every exit path — normal return, early return, or an
	// exception escaping MainLoop — stops and joins the worker, cancels queued
	// game-thread tasks, flushes settings and destroys the overlay before
	// control leaves the runtime.
	struct ShutdownGuard {
		~ShutdownGuard() { Runtime::Instance().Shutdown(); }
	} shutdownGuard;
	(void)shutdownGuard;

	identity_ = QueryBuildIdentity();

	mythos::Logger::Instance().Open(platform::LogDirectory(), identity_.fingerprint);
	mythos::LogInfo("MYTHOS starting");
	mythos::LogInfo(identity_.fingerprint);

	settings_.Initialize(platform::SettingsPath(), platform::LegacySettingsPath());

	const std::wstring moduleName = platform::ToWide(Offsets::kGameModule);
	if (!memory_.Attach(moduleName.c_str())) {
		mythos::LogError("game module not mapped; stopping");
		return 1;
	}
	{
		mythos::SectionRange execSections[mythos::kMaxSections];
		mythos::SectionRange dataSections[mythos::kMaxSections];
		const int execCount = memory_.sections(true, execSections, mythos::kMaxSections);
		const int dataCount = memory_.sections(false, dataSections, mythos::kMaxSections);
		size_t dataBytes = 0;
		for (int i = 0; i < dataCount; ++i) dataBytes += dataSections[i].size;
		const mythos::ModuleInfo moduleInfo = memory_.module();
		char buffer[256] = {};
		std::snprintf(buffer, sizeof(buffer),
		              "module base=0x%llX size=0x%llX exec_sections=%d data_sections=%d "
		              "data_bytes=%zu",
		              static_cast<unsigned long long>(moduleInfo.base),
		              static_cast<unsigned long long>(moduleInfo.size), execCount, dataCount,
		              dataBytes);
		mythos::LogInfo(buffer);
	}

	names_ = std::make_unique<mythos::NamePool>(memory_);
	resolver_ = std::make_unique<mythos::WorldResolver>(memory_, *names_);
	gameThread_ = std::make_unique<GameThreadExecutor>();
	engineCalls_ = std::make_unique<EngineCalls>(memory_, *gameThread_);
	vischeck_ = std::make_unique<VisCheck>(memory_, *names_);
	collector_ = std::make_unique<mythos::SnapshotCollector>(memory_, *names_, vischeck_.get());
	aim_ = std::make_unique<AimController>(memory_, *engineCalls_);

	if (identity_.knownMismatch()) {
		resolver_->MarkOffsetsInvalid();
		mythos::LogError("known build mismatch: expected " +
		               std::string(Offsets::kActiveProfile.knownFileVersion) + ", found " +
		               identity_.fileVersion);
	} else if (identity_.fileVersion.empty()) {
		mythos::LogInfo("build version not discoverable; ESP is gated on world invariants");
	}

	// Publish a Starting frame so the UI has valid data immediately.
	{
		PublishedFrame initial;
		initial.diagnostics.buildFingerprint = identity_.fingerprint;
		initial.diagnostics.buildIdentity = identity_.fileVersion.empty()
			? std::string("Steam app ") + std::to_string(Offsets::kActiveProfile.steamAppId) +
				" build " + std::to_string(Offsets::kActiveProfile.steamBuild)
			: identity_.fileVersion;
		initial.diagnostics.state = mythos::RuntimeState::Starting;
		initial.diagnostics.stage = mythos::ResolveStage::NoModule;
		PublishFrame(std::move(initial));
	}

	const DWORD processId = GetCurrentProcessId();
	targetWindow_ = FindTargetWindow(processId);
	for (int waited = 0; targetWindow_ == nullptr && !stop_ && waited < 15000; waited += 100) {
		Sleep(100);
		targetWindow_ = FindTargetWindow(processId);
	}
	if (targetWindow_ == nullptr) {
		mythos::LogError("no target window found within 15 s; stopping");
		return 2;
	}
	mythos::LogInfo("target window found");

	// Prove the optional game-thread APC path before the AddYawInput/AddPitchInput
	// method is offered. Direct rotation writes and the separately opted-in,
	// reference-compatible vischeck do not depend on this probe.
	gameThread_->Initialize(targetWindow_);
	mythos::LogInfo("game-thread path: " + gameThread_->message());

	overlay_ = std::make_unique<OverlayWindow>();
	std::wstring error;
	if (!overlay_->Initialize(targetWindow_, &error)) {
		mythos::LogError("overlay initialization failed: " + platform::ToUtf8(error));
		overlay_.reset();
		return 3;
	}

	worker_ = std::thread(&Runtime::WorkerLoop, this);
	MainLoop();
	return 0;
}

void Runtime::MainLoop() {
	MSG message{};
	while (!stop_) {
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&message);
			DispatchMessageW(&message);
			if (message.message == WM_QUIT) stop_ = true;
		}
		if (stop_) break;

		if (overlay_ == nullptr) break;
		if (overlay_->CloseRequested()) {
			mythos::LogInfo("overlay close requested");
			break;
		}
		if (!IsWindow(targetWindow_)) {
			mythos::LogInfo("target window destroyed; stopping");
			break;
		}
		if (platform::ConsumeKeyPress(Offsets::Keys::Unload)) {
			mythos::LogInfo("stop key pressed; MYTHOS stops (restart the game to inject again)");
			break;
		}
		if (platform::ConsumeKeyPress(Offsets::Keys::MenuToggle)) {
			const HWND foreground = GetForegroundWindow();
			const bool relevant = foreground == targetWindow_ || foreground == overlay_->Window();
			if (relevant) {
				const bool visible = !overlay_->MenuVisible();
				overlay_->SetMenuVisible(visible);
				uiState_.menuVisible = visible;
				ImGui::GetIO().MouseDrawCursor = visible;
			}
		}

		settings_.Tick(platform::MonotonicMilliseconds());

		if (overlay_->RendererFailed()) {
			mythos::LogError("renderer failure; stopping");
			break;
		}
		if (!overlay_->SyncToTarget()) {
			Sleep(30);
			continue;
		}

		PublishedFrame frame = AcquireFrame();
		const uint64_t nowMs = platform::MonotonicMilliseconds();
		if (frame.snapshot != nullptr) {
			frame.diagnostics.snapshotAgeMs =
				static_cast<uint32_t>(nowMs - frame.snapshot->capturedAtMs);
		}
		frame.diagnostics.overlayFps = ImGui::GetIO().Framerate;
		frame.diagnostics.rendererOk = true;

		float width = 0.0f;
		float height = 0.0f;
		if (!overlay_->BeginFrame(&width, &height)) {
			Sleep(16);
			continue;
		}
		viewportWidth_ = width;
		viewportHeight_ = height;

		const mythos::OverlayConfig config = settings_.Snapshot();
		renderBones_.reset();
		frame.collection.entities.drawn = 0;
		frame.collection.entities.noProjection = 0;
		frame.collection.entities.offScreen = 0;
		if (config.espEnabled && frame.snapshot != nullptr && frame.snapshot->valid &&
		    frame.diagnostics.state == mythos::RuntimeState::Ready) {
			esp_.Draw(*frame.snapshot, config, width, height, renderBones_,
			          frame.collection.entities);
		}
		frame.collection.bones.skeletonsDrawn = renderBones_.skeletonsDrawn;
		esp_.DrawAimOverlay(config, frame.aim, width, height);

		if (overlay_->MenuVisible()) {
			ui_.Draw(settings_, frame.diagnostics, frame.resolver, frame.collection,
			         frame.aim, frame.engineCalls, frame.vischeck,
			         config, uiState_, platform::AnimationsEnabled());
		}

		overlay_->EndFrame();
	}
}

void Runtime::WorkerLoop() {
	// An exception must never escape a thread function (std::terminate), and
	// the overlay thread must not tear the runtime down while this loop is alive.
	try {
		WorkerLoopImpl();
	} catch (const std::exception& exception) {
		stop_ = true;
		try {
			mythos::LogError(std::string("worker exception: ") + exception.what());
		} catch (...) {
		}
	} catch (...) {
		stop_ = true;
		try {
			mythos::LogError("worker exception");
		} catch (...) {
		}
	}
}

void Runtime::WorkerLoopImpl() {
	uint64_t sequence = 0;

	while (!stop_) {
		const uint64_t startMs = platform::MonotonicMilliseconds();
		const mythos::OverlayConfig config = settings_.Snapshot();

		if (!resolver_->offsetsInvalid()) {
			const bool resolved = resolver_->Resolve();
			// Fallback work also keeps the FNamePool resolution moving even
			// when the world chain is already valid.
			if (!resolved || !names_->ready()) {
				(void)resolver_->PumpFallback(startMs);
			}
			// Quarantined engine interaction: all calls require owner opt-in.
			// AddYawInput/AddPitchInput additionally require the verified APC path;
			// the reference-compatible ProcessEvent vischeck and direct rotation
			// writes run synchronously on this worker. When aim is active, capture
			// retains candidates hidden by ESP; the renderer applies ESP-only
			// filters after aim selection.
			engineCalls_->SetEngineCallsEnabled(config.unsafeEngineCalls);
			vischeck_->SetEngineCallsEnabled(config.unsafeEngineCalls);
			engineCalls_->Resolve(kEngineScanStepBytes);
			vischeck_->Tick(resolver_->context().playerController);
			const std::string& vischeckMessage = vischeck_->status().message;
			if (vischeckMessage != lastVischeckMessage_) {
				lastVischeckMessage_ = vischeckMessage;
				mythos::LogInfo("vischeck: " + vischeckMessage);
			}
		}

		mythos::GameSnapshotPtr snapshot = collector_->Capture(
			resolver_->context(), resolver_->stage(), ToCaptureSettings(config), ++sequence, startMs);

		if (snapshot->valid && snapshot->camera.valid && resolver_->context().valid &&
		    !resolver_->offsetsInvalid()) {
			mythos::ProjectionSettings projection;
			projection.axisOverride = config.projection.axisOverride;
			projection.fovScale = static_cast<double>(config.projection.fovScale);
			projection.fallbackFov = config.projection.fallbackFov;
			aim_->Tick(resolver_->context(), *snapshot, config.aim, projection,
			           viewportWidth_.load(), viewportHeight_.load());
		}

		mythos::RuntimeState state = EvaluateState(resolver_->context(), *snapshot);
		if (resolver_->offsetsInvalid()) state = mythos::RuntimeState::OffsetsInvalid;

		if (!stateLogged_ || state != lastLoggedState_) {
			stateLogged_ = true;
			lastLoggedState_ = state;
			mythos::LogInfo(std::string("state -> ") + mythos::RuntimeStateName(state) +
			              " | chain: " + mythos::ResolveStageName(resolver_->stage()) +
			              " | names: " + (names_->ready() ? "ready" : "pending") +
			              " | roster=" + std::to_string(resolver_->context().playerCount) +
			              " | proven: " + (resolver_->context().proven ? "yes" : "no"));
		}
		if (resolver_->context().valid && !worldLogged_) {
			worldLogged_ = true;
			const mythos::ModuleInfo moduleInfo = memory_.module();
			const uintptr_t anchorSlot = resolver_->diagnostics().anchorSlot;
			const uintptr_t anchorRva =
				(moduleInfo.base != 0 && anchorSlot >= moduleInfo.base)
					? anchorSlot - moduleInfo.base
					: 0;
			char buffer[320] = {};
			std::snprintf(buffer, sizeof(buffer),
			              "world chain resolved: world=0x%llX controller=0x%llX "
			              "camera=0x%llX roster=%d proven=%s | anchor_rva=0x%llX source=%s",
			              static_cast<unsigned long long>(resolver_->context().world),
			              static_cast<unsigned long long>(resolver_->context().playerController),
			              static_cast<unsigned long long>(resolver_->context().cameraManager),
			              resolver_->context().playerCount,
			              resolver_->context().proven ? "yes" : "no",
			              static_cast<unsigned long long>(anchorRva),
			              resolver_->diagnostics().worldFromRva ? "known RVA" : "scan");
			mythos::LogInfo(buffer);
		}
		if (names_->ready() && !namesLogged_) {
			namesLogged_ = true;
			char buffer[160] = {};
			std::snprintf(buffer, sizeof(buffer), "name pool ready at 0x%llX",
			              static_cast<unsigned long long>(names_->poolAddress()));
			mythos::LogInfo(buffer);
		}

		if (state == mythos::RuntimeState::Ready) {
			if (!everReady_) {
				everReady_ = true;
				mythos::LogInfo("runtime ready: world, camera, roster and name pool validated");
			}
			if (recovering_) {
				recovering_ = false;
				mythos::LogInfo("recovered after map change");
			}
			notReadyStreak_ = 0;
		} else if (state == mythos::RuntimeState::Resolving && everReady_) {
			++notReadyStreak_;
			if (notReadyStreak_ >= kRecoverAfterTicks && !recovering_) {
				recovering_ = true;
				collector_->ClearCaches();
				resolver_->OnMapTransition();
				vischeck_->OnWorldReset();
				mythos::LogInfo("world lost; clearing caches for map transition");
			}
		}

		const mythos::ResolverDiagnostics& resolverDiagnostics = resolver_->diagnostics();
		const mythos::ModuleInfo module = memory_.module();

		mythos::RuntimeDiagnostics diagnostics;
		diagnostics.buildFingerprint = identity_.fingerprint;
		diagnostics.buildIdentity = identity_.fileVersion.empty()
			? std::string("Steam app ") + std::to_string(Offsets::kActiveProfile.steamAppId) +
				" build " + std::to_string(Offsets::kActiveProfile.steamBuild)
			: identity_.fileVersion;
		diagnostics.state = state;
		diagnostics.stage = resolver_->stage();
		diagnostics.namesReady = names_->ready();
		diagnostics.worldValid = resolver_->context().valid;
		diagnostics.cameraValid = snapshot->camera.valid;
		diagnostics.recovering = recovering_;
		diagnostics.rendererOk = overlay_ == nullptr || !overlay_->RendererFailed();
		diagnostics.anchorRva =
			(resolverDiagnostics.anchorSlot >= module.base && module.base != 0)
				? resolverDiagnostics.anchorSlot - module.base
				: 0;
		diagnostics.worldPointer = resolver_->context().world;
		diagnostics.lastScanMs = resolverDiagnostics.lastScanMs;
		diagnostics.candidates = resolverDiagnostics.candidates;
		diagnostics.fullScans = resolverDiagnostics.fullScans;
		diagnostics.reanchors = resolverDiagnostics.reanchors;
		diagnostics.rescues = resolverDiagnostics.rescues;
		diagnostics.readFailures =
			resolverDiagnostics.readFailures + collector_->diagnostics().readFailures;
		diagnostics.skeletonCacheSize = static_cast<int>(collector_->skeletons().size());
		diagnostics.skeletonNamed = collector_->skeletons().namedCount();
		diagnostics.skeletonCoreBones = collector_->skeletons().totalCoreBones();
		diagnostics.refSkeletonOffset = collector_->skeletons().refSkeletonOffset();
		diagnostics.entities = snapshot->counters;
		diagnostics.sequence = snapshot->sequence;

		PublishedFrame frame;
		frame.snapshot = std::move(snapshot);
		frame.diagnostics = diagnostics;
		frame.resolver = resolverDiagnostics;
		frame.collection = collector_->diagnostics();
		engineCalls_->RefreshStatus();
		frame.aim = aim_->telemetry();
		frame.engineCalls = engineCalls_->status();
		frame.vischeck = vischeck_->status();
		PublishFrame(std::move(frame));

		const uint64_t elapsed = platform::MonotonicMilliseconds() - startMs;
		if (elapsed < kWorkerIntervalMs) {
			std::this_thread::sleep_for(std::chrono::milliseconds(kWorkerIntervalMs - elapsed));
		}
	}
}

} // namespace mythos_host
