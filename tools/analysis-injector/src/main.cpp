// ============================================================================
// MYTHOS.Analysis — analysis-only DLL injector for dumping/RE work.
//
// This tool performs NO build validation of any kind: it injects whatever DLL
// it is given into the target process. It exists so the MYTHOS loader itself
// never needs a build-gate bypass and always fails closed before injection.
//
// Never point this tool at MYTHOS.dll on an unverified build. Use it for
// third-party analysis DLLs (for example an SDK dumper) while the game is
// running offline.
// ============================================================================
#include "Offsets.hpp"

#include <Windows.h>

#include <TlHelp32.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

enum ExitCode : int {
	kSuccess = 0,
	kInvalidArguments = 2,
	kDllNotFound = 3,
	kDllInvalidArchitecture = 4,
	kTargetNotFound = 5,
	kTargetArchitectureMismatch = 6,
	kAccessDenied = 8,
	kAllocationFailed = 9,
	kWriteFailed = 10,
	kRemoteThreadFailed = 11,
	kInjectionTimeout = 12,
	kLoadLibraryFailed = 14,
};

constexpr DWORD kInjectTimeoutMs = 10000;

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
		return false;
	}
	// --dll is required: never default to the project's own DLL here.
	return !out.dllPath.empty();
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

void PrintUsage() {
	wprintf(L"MYTHOS analysis injector (NO build checks; dumping/RE work only)\n");
	wprintf(L"  usage: MYTHOS.Analysis.exe --dll <path> [--pid <id>]\n");
	wprintf(L"  default target: %hs\n", Offsets::kTargetProcess);
	wprintf(L"  Never point this tool at MYTHOS.dll on an unverified build.\n");
}

int RunAnalyzer(int argc, wchar_t** argv) {
	SetConsoleOutputCP(CP_UTF8);

	Arguments arguments;
	if (!ParseArguments(argc, argv, arguments) || arguments.help) {
		PrintUsage();
		return arguments.help ? kSuccess : kInvalidArguments;
	}

	const std::wstring dllPath = MakeAbsolute(arguments.dllPath);

	wprintf(L"MYTHOS analysis injector (no build checks)\n  dll    : %ls\n", dllPath.c_str());

	const DWORD attributes = GetFileAttributesW(dllPath.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
		wprintf(L"[!] DLL not found: %ls\n", dllPath.c_str());
		return kDllNotFound;
	}

	DWORD dllMachine = 0;
	{
		HANDLE file = CreateFileW(dllPath.c_str(), GENERIC_READ,
		                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		                          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			wprintf(L"[!] DLL could not be opened.\n");
			return kDllNotFound;
		}
		IMAGE_DOS_HEADER dos{};
		DWORD read = 0;
		bool ok = ReadFile(file, &dos, sizeof(dos), &read, nullptr) && read == sizeof(dos) &&
		          dos.e_magic == IMAGE_DOS_SIGNATURE;
		IMAGE_NT_HEADERS64 nt{};
		if (ok) {
			LARGE_INTEGER offset{};
			offset.QuadPart = dos.e_lfanew;
			ok = SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) != 0 &&
			     ReadFile(file, &nt, sizeof(nt), &read, nullptr) && read == sizeof(nt) &&
			     nt.Signature == IMAGE_NT_SIGNATURE;
		}
		CloseHandle(file);
		if (!ok) {
			wprintf(L"[!] DLL headers could not be read.\n");
			return kDllInvalidArchitecture;
		}
		dllMachine = nt.FileHeader.Machine;
	}
	if (dllMachine != IMAGE_FILE_MACHINE_AMD64) {
		wprintf(L"[!] DLL is not a valid AMD64 image.\n");
		return kDllInvalidArchitecture;
	}

	const std::wstring targetName = [] {
		std::wstring wide;
		for (const char* c = Offsets::kTargetProcess; *c != '\0'; ++c) {
			wide.push_back(static_cast<wchar_t>(*c));
		}
		return wide;
	}();

	DWORD pid = arguments.pid;
	if (pid == 0) {
		pid = FindProcessIdByName(targetName.c_str());
		if (pid == 0) {
			wprintf(L"[!] Process not found. Start the game, then run this tool.\n");
			return kTargetNotFound;
		}
	}
	wprintf(L"  target : PID %lu\n", pid);

	const DWORD access = PROCESS_CREATE_THREAD | PROCESS_QUERY_LIMITED_INFORMATION |
	                     PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
	HANDLE process = OpenProcess(access, FALSE, pid);
	if (process == nullptr) {
		const DWORD error = GetLastError();
		wprintf(L"[!] OpenProcess failed (%lu). Run this tool at the same integrity level "
		        L"as the game.\n", error);
		return (error == ERROR_ACCESS_DENIED) ? kAccessDenied : kTargetNotFound;
	}

	if (!TargetIsX64(process)) {
		wprintf(L"[!] Target process is not x64.\n");
		CloseHandle(process);
		return kTargetArchitectureMismatch;
	}

	const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
	void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (remote == nullptr) {
		wprintf(L"[!] VirtualAllocEx failed.\n");
		CloseHandle(process);
		return kAllocationFailed;
	}

	SIZE_T written = 0;
	if (!WriteProcessMemory(process, remote, dllPath.c_str(), bytes, &written) || written != bytes) {
		wprintf(L"[!] WriteProcessMemory failed.\n");
		VirtualFreeEx(process, remote, 0, MEM_RELEASE);
		CloseHandle(process);
		return kWriteFailed;
	}

	const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
	const auto loadLibraryW = reinterpret_cast<LPTHREAD_START_ROUTINE>(
		GetProcAddress(kernel32, "LoadLibraryW"));
	if (loadLibraryW == nullptr) {
		wprintf(L"[!] LoadLibraryW not found.\n");
		VirtualFreeEx(process, remote, 0, MEM_RELEASE);
		CloseHandle(process);
		return kRemoteThreadFailed;
	}

	HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibraryW, remote, 0, nullptr);
	if (thread == nullptr) {
		wprintf(L"[!] CreateRemoteThread failed.\n");
		VirtualFreeEx(process, remote, 0, MEM_RELEASE);
		CloseHandle(process);
		return kRemoteThreadFailed;
	}

	const DWORD wait = WaitForSingleObject(thread, kInjectTimeoutMs);
	if (wait != WAIT_OBJECT_0) {
		wprintf(L"[!] Remote LoadLibraryW timed out.\n");
		CloseHandle(thread);
		CloseHandle(process);
		return kInjectionTimeout;
	}

	DWORD remoteResult = 0;
	const BOOL gotResult = GetExitCodeThread(thread, &remoteResult);
	CloseHandle(thread);
	VirtualFreeEx(process, remote, 0, MEM_RELEASE);
	CloseHandle(process);

	if (!gotResult || remoteResult == 0) {
		wprintf(L"[!] LoadLibraryW returned NULL in the target.\n");
		return kLoadLibraryFailed;
	}

	wprintf(L"[+] injection succeeded\n");
	return kSuccess;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
	const int exitCode = RunAnalyzer(argc, argv);
	if (exitCode != 0) {
		HANDLE console = GetConsoleWindow();
		if (console != nullptr) {
			DWORD processes[4] = {};
			if (GetConsoleProcessList(processes, 4) == 1) {
				wprintf(L"Press Enter to close...\n");
				getwchar();
			}
		}
	}
	return exitCode;
}
