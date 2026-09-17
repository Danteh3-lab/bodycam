// ============================================================================
// MYTHOS.Loader.exe — explicit, minimal-rights DLL loader.
//
// Usage: MYTHOS.Loader.exe [--dll <path>] [--pid <id>] [--help]
//
// Validation before injection:
//   * DLL exists and is an AMD64 image
//   * target process is x64
//   * known Steam build matches when discoverable (otherwise proceed; the DLL
//     gates ESP on world invariants)
//   * MYTHOS.dll or legacy NOVA.dll is not already loaded
// Injection uses the minimum required access rights and LoadLibraryW. There is
// no debug-privilege escalation, no process-all-access, and no stealth
// behavior.
// ============================================================================
#include "Offsets.hpp"

#include <Windows.h>
#include <TlHelp32.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

// Loader exit codes — distinct and documented.
enum LoaderExitCode : int {
	kSuccess = 0,
	kInvalidArguments = 2,
	kDllNotFound = 3,
	kDllInvalidArchitecture = 4,
	kTargetNotFound = 5,
	kTargetArchitectureMismatch = 6,
	kBuildMismatch = 7,
	kAccessDenied = 8,
	kAllocationFailed = 9,
	kWriteFailed = 10,
	kRemoteThreadFailed = 11,
	kInjectionTimeout = 12,
	kAlreadyLoaded = 13,
	kLoadLibraryFailed = 14,
};

constexpr DWORD kInjectTimeoutMs = 30'000;
const wchar_t* kDllName = L"MYTHOS.dll";
const wchar_t* kLegacyDllName = L"NOVA.dll";

std::wstring ExecutableDirectory() {
	std::vector<wchar_t> buffer(512);
	for (;;) {
		const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
		                                        static_cast<DWORD>(buffer.size()));
		if (length == 0) return {};
		if (length + 1 < buffer.size()) {
			std::wstring path(buffer.data(), length);
			const size_t slash = path.find_last_of(L"\\/");
			if (slash != std::wstring::npos) path.resize(slash + 1);
			return path;
		}
		buffer.resize(buffer.size() * 2);
	}
}

std::wstring MakeAbsolute(const std::wstring& path) {
	std::vector<wchar_t> buffer(512);
	for (;;) {
		const DWORD length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()),
		                                      buffer.data(), nullptr);
		if (length == 0) return {};
		if (length < buffer.size()) return std::wstring(buffer.data(), length);
		buffer.resize(static_cast<size_t>(length) + 1);
	}
}

struct Arguments {
	std::wstring dllPath;
	DWORD pid = 0;
	bool help = false;
};

bool ParseArguments(int argc, wchar_t** argv, Arguments& out) {
	for (int i = 1; i < argc; ++i) {
		const std::wstring_view argument(argv[i]);
		if (argument == L"--help" || argument == L"-h" || argument == L"/?") {
			out.help = true;
			continue;
		}
		if (argument == L"--dll") {
			if (i + 1 >= argc) return false;
			out.dllPath = argv[++i];
			continue;
		}
		if (argument == L"--pid") {
			if (i + 1 >= argc) return false;
			wchar_t* end = nullptr;
			const unsigned long value = wcstoul(argv[++i], &end, 10);
			if (end == nullptr || *end != L'\0' || value == 0 || value > 0xFFFFFFFFul) return false;
			out.pid = static_cast<DWORD>(value);
			continue;
		}
		// Unknown argument.
		return false;
	}
	return true;
}

bool ReadImageHeaders(const std::wstring& path, DWORD* machineOut, Offsets::ImageIdentity* identityOut) {
	HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
	                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	                          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return false;

	IMAGE_DOS_HEADER dos{};
	DWORD read = 0;
	bool ok = ReadFile(file, &dos, sizeof(dos), &read, nullptr) && read == sizeof(dos) &&
	          dos.e_magic == IMAGE_DOS_SIGNATURE;
	if (ok) {
		LARGE_INTEGER offset{};
		offset.QuadPart = dos.e_lfanew;
		ok = SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) != 0;
	}
	IMAGE_NT_HEADERS64 nt{};
	if (ok) {
		ok = ReadFile(file, &nt, sizeof(nt), &read, nullptr) && read == sizeof(nt) &&
		     nt.Signature == IMAGE_NT_SIGNATURE;
	}
	CloseHandle(file);

	if (!ok) return false;
	if (machineOut != nullptr) *machineOut = nt.FileHeader.Machine;
	if (identityOut != nullptr) {
		identityOut->sizeOfImage = nt.OptionalHeader.SizeOfImage;
		identityOut->timeDateStamp = nt.FileHeader.TimeDateStamp;
		identityOut->checkSum = nt.OptionalHeader.CheckSum;
		identityOut->valid = true;
	}
	return true;
}

std::wstring FileVersionString(const std::wstring& path) {
	DWORD dummy = 0;
	const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &dummy);
	if (size == 0) return {};

	std::vector<unsigned char> data(size);
	if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};

	VS_FIXEDFILEINFO* fixed = nullptr;
	UINT fixedLength = 0;
	if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fixed), &fixedLength) ||
	    fixed == nullptr || fixedLength < sizeof(VS_FIXEDFILEINFO)) {
		return {};
	}
	if (fixed->dwSignature != 0xFEEF04BDu) return {};

	wchar_t buffer[64] = {};
	swprintf_s(buffer, L"%u.%u.%u.%u",
	           HIWORD(fixed->dwFileVersionMS), LOWORD(fixed->dwFileVersionMS),
	           HIWORD(fixed->dwFileVersionLS), LOWORD(fixed->dwFileVersionLS));
	return buffer;
}

DWORD FindProcessIdByName(const wchar_t* name) {
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) return 0;

	PROCESSENTRY32W entry = {};
	entry.dwSize = sizeof(entry);
	DWORD found = 0;
	if (Process32FirstW(snapshot, &entry)) {
		do {
			if (_wcsicmp(entry.szExeFile, name) == 0) {
				found = entry.th32ProcessID;
				break;
			}
		} while (Process32NextW(snapshot, &entry));
	}
	CloseHandle(snapshot);
	return found;
}

bool ModuleAlreadyLoaded(DWORD pid, const wchar_t* moduleName) {
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
	if (snapshot == INVALID_HANDLE_VALUE) return false; // cannot tell: proceed

	MODULEENTRY32W entry = {};
	entry.dwSize = sizeof(entry);
	bool loaded = false;
	if (Module32FirstW(snapshot, &entry)) {
		do {
			if (_wcsicmp(entry.szModule, moduleName) == 0) {
				loaded = true;
				break;
			}
		} while (Module32NextW(snapshot, &entry));
	}
	CloseHandle(snapshot);
	return loaded;
}

bool TargetIsX64(HANDLE process) {
	USHORT processMachine = 0;
	USHORT nativeMachine = 0;
	using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
	const auto isWow64Process2 = reinterpret_cast<IsWow64Process2Fn>(
		GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
	if (isWow64Process2 != nullptr) {
		if (!isWow64Process2(process, &processMachine, &nativeMachine)) return false;
		return processMachine == IMAGE_FILE_MACHINE_UNKNOWN;
	}

	BOOL wow64 = FALSE;
	if (!IsWow64Process(process, &wow64)) return false;
	return wow64 == FALSE;
}

std::wstring QueryImagePath(HANDLE process) {
	std::vector<wchar_t> buffer(512);
	for (;;) {
		DWORD length = static_cast<DWORD>(buffer.size());
		if (!QueryFullProcessImageNameW(process, 0, buffer.data(), &length)) return {};
		if (length + 1 < buffer.size()) return std::wstring(buffer.data(), length);
		buffer.resize(buffer.size() * 2);
	}
}

struct InjectionResult {
	bool success = false;
	int exitCode = kSuccess;
	std::wstring message;
};

InjectionResult Inject(HANDLE process, const std::wstring& dllPath) {
	InjectionResult result;

	const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
	void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (remote == nullptr) {
		result.exitCode = kAllocationFailed;
		result.message = L"VirtualAllocEx failed";
		return result;
	}

	SIZE_T written = 0;
	if (!WriteProcessMemory(process, remote, dllPath.c_str(), bytes, &written) || written != bytes) {
		result.exitCode = kWriteFailed;
		result.message = L"WriteProcessMemory failed";
		VirtualFreeEx(process, remote, 0, MEM_RELEASE);
		return result;
	}

	const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
	const auto loadLibraryW = reinterpret_cast<LPTHREAD_START_ROUTINE>(
		GetProcAddress(kernel32, "LoadLibraryW"));
	if (loadLibraryW == nullptr) {
		result.exitCode = kRemoteThreadFailed;
		result.message = L"LoadLibraryW not found";
		VirtualFreeEx(process, remote, 0, MEM_RELEASE);
		return result;
	}

	HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibraryW, remote, 0, nullptr);
	if (thread == nullptr) {
		result.exitCode = kRemoteThreadFailed;
		result.message = L"CreateRemoteThread failed";
		VirtualFreeEx(process, remote, 0, MEM_RELEASE);
		return result;
	}

	const DWORD wait = WaitForSingleObject(thread, kInjectTimeoutMs);
	if (wait != WAIT_OBJECT_0) {
		result.exitCode = kInjectionTimeout;
		result.message = (wait == WAIT_TIMEOUT)
			? L"remote LoadLibraryW timed out; buffer left allocated"
			: L"WaitForSingleObject failed; buffer left allocated";
		CloseHandle(thread);
		return result;
	}

	DWORD remoteResult = 0;
	if (!GetExitCodeThread(thread, &remoteResult)) {
		result.exitCode = kRemoteThreadFailed;
		result.message = L"GetExitCodeThread failed";
		CloseHandle(thread);
		VirtualFreeEx(process, remote, 0, MEM_RELEASE);
		return result;
	}
	CloseHandle(thread);
	VirtualFreeEx(process, remote, 0, MEM_RELEASE);

	if (remoteResult == 0) {
		result.exitCode = kLoadLibraryFailed;
		result.message = L"LoadLibraryW returned NULL in the target";
		return result;
	}

	result.success = true;
	result.exitCode = kSuccess;
	result.message = L"injection succeeded";
	return result;
}

void PrintUsage() {
	wprintf(L"MYTHOS loader\n");
	wprintf(L"  usage: MYTHOS.Loader.exe [--dll <path>] [--pid <id>]\n");
	wprintf(L"  default DLL: %ls (next to this loader)\n", kDllName);
	wprintf(L"  default target: %hs\n", Offsets::kTargetProcess);
}

} // namespace

int RunLoader(int argc, wchar_t** argv) {
	SetConsoleOutputCP(CP_UTF8);

	Arguments arguments;
	if (!ParseArguments(argc, argv, arguments) || arguments.help) {
		PrintUsage();
		return arguments.help ? kSuccess : kInvalidArguments;
	}

	std::wstring dllPath = arguments.dllPath;
	if (dllPath.empty()) {
		const std::wstring directory = ExecutableDirectory();
		if (directory.empty()) {
			wprintf(L"[!] Could not resolve the loader directory.\n");
			return kInvalidArguments;
		}
		dllPath = directory + kDllName;
	} else if (dllPath.find_first_of(L"\\/") == std::wstring::npos) {
		dllPath = ExecutableDirectory() + dllPath;
	}
	dllPath = MakeAbsolute(dllPath);

	wprintf(L"MYTHOS loader\n  dll    : %ls\n", dllPath.c_str());

	const DWORD attributes = GetFileAttributesW(dllPath.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
		wprintf(L"[!] DLL not found: %ls\n", dllPath.c_str());
		return kDllNotFound;
	}

	DWORD dllMachine = 0;
	if (!ReadImageHeaders(dllPath, &dllMachine, nullptr) || dllMachine != IMAGE_FILE_MACHINE_AMD64) {
		wprintf(L"[!] DLL is not a valid AMD64 image.\n");
		return kDllInvalidArchitecture;
	}

	const Offsets::Profile& profile = Offsets::kActiveProfile;

	DWORD pid = arguments.pid;
	if (pid == 0) {
		pid = FindProcessIdByName(L"Bodycam-Win64-Shipping.exe");
		if (pid == 0) {
			wprintf(L"[!] Process not found. Start the game, then run this loader.\n");
			return kTargetNotFound;
		}
	}
	wprintf(L"  target : PID %lu\n", pid);

	// Already-loaded check first (uses a toolhelp snapshot, no strong rights).
	if (ModuleAlreadyLoaded(pid, kDllName) || ModuleAlreadyLoaded(pid, kLegacyDllName)) {
		wprintf(L"[i] MYTHOS.dll or legacy NOVA.dll is already loaded in that process; nothing to do. "
		        L"Restart the game to load MYTHOS again.\n");
		return kAlreadyLoaded;
	}

	const DWORD access = PROCESS_CREATE_THREAD | PROCESS_QUERY_LIMITED_INFORMATION |
	                     PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
	HANDLE process = OpenProcess(access, FALSE, pid);
	if (process == nullptr) {
		const DWORD error = GetLastError();
		wprintf(L"[!] OpenProcess failed (%lu). Run the loader at the same integrity level "
		        L"as the game.\n", error);
		return (error == ERROR_ACCESS_DENIED) ? kAccessDenied : kTargetNotFound;
	}

	if (!TargetIsX64(process)) {
		wprintf(L"[!] Target process is not x64; MYTHOS requires the x64 build.\n");
		CloseHandle(process);
		return kTargetArchitectureMismatch;
	}

	const std::wstring imagePath = QueryImagePath(process);
	const char* pinnedVersion = profile.knownFileVersion;

	// Pinned PE identity check: the profile records the exact build the offsets
	// were verified against. A mismatch fails closed before injection.
	if (Offsets::HasPinnedImageIdentity(profile)) {
		Offsets::ImageIdentity identity;
		DWORD targetMachine = 0;
		if (imagePath.empty() || !ReadImageHeaders(imagePath, &targetMachine, &identity)) {
			wprintf(L"[!] Build identity could not be read and the profile is pinned; "
			        L"failing closed.\n");
			CloseHandle(process);
			return kBuildMismatch;
		}
		wprintf(L"  build  : SizeOfImage 0x%08X  TimeDateStamp 0x%08X  CheckSum 0x%08X\n",
		        identity.sizeOfImage, identity.timeDateStamp, identity.checkSum);
		if (Offsets::ImageIdentityConflictsWithProfile(identity, profile)) {
			wprintf(L"[!] Build mismatch: installed image is not Steam build %llu.\n",
			        static_cast<unsigned long long>(profile.steamBuild));
			CloseHandle(process);
			return kBuildMismatch;
		}
	}

	// Legacy file-version pin, honored when a build ships a version resource.
	if (pinnedVersion != nullptr && *pinnedVersion != '\0' && !imagePath.empty()) {
		const std::wstring fileVersion = FileVersionString(imagePath);
		if (!fileVersion.empty()) {
			const std::wstring pinned = [pinnedVersion]() {
				std::wstring wide;
				for (const char* c = pinnedVersion; *c != '\0'; ++c) {
					wide.push_back(static_cast<wchar_t>(*c));
				}
				return wide;
			}();
			if (fileVersion != pinned) {
				wprintf(L"[!] Build mismatch: profile expects %ls, target is %ls.\n",
				        pinned.c_str(), fileVersion.c_str());
				CloseHandle(process);
				return kBuildMismatch;
			}
		}
	}

	const InjectionResult result = Inject(process, dllPath);
	CloseHandle(process);

	if (!result.success) {
		wprintf(L"[-] %ls\n", result.message.c_str());
		return result.exitCode;
	}

	wprintf(L"[+] %ls\n", result.message.c_str());
	wprintf(L"    INSERT toggles the MYTHOS menu, DELETE stops MYTHOS "
	        L"(restart the game to load it again).\n");
	return kSuccess;
}

namespace {

// True when this console belongs to the loader alone, i.e. it was started by
// double-click rather than from an existing shell.
bool LaunchedStandalone() {
	DWORD processes[4] = {};
	const DWORD count = GetConsoleProcessList(processes, 4);
	return count == 1;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
	const int exitCode = RunLoader(argc, argv);

	// Keep the window open when double-clicked so the result is readable.
	if (argc <= 1 && LaunchedStandalone()) {
		wprintf(L"\nPress Enter to close...");
		(void)getchar();
	}
	return exitCode;
}
