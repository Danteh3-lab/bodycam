// ============================================================================
// Static read-only contract test.
//
// Scans the core and DLL sources (comments and string literals stripped) and
// rejects memory-write helpers, patching APIs, engine-call references and
// aim/input symbols. The loader's isolated DLL-path injection is intentionally
// outside this scan and is asserted separately.
// ============================================================================
#include "test_framework.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

bool IsIdentifierChar(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') || c == '_';
}

bool ContainsToken(const std::string& text, const std::string& token) {
	if (token.empty() || text.size() < token.size()) return false;
	size_t position = text.find(token);
	while (position != std::string::npos) {
		const bool leftOk = position == 0 || !IsIdentifierChar(text[position - 1]);
		const size_t end = position + token.size();
		const bool rightOk = end >= text.size() || !IsIdentifierChar(text[end]);
		if (leftOk && rightOk) return true;
		position = text.find(token, position + 1);
	}
	return false;
}

// Replaces comments and string/char literal contents with spaces so that
// prose or display strings cannot trigger (or hide) a match.
std::string StripCommentsAndLiterals(const std::string& source) {
	std::string output = source;
	enum class State { Normal, LineComment, BlockComment, String, Character };
	State state = State::Normal;

	for (size_t i = 0; i < output.size(); ++i) {
		const char c = output[i];
		const char next = (i + 1 < output.size()) ? output[i + 1] : '\0';

		switch (state) {
		case State::Normal:
			if (c == '/' && next == '/') {
				state = State::LineComment;
				output[i] = ' ';
				output[i + 1] = ' ';
				++i;
			} else if (c == '/' && next == '*') {
				state = State::BlockComment;
				output[i] = ' ';
				output[i + 1] = ' ';
				++i;
			} else if (c == '"') {
				state = State::String;
			} else if (c == '\'') {
				state = State::Character;
			}
			break;

		case State::LineComment:
			if (c == '\n') {
				state = State::Normal;
			} else {
				output[i] = ' ';
			}
			break;

		case State::BlockComment:
			if (c == '*' && next == '/') {
				output[i] = ' ';
				output[i + 1] = ' ';
				++i;
				state = State::Normal;
			} else if (c != '\n') {
				output[i] = ' ';
			}
			break;

		case State::String:
			if (c == '\\') {
				output[i] = ' ';
				if (i + 1 < output.size()) output[++i] = ' ';
			} else if (c == '"') {
				state = State::Normal;
			} else if (c != '\n') {
				output[i] = ' ';
			}
			break;

		case State::Character:
			if (c == '\\') {
				output[i] = ' ';
				if (i + 1 < output.size()) output[++i] = ' ';
			} else if (c == '\'') {
				state = State::Normal;
			} else if (c != '\n') {
				output[i] = ' ';
			}
			break;
		}
	}
	return output;
}

std::vector<std::filesystem::path> CollectSources(const std::filesystem::path& root,
                                                  std::initializer_list<const char*> directories) {
	std::vector<std::filesystem::path> files;
	for (const char* directory : directories) {
		const std::filesystem::path base = root / directory;
		std::error_code code;
		if (!std::filesystem::exists(base, code)) continue;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(base, code)) {
			if (!entry.is_regular_file()) continue;
			const std::string extension = entry.path().extension().string();
			if (extension == ".cpp" || extension == ".hpp" || extension == ".h" ||
			    extension == ".inl") {
				files.push_back(entry.path());
			}
		}
	}
	return files;
}

std::string ReadFile(const std::filesystem::path& path) {
	std::ifstream stream(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(stream)),
	                   std::istreambuf_iterator<char>());
}

// True when the stripped text declares `name = <hex>;` with exactly that
// literal on the right-hand side. Used for pinned offset values where a bare
// token search could match an unrelated constant (e.g. 0x73 also appears as
// the AddInput tail offset).
bool DeclaresHex(const std::string& text, const std::string& name, const std::string& hex) {
	const size_t namePos = text.find(name);
	if (namePos == std::string::npos) return false;
	if (namePos != 0 && IsIdentifierChar(text[namePos - 1])) return false;
	const size_t end = namePos + name.size();
	if (end < text.size() && IsIdentifierChar(text[end])) return false;
	const size_t equals = text.find('=', end);
	if (equals == std::string::npos || equals > end + 32) return false;
	const size_t semicolon = text.find(';', equals);
	if (semicolon == std::string::npos) return false;
	return text.substr(equals + 1, semicolon - equals - 1).find(hex) != std::string::npos;
}

// Never allowed anywhere in core/ or dll/, at any time: patching, injection,
// hooks and unbounded module loads. Only the loader may inject (asserted
// separately), and engine interaction is quarantined to the modules below.
const char* const kForbiddenTokens[] = {
	"VirtualProtect",
	"VirtualAlloc",
	"WriteProcessMemory",
	"NtWriteVirtualMemory",
	"MapViewOfFile",
	"CreateRemoteThread",
	"SafeWrite",
	"Detour",
	"MinHook",
	"MH_Initialize",
	"LoadLibrary",
};

// Engine interaction (calls, aim fields, the write helper) is only allowed in
// the quarantine modules; every other core/dll file must stay free of them.
const char* const kEngineInteractionTokens[] = {
	"Offsets::Aim",
	"Offsets::EngineCalls",
	"Offsets::VisCheck",
	"ProcessEvent",
	"AddPitchInput",
	"AddYawInput",
	"RotationInput",
	"RotationInputPitch",
	"RotationInputYaw",
	"ControlRotation",
	"GuardedWrite",
	"QueueUserAPC",
};

const char* const kQuarantineModules[] = {
	"EngineCalls",
	"VisCheck",
	"AimController",
	"GameThread",
};

bool IsQuarantinedModule(const std::filesystem::path& path) {
	const std::string stem = path.stem().string();
	for (const char* module : kQuarantineModules) {
		if (stem == module) return true;
	}
	return false;
}

} // namespace

MYTHOS_TEST(ContractScannerSelfTest) {
	const std::string text = "int x = VirtualProtect(); // VirtualAlloc\ndeadbeef";
	CHECK(ContainsToken(text, "VirtualProtect"));
	CHECK(!ContainsToken(text, "VirtualAllo"));
	CHECK(ContainsToken(StripCommentsAndLiterals("// VirtualProtect\nint x;"), "int x"));

	const std::string stripped = StripCommentsAndLiterals(
		"const char* s = \"VirtualProtect\"; // VirtualAlloc\n/* Detour */ int y;");
	CHECK(!ContainsToken(stripped, "VirtualProtect"));
	CHECK(!ContainsToken(stripped, "VirtualAlloc"));
	CHECK(!ContainsToken(stripped, "Detour"));
	CHECK(ContainsToken(stripped, "int y"));
}

MYTHOS_TEST(CoreIsStrictlyReadOnly) {
	// mythos_core never writes memory, patches code or calls engine functions.
	const std::filesystem::path root = MYTHOS_SOURCE_DIR;
	const std::vector<std::filesystem::path> files = CollectSources(root, { "core" });
	CHECK(files.size() >= 15); // guard against a vacuous scan

	int violations = 0;
	for (const std::filesystem::path& path : files) {
		const std::string text = StripCommentsAndLiterals(ReadFile(path));
		for (const char* token : kForbiddenTokens) {
			if (!ContainsToken(text, token)) continue;
			++violations;
			std::printf("    forbidden token '%s' in %s\n", token, path.filename().string().c_str());
		}
		for (const char* token : kEngineInteractionTokens) {
			if (!ContainsToken(text, token)) continue;
			++violations;
			std::printf("    engine token '%s' in core file %s\n", token,
			            path.filename().string().c_str());
		}
	}
	CHECK_EQ(violations, 0);
}

MYTHOS_TEST(EngineInteractionIsQuarantinedToDedicatedModules) {
	const std::filesystem::path root = MYTHOS_SOURCE_DIR;
	const std::vector<std::filesystem::path> files = CollectSources(root, { "core", "dll" });
	CHECK(files.size() >= 20); // guard against a vacuous scan

	int violations = 0;
	for (const std::filesystem::path& path : files) {
		const std::string text = StripCommentsAndLiterals(ReadFile(path));
		for (const char* token : kForbiddenTokens) {
			if (!ContainsToken(text, token)) continue;
			++violations;
			std::printf("    forbidden token '%s' in %s\n", token, path.filename().string().c_str());
		}
		if (IsQuarantinedModule(path)) continue;
		for (const char* token : kEngineInteractionTokens) {
			if (!ContainsToken(text, token)) continue;
			++violations;
			std::printf("    engine token '%s' outside quarantine: %s\n", token,
			            path.filename().string().c_str());
		}
	}
	CHECK_EQ(violations, 0);

	// The quarantine modules must actually contain their approved APIs.
	const std::filesystem::path dllRoot = root / "dll" / "src";
	const std::string engineSource = ReadFile(dllRoot / "EngineCalls.cpp");
	const std::string engine = StripCommentsAndLiterals(engineSource);
	const std::string vischeck = StripCommentsAndLiterals(ReadFile(dllRoot / "VisCheck.cpp"));
	const std::string vischeckHeader =
		StripCommentsAndLiterals(ReadFile(dllRoot / "VisCheck.hpp"));
	const std::string aim = StripCommentsAndLiterals(ReadFile(dllRoot / "AimController.cpp"));
	const std::string gameThread = StripCommentsAndLiterals(ReadFile(dllRoot / "GameThread.cpp"));
	CHECK(ContainsToken(engine, "AddPitchInput"));
	CHECK(ContainsToken(engine, "AddYawInput"));
	CHECK(ContainsToken(engine, "GuardedWriteDouble"));
	CHECK(ContainsToken(engine, "Offsets::Aim"));
	CHECK(ContainsToken(vischeck, "ProcessEvent"));
	CHECK(ContainsToken(vischeck, "IsVisible"));
	CHECK(ContainsToken(vischeck, "SafeProcessEvent"));
	CHECK(ContainsToken(vischeckHeader, "VisibilityProbe"));
	CHECK(!ContainsToken(vischeck, "gameThread_.Execute"));
	CHECK(!ContainsToken(vischeck, "gameThread_.verified"));
	CHECK(!ContainsToken(vischeckHeader, "GameThread"));
	CHECK(ContainsToken(aim, "ControlRotation"));
	CHECK(ContainsToken(aim, "SelectAimTarget"));
	CHECK(ContainsToken(gameThread, "QueueUserAPC"));
	CHECK(ContainsToken(gameThread, "GET_MODULE_HANDLE_EX_FLAG_PIN"));
	CHECK(ContainsToken(gameThread, "CancelPending"));

	// ProcessEvent identity is the measured one, cross-checked RVA <-> vtable,
	// with no speculative slots and no "any executable address" fallback.
	const std::string offsets = StripCommentsAndLiterals(ReadFile(root / "Offsets.hpp"));
	CHECK(ContainsToken(offsets, "0x034E4A60"));
	CHECK(ContainsToken(offsets, "0x4F"));
	CHECK(ContainsToken(offsets, "NativeFunctionPrologue"));
	// Complete pinned build-25368976 identity: PE fields, globals, input RVAs,
	// and the FBoolProperty byte/mask offsets.
	CHECK(ContainsToken(offsets, "0x0A720000u"));
	CHECK(ContainsToken(offsets, "0x8E1C799Au"));
	CHECK(ContainsToken(offsets, "0x0A2E9763u"));
	CHECK(ContainsToken(offsets, "0x099E3040"));
	CHECK(ContainsToken(offsets, "0x09C42738"));
	CHECK(DeclaresHex(offsets, "AddPitchInput", "0x3CB9DF0"));
	CHECK(DeclaresHex(offsets, "AddYawInput", "0x3CBA000"));
	CHECK(DeclaresHex(offsets, "ProcessEvent", "0x034E4A60"));
	CHECK(DeclaresHex(offsets, "BoolByteOffset", "0x71"));
	CHECK(DeclaresHex(offsets, "BoolFieldMask", "0x73"));
	CHECK(DeclaresHex(offsets, "PSTeamId", "0x398"));
	CHECK(DeclaresHex(offsets, "PSKills", "0x39C"));
	CHECK(DeclaresHex(offsets, "PSDeaths", "0x3A0"));
	CHECK(ContainsToken(vischeck, "ProcessEventIdx"));
	CHECK(ContainsToken(vischeck, "rvaCandidate"));
	CHECK(ContainsToken(vischeck, "slotCandidate"));
	CHECK(ContainsToken(vischeck, "MatchesVerifiedNativePrologue"));
	CHECK(!ContainsToken(vischeck, "LooksLikeProcessEvent"));
	CHECK(!ContainsToken(vischeck, "0x42"));
	CHECK(!ContainsToken(vischeck, "0x43"));
	CHECK(!ContainsToken(vischeck, "0x44"));
	// Diagnostic messages are string literals, so check the raw source.
	const std::string vischeckRaw = ReadFile(dllRoot / "VisCheck.cpp");
	CHECK(vischeckRaw.find("ProcessEvent RVA outside the executable image") != std::string::npos);
	CHECK(vischeckRaw.find("ProcessEvent vtable slot unreadable") != std::string::npos);
	CHECK(vischeckRaw.find("ProcessEvent RVA/vtable mismatch") != std::string::npos);
	CHECK(vischeckRaw.find("ProcessEvent signature mismatch") != std::string::npos);

	// Reference-compatible direct methods must not be blocked by the optional
	// APC path. Keep engine-function methods and ProcessEvent on the executor,
	// but ensure the two direct-write regions perform their guarded reads and
	// writes synchronously from MYTHOS's worker thread.
	const size_t directStart = engine.find("EngineCalls::AddLookInputDirect");
	const size_t controlStart = engine.find("EngineCalls::SetControlRotation");
	CHECK(directStart != std::string::npos);
	CHECK(controlStart != std::string::npos);
	if (directStart != std::string::npos && controlStart != std::string::npos &&
	    directStart < controlStart) {
		const std::string direct = engine.substr(directStart, controlStart - directStart);
		CHECK(ContainsToken(direct, "ReadDouble"));
		CHECK(ContainsToken(direct, "GuardedWriteDouble"));
		CHECK(!ContainsToken(direct, "gameThread_.Execute"));
		CHECK(!ContainsToken(direct, "gameThread_.verified"));
	}
	// Use the unstripped source only to locate the namespace terminator; the
	// stripped buffer keeps identical offsets while removing comment text.
	const size_t controlEnd = engineSource.find("} // namespace mythos_host", controlStart);
	if (controlStart != std::string::npos && controlEnd != std::string::npos &&
	    controlStart < controlEnd) {
		const std::string control = engine.substr(controlStart, controlEnd - controlStart);
		CHECK(ContainsToken(control, "GuardedWriteDouble"));
		CHECK(!ContainsToken(control, "gameThread_.Execute"));
		CHECK(!ContainsToken(control, "gameThread_.verified"));
	}
	CHECK(!ContainsToken(aim, "gameThreadVerified"));

	// Ordering, not just presence: the module is pinned before any APC can be
	// queued, and shutdown joins the worker before cancelling queued tasks.
	const size_t pinPos = gameThread.find("GET_MODULE_HANDLE_EX_FLAG_PIN");
	const size_t apcPos = gameThread.find("QueueUserAPC");
	CHECK(pinPos != std::string::npos);
	CHECK(apcPos != std::string::npos);
	CHECK(pinPos < apcPos);

	const std::string runtime = StripCommentsAndLiterals(ReadFile(dllRoot / "Runtime.cpp"));
	const size_t joinPos = runtime.find("worker_.join");
	const size_t cancelPos = runtime.find("CancelPending");
	CHECK(joinPos != std::string::npos);
	CHECK(cancelPos != std::string::npos);
	CHECK(joinPos < cancelPos);

	// The write helper is exclusive to EngineCalls.
	int writeViolations = 0;
	for (const std::filesystem::path& path : files) {
		if (path.stem().string() == "EngineCalls") continue;
		const std::string text = StripCommentsAndLiterals(ReadFile(path));
		if (ContainsToken(text, "GuardedWrite")) {
			++writeViolations;
			std::printf("    write helper outside EngineCalls: %s\n",
			            path.filename().string().c_str());
		}
	}
	CHECK_EQ(writeViolations, 0);
}

MYTHOS_TEST(BootstrapTeardownIsGuarded) {
	// A throwing bootstrap path must still stop/join the worker and destroy the
	// overlay before the DLL unloads.
	const std::filesystem::path dllRoot =
		std::filesystem::path(MYTHOS_SOURCE_DIR) / "dll" / "src";

	// Comments are stripped first: prose such as "Shutdown() is idempotent"
	// must not satisfy the executable-call check.
	const std::string dllmain = StripCommentsAndLiterals(ReadFile(dllRoot / "dllmain.cpp"));
	CHECK(!dllmain.empty());

	const std::string teardownCall = "Runtime::Instance().Shutdown();";
	const size_t unloadPos = dllmain.find("FreeLibraryAndExitThread");
	CHECK(unloadPos != std::string::npos);

	int teardownCallsBeforeUnload = 0;
	for (size_t pos = dllmain.find(teardownCall); pos != std::string::npos;
	     pos = dllmain.find(teardownCall, pos + teardownCall.size())) {
		if (pos < unloadPos) ++teardownCallsBeforeUnload;
	}
	CHECK_EQ(teardownCallsBeforeUnload, 2); // std::exception and catch-all paths

	const std::string runtime = ReadFile(dllRoot / "Runtime.cpp");
	CHECK(!runtime.empty());
	CHECK(ContainsToken(runtime, "ShutdownGuard"));   // RAII on every Run() exit
	CHECK(ContainsToken(runtime, "worker_.join"));    // explicit join in Shutdown
}

MYTHOS_TEST(LoaderInjectionIsIsolatedToTheLoader) {
	// The loader is the only component allowed to inject, and it must use the
	// minimal, explicitly documented APIs.
	const std::filesystem::path loaderSource =
		std::filesystem::path(MYTHOS_SOURCE_DIR) / "loader" / "src" / "main.cpp";
	const std::string text = ReadFile(loaderSource);
	CHECK(!text.empty());
	CHECK(ContainsToken(text, "LoadLibraryW"));
	CHECK(ContainsToken(text, "PROCESS_CREATE_THREAD"));
	CHECK(!ContainsToken(text, "PROCESS_ALL_ACCESS"));
	CHECK(!ContainsToken(text, "SE_DEBUG_NAME"));
	CHECK(text.find("MYTHOS.dll") != std::string::npos);
	CHECK(text.find("MYTHOS.Loader.exe") != std::string::npos);
	CHECK(text.find("NOVA.dll") != std::string::npos);
	CHECK(text.find("ModuleAlreadyLoaded(pid, kLegacyDllName)") != std::string::npos);
	// No build-gate bypass may exist in the shipping loader: a rename would
	// defeat any filename-based refusal, so the flag itself must be absent.
	CHECK(text.find("allow-unknown-build") == std::string::npos);
	CHECK(!ContainsToken(text, "allowUnknownBuild"));
	CHECK(!ContainsToken(text, "SKIPPED"));
}

MYTHOS_TEST(AnalysisInjectorIsSeparateAndLabeled) {
	// Dumping/RE injection lives in its own analysis-only tool, never in the
	// shipping loader. It performs no build validation by design.
	const std::filesystem::path analysisSource =
		std::filesystem::path(MYTHOS_SOURCE_DIR) / "tools" / "analysis-injector" /
		"src" / "main.cpp";
	const std::string text = ReadFile(analysisSource);
	CHECK(!text.empty());
	CHECK(ContainsToken(text, "LoadLibraryW"));
	CHECK(ContainsToken(text, "CreateRemoteThread"));
	CHECK(text.find("analysis") != std::string::npos);
	CHECK(text.find("MYTHOS.Analysis") != std::string::npos);
	CHECK(!ContainsToken(text, "allowUnknownBuild"));
	CHECK(text.find("allow-unknown-build") == std::string::npos);
}

MYTHOS_TEST(BrandConsistencyHasOnlyExplicitLegacyAllowlist) {
	const std::filesystem::path root = std::filesystem::path(MYTHOS_SOURCE_DIR);
	std::vector<std::filesystem::path> files = CollectSources(root, { "core", "dll", "loader", "tests" });
	for (const char* document : { "README.md", "DESIGN.md", "CMakeLists.txt",
	                              "core/CMakeLists.txt", "dll/CMakeLists.txt",
	                              "loader/CMakeLists.txt", "tests/CMakeLists.txt" }) {
		files.push_back(root / document);
	}
	const std::vector<std::string> allowlisted = {
		"core/include/mythos/Config.hpp",
		"dll/src/Platform.cpp",
		"dll/src/SettingsStore.cpp",
		"dll/src/SettingsStore.hpp",
		"loader/src/main.cpp",
		"README.md",
		"tests/src/test_config.cpp",
		"tests/src/test_contract.cpp",
	};
	int violations = 0;
	for (const std::filesystem::path& path : files) {
		const std::string relative = std::filesystem::relative(path, root).generic_string();
		if (std::find(allowlisted.begin(), allowlisted.end(), relative) != allowlisted.end()) continue;
		const std::string text = ReadFile(path);
		if (text.find("NOVA") != std::string::npos || text.find("Nova") != std::string::npos ||
		    text.find("nova") != std::string::npos) {
			++violations;
			std::printf("    legacy brand token in %s\n", relative.c_str());
		}
	}
	CHECK_EQ(violations, 0);
}

MYTHOS_TEST(OverlayStartupInitializesStyleBeforeShowing) {
	// The overlay must not become visible before its hidden-mode styles and
	// layered attributes are in place, and a style failure must abort startup.
	const std::filesystem::path overlaySource =
		std::filesystem::path(MYTHOS_SOURCE_DIR) / "dll" / "src" / "OverlayWindow.cpp";
	const std::string text = StripCommentsAndLiterals(ReadFile(overlaySource));
	CHECK(!text.empty());

	const size_t createPos = text.find("CreateWindowExW");
	const size_t applyPos = text.find("ApplyExtendedStyle");
	const size_t showPos = text.find("ShowWindow");
	CHECK(createPos != std::string::npos);
	CHECK(applyPos != std::string::npos);
	CHECK(showPos != std::string::npos);
	CHECK(createPos < applyPos);
	CHECK(applyPos < showPos);

	CHECK(!ContainsToken(text, "WS_VISIBLE"));
	CHECK(ContainsToken(text, "if (!ApplyExtendedStyle())"));
	CHECK(ContainsToken(text, "SetLayeredWindowAttributes"));
}
