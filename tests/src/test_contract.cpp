// ============================================================================
// Static read-only contract test.
//
// Scans the core and DLL sources (comments and string literals stripped) and
// rejects memory-write helpers, patching APIs, engine-call references and
// aim/input symbols. The loader's isolated DLL-path injection is intentionally
// outside this scan and is asserted separately.
// ============================================================================
#include "test_framework.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
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

std::vector<std::filesystem::path> CollectSources(const std::filesystem::path& root) {
	std::vector<std::filesystem::path> files;
	for (const char* directory : { "core", "dll" }) {
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

const char* const kForbiddenTokens[] = {
	// Memory mutation / patching / injection.
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
	// Engine call / aim and input symbols (Offsets::Reference is reference-only).
	"Offsets::Reference",
	"Reference::Calls",
	"ProcessEvent",
	"AddPitchInput",
	"AddYawInput",
	"AddRollInput",
	"RotationInput",
	"RotationInputPitch",
	"RotationInputYaw",
	"RotationInputRoll",
	"ControlRotation",
	"RemoteViewPitch",
	"PCTargetViewRotation",
	"InputYawScale",
	"InputPitchScale",
	"InputRollScale",
	"AimDefault",
};

} // namespace

NOVA_TEST(ContractScannerSelfTest) {
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

NOVA_TEST(CoreAndDllAreStrictlyReadOnly) {
	const std::filesystem::path root = NOVA_SOURCE_DIR;
	const std::vector<std::filesystem::path> files = CollectSources(root);
	CHECK(files.size() >= 20); // guard against a vacuous scan

	int violations = 0;
	for (const std::filesystem::path& path : files) {
		const std::string text = StripCommentsAndLiterals(ReadFile(path));
		for (const char* token : kForbiddenTokens) {
			if (!ContainsToken(text, token)) continue;
			++violations;
			std::printf("    forbidden token '%s' in %s\n", token, path.filename().string().c_str());
		}
	}
	CHECK_EQ(violations, 0);
}

NOVA_TEST(BootstrapTeardownIsGuarded) {
	// A throwing bootstrap path must still stop/join the worker and destroy the
	// overlay before the DLL unloads.
	const std::filesystem::path dllRoot =
		std::filesystem::path(NOVA_SOURCE_DIR) / "dll" / "src";

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

NOVA_TEST(LoaderInjectionIsIsolatedToTheLoader) {
	// The loader is the only component allowed to inject, and it must use the
	// minimal, explicitly documented APIs.
	const std::filesystem::path loaderSource =
		std::filesystem::path(NOVA_SOURCE_DIR) / "loader" / "src" / "main.cpp";
	const std::string text = ReadFile(loaderSource);
	CHECK(!text.empty());
	CHECK(ContainsToken(text, "LoadLibraryW"));
	CHECK(ContainsToken(text, "PROCESS_CREATE_THREAD"));
	CHECK(!ContainsToken(text, "PROCESS_ALL_ACCESS"));
	CHECK(!ContainsToken(text, "SE_DEBUG_NAME"));
}

NOVA_TEST(OverlayStartupInitializesStyleBeforeShowing) {
	// The overlay must not become visible before its hidden-mode styles and
	// layered attributes are in place, and a style failure must abort startup.
	const std::filesystem::path overlaySource =
		std::filesystem::path(NOVA_SOURCE_DIR) / "dll" / "src" / "OverlayWindow.cpp";
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
