#include "Runtime.hpp"

#include "Platform.hpp"

#include "Offsets.hpp"
#include "nova/Logging.hpp"

#include <imgui.h>

#include <chrono>
#include <cstdio>
#include <string>

namespace nova_host {
namespace {

constexpr uint64_t kWorkerIntervalMs = 16;   // ~60 Hz sampling
constexpr int      kRecoverAfterTicks = 30;  // ~0.5 s before declaring recovery

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
		settings_.Flush();
	} catch (...) {
	}
	try {
		if (overlay_ != nullptr) overlay_->Shutdown();
	} catch (...) {
	}
	try {
		nova::LogInfo("NOVA unloaded");
		nova::Logger::Instance().Close();
	} catch (...) {
	}
}

HWND Runtime::FindTargetWindow(DWORD processId) {
	WindowSearch search;
	search.processId = processId;
	EnumWindows(&EnumWindowsCallback, reinterpret_cast<LPARAM>(&search));
	return search.best;
}

nova::CaptureSettings Runtime::ToCaptureSettings(const nova::OverlayConfig& config) {
	nova::CaptureSettings capture;
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
	capture.maxDistanceMeters = static_cast<double>(config.players.maxDistance);
	return capture;
}

nova::RuntimeState Runtime::EvaluateState(const nova::WorldContext& world,
                                          const nova::GameSnapshot& snapshot) const {
	if (!world.valid) return nova::RuntimeState::Resolving;
	const bool ready = world.proven && names_->ready() && snapshot.valid && snapshot.camera.valid;
	return ready ? nova::RuntimeState::Ready : nova::RuntimeState::Resolving;
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
	(void)module; // the bootstrap thread owns unloading; Run only needs the lifetime

	// RAII teardown: every exit path — normal return, early return, or an
	// exception escaping MainLoop — stops and joins the worker, flushes
	// settings and destroys the overlay before the DLL can be unloaded.
	struct ShutdownGuard {
		~ShutdownGuard() { Runtime::Instance().Shutdown(); }
	} shutdownGuard;
	(void)shutdownGuard;

	identity_ = QueryBuildIdentity();

	nova::Logger::Instance().Open(platform::LogDirectory(), identity_.fingerprint);
	nova::LogInfo("NOVA starting");
	nova::LogInfo(identity_.fingerprint);

	settings_.Initialize(platform::SettingsPath());

	const std::wstring moduleName = platform::ToWide(Offsets::kGameModule);
	if (!memory_.Attach(moduleName.c_str())) {
		nova::LogError("game module not mapped; unloading");
		return 1;
	}
	{
		nova::SectionRange execSections[nova::kMaxSections];
		nova::SectionRange dataSections[nova::kMaxSections];
		const int execCount = memory_.sections(true, execSections, nova::kMaxSections);
		const int dataCount = memory_.sections(false, dataSections, nova::kMaxSections);
		size_t dataBytes = 0;
		for (int i = 0; i < dataCount; ++i) dataBytes += dataSections[i].size;
		const nova::ModuleInfo moduleInfo = memory_.module();
		char buffer[256] = {};
		std::snprintf(buffer, sizeof(buffer),
		              "module base=0x%llX size=0x%llX exec_sections=%d data_sections=%d "
		              "data_bytes=%zu",
		              static_cast<unsigned long long>(moduleInfo.base),
		              static_cast<unsigned long long>(moduleInfo.size), execCount, dataCount,
		              dataBytes);
		nova::LogInfo(buffer);
	}

	names_ = std::make_unique<nova::NamePool>(memory_);
	resolver_ = std::make_unique<nova::WorldResolver>(memory_, *names_);
	collector_ = std::make_unique<nova::SnapshotCollector>(memory_, *names_);

	if (identity_.knownMismatch()) {
		resolver_->MarkOffsetsInvalid();
		nova::LogError("known build mismatch: expected " +
		               std::string(Offsets::kActiveProfile.knownFileVersion) + ", found " +
		               identity_.fileVersion);
	} else if (identity_.fileVersion.empty()) {
		nova::LogInfo("build version not discoverable; ESP is gated on world invariants");
	}

	// Publish a Starting frame so the UI has valid data immediately.
	{
		PublishedFrame initial;
		initial.diagnostics.buildFingerprint = identity_.fingerprint;
		initial.diagnostics.buildIdentity = identity_.fileVersion.empty()
			? std::string("Steam app ") + std::to_string(Offsets::kActiveProfile.steamAppId) +
				" build " + std::to_string(Offsets::kActiveProfile.steamBuild)
			: identity_.fileVersion;
		initial.diagnostics.state = nova::RuntimeState::Starting;
		initial.diagnostics.stage = nova::ResolveStage::NoModule;
		PublishFrame(std::move(initial));
	}

	const DWORD processId = GetCurrentProcessId();
	targetWindow_ = FindTargetWindow(processId);
	for (int waited = 0; targetWindow_ == nullptr && !stop_ && waited < 15000; waited += 100) {
		Sleep(100);
		targetWindow_ = FindTargetWindow(processId);
	}
	if (targetWindow_ == nullptr) {
		nova::LogError("no target window found within 15 s; unloading");
		return 2;
	}
	nova::LogInfo("target window found");

	overlay_ = std::make_unique<OverlayWindow>();
	std::wstring error;
	if (!overlay_->Initialize(targetWindow_, &error)) {
		nova::LogError("overlay initialization failed: " + platform::ToUtf8(error));
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
			nova::LogInfo("overlay close requested");
			break;
		}
		if (!IsWindow(targetWindow_)) {
			nova::LogInfo("target window destroyed; unloading");
			break;
		}
		if (platform::ConsumeKeyPress(Offsets::Keys::Unload)) {
			nova::LogInfo("unload key pressed");
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
			nova::LogError("renderer failure; unloading");
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

		const nova::OverlayConfig config = settings_.Snapshot();
		if (config.espEnabled && frame.snapshot != nullptr && frame.snapshot->valid &&
		    frame.diagnostics.state == nova::RuntimeState::Ready) {
			esp_.Draw(*frame.snapshot, config, width, height, renderBones_);
		}

		if (overlay_->MenuVisible()) {
			ui_.Draw(settings_, frame.diagnostics, frame.resolver, frame.collection,
			         config, uiState_, platform::AnimationsEnabled());
		}

		overlay_->EndFrame();
	}
}

void Runtime::WorkerLoop() {
	// An exception must never escape a thread function (std::terminate), and
	// the overlay thread must not unload the DLL while this loop is alive.
	try {
		WorkerLoopImpl();
	} catch (const std::exception& exception) {
		stop_ = true;
		try {
			nova::LogError(std::string("worker exception: ") + exception.what());
		} catch (...) {
		}
	} catch (...) {
		stop_ = true;
		try {
			nova::LogError("worker exception");
		} catch (...) {
		}
	}
}

void Runtime::WorkerLoopImpl() {
	uint64_t sequence = 0;

	while (!stop_) {
		const uint64_t startMs = platform::MonotonicMilliseconds();
		const nova::OverlayConfig config = settings_.Snapshot();

		if (!resolver_->offsetsInvalid()) {
			const bool resolved = resolver_->Resolve();
			// Fallback work also keeps the FNamePool resolution moving even
			// when the world chain is already valid.
			if (!resolved || !names_->ready()) {
				(void)resolver_->PumpFallback(startMs);
			}
		}

		nova::GameSnapshotPtr snapshot = collector_->Capture(
			resolver_->context(), resolver_->stage(), ToCaptureSettings(config), ++sequence, startMs);

		nova::RuntimeState state = EvaluateState(resolver_->context(), *snapshot);
		if (resolver_->offsetsInvalid()) state = nova::RuntimeState::OffsetsInvalid;

		if (!stateLogged_ || state != lastLoggedState_) {
			stateLogged_ = true;
			lastLoggedState_ = state;
			nova::LogInfo(std::string("state -> ") + nova::RuntimeStateName(state) +
			              " | chain: " + nova::ResolveStageName(resolver_->stage()) +
			              " | names: " + (names_->ready() ? "ready" : "pending") +
			              " | roster=" + std::to_string(resolver_->context().playerCount) +
			              " | proven: " + (resolver_->context().proven ? "yes" : "no"));
		}
		if (resolver_->context().valid && !worldLogged_) {
			worldLogged_ = true;
			const nova::ModuleInfo moduleInfo = memory_.module();
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
			nova::LogInfo(buffer);
		}
		if (names_->ready() && !namesLogged_) {
			namesLogged_ = true;
			char buffer[160] = {};
			std::snprintf(buffer, sizeof(buffer), "name pool ready at 0x%llX",
			              static_cast<unsigned long long>(names_->poolAddress()));
			nova::LogInfo(buffer);
		}

		if (state == nova::RuntimeState::Ready) {
			if (!everReady_) {
				everReady_ = true;
				nova::LogInfo("runtime ready: world, camera, roster and name pool validated");
			}
			if (recovering_) {
				recovering_ = false;
				nova::LogInfo("recovered after map change");
			}
			notReadyStreak_ = 0;
		} else if (state == nova::RuntimeState::Resolving && everReady_) {
			++notReadyStreak_;
			if (notReadyStreak_ >= kRecoverAfterTicks && !recovering_) {
				recovering_ = true;
				collector_->ClearCaches();
				resolver_->OnMapTransition();
				nova::LogInfo("world lost; clearing caches for map transition");
			}
		}

		const nova::ResolverDiagnostics& resolverDiagnostics = resolver_->diagnostics();
		const nova::ModuleInfo module = memory_.module();

		nova::RuntimeDiagnostics diagnostics;
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
		PublishFrame(std::move(frame));

		const uint64_t elapsed = platform::MonotonicMilliseconds() - startMs;
		if (elapsed < kWorkerIntervalMs) {
			std::this_thread::sleep_for(std::chrono::milliseconds(kWorkerIntervalMs - elapsed));
		}
	}
}

} // namespace nova_host
