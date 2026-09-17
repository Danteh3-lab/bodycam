// ============================================================================
// MYTHOS.dll entry point.
//
// DllMain stays minimal: thread notifications are disabled (we do not need
// them) and a single bootstrap thread starts all initialization after the
// loader lock has been released. DELETE stops MYTHOS; once the game-thread path
// is opened the module is pinned and stays mapped until the game exits. The
// game process is never terminated.
// ============================================================================
#include "Runtime.hpp"

#include "mythos/Logging.hpp"

#include <Windows.h>

#include <exception>
#include <string>

namespace {

DWORD WINAPI BootstrapThread(LPVOID parameter) {
	auto* module = static_cast<HMODULE>(parameter);

	// Nothing may escape this thread: an unhandled exception would terminate
	// the game process. Teardown (stop/join the worker, cancel queued
	// game-thread tasks, flush settings, destroy the overlay) must complete
	// before control leaves the runtime; Shutdown() is idempotent and
	// non-throwing so it is safe on every path.
	int exitCode = 1;
	try {
		exitCode = mythos_host::Runtime::Instance().Run(module);
	} catch (const std::exception& exception) {
		mythos_host::Runtime::Instance().Shutdown();
		try {
			mythos::LogError(std::string("unhandled exception during runtime: ") + exception.what());
		} catch (...) {
		}
	} catch (...) {
		mythos_host::Runtime::Instance().Shutdown();
		try {
			mythos::LogError("unhandled exception during runtime");
		} catch (...) {
		}
	}

	// The module is pinned once the game-thread path is opened, so a pending
	// APC can never call into unmapped code. Shutdown() has joined the worker
	// and cancelled queued tasks before control reaches this point.
	FreeLibraryAndExitThread(module, static_cast<DWORD>(exitCode));
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
	UNREFERENCED_PARAMETER(reserved);

	switch (reason) {
	case DLL_PROCESS_ATTACH: {
		DisableThreadLibraryCalls(module);
		HANDLE thread = CreateThread(nullptr, 0, &BootstrapThread, module, 0, nullptr);
		if (thread == nullptr) return FALSE;
		CloseHandle(thread);
		return TRUE;
	}
	case DLL_PROCESS_DETACH:
	default:
		return TRUE;
	}
}
