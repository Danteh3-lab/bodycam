#include <windows.h>
#include <tlhelp32.h>
#include <cwchar>
#include <string>
#include <vector>
#include <cstdio>

static const wchar_t* kTargetProcess = L"Bodycam-Win64-Shipping.exe";
static const wchar_t* kDefaultDllName = L"NOVA.dll";
static constexpr DWORD kInjectTimeoutMs = 30'000;

static std::wstring GetExeDir() {
	std::vector<wchar_t> buffer(512);
	for (;;) {
		const DWORD length = GetModuleFileNameW(
			nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0) return {};
		if (static_cast<size_t>(length) + 1 < buffer.size()) {
			std::wstring path(buffer.data(), length);
			const size_t slash = path.find_last_of(L"\\/");
			if (slash != std::wstring::npos) path.resize(slash + 1);
			return path;
		}
		buffer.resize(buffer.size() * 2);
	}
}

static std::wstring MakeAbsolutePath(const std::wstring& path) {
	if (path.empty()) return {};

	std::vector<wchar_t> buffer(512);
	for (;;) {
		const DWORD length = GetFullPathNameW(
			path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
		if (length == 0) return {};
		if (static_cast<size_t>(length) < buffer.size()) return std::wstring(buffer.data(), length);
		buffer.resize(static_cast<size_t>(length) + 1);
	}
}

static std::wstring GetDllPath(int argc, wchar_t** argv) {
	std::wstring path;
	if (argc > 1 && argv[1][0]) {
		path = argv[1];
		if (path.find_first_of(L"\\/") == std::wstring::npos) {
			const std::wstring exeDir = GetExeDir();
			if (exeDir.empty()) return {};
			path = exeDir + path;
		}
	} else {
		const std::wstring exeDir = GetExeDir();
		if (exeDir.empty()) return {};
		path = exeDir + kDefaultDllName;
	}
	return MakeAbsolutePath(path);
}

static DWORD FindProcessId(const wchar_t* name) {
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) return 0;
	PROCESSENTRY32W pe = {};
	pe.dwSize = static_cast<DWORD>(sizeof(pe));
	DWORD pid = 0;
	if (Process32FirstW(snap, &pe)) {
		do {
			if (_wcsicmp(pe.szExeFile, name) == 0) { pid = pe.th32ProcessID; break; }
		} while (Process32NextW(snap, &pe));
	}
	CloseHandle(snap);
	return pid;
}

static bool EnablePrivilege(const char* name) {
	HANDLE hToken = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
		return false;
	TOKEN_PRIVILEGES tp = {};
	tp.PrivilegeCount = 1;
	if (!LookupPrivilegeValueA(nullptr, name, &tp.Privileges[0].Luid)) {
		CloseHandle(hToken);
		return false;
	}
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	const bool ok = AdjustTokenPrivileges(hToken, FALSE, &tp, 0, nullptr, nullptr)
	                && GetLastError() == ERROR_SUCCESS;
	CloseHandle(hToken);
	return ok;
}

static HANDLE OpenTarget(DWORD pid) {
	HANDLE h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
	if (h) return h;
	return OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE
	                    | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
}

static bool Inject(HANDLE hProc, const std::wstring& dllPath) {
	const SIZE_T bytes = static_cast<SIZE_T>((dllPath.size() + 1) * sizeof(wchar_t));
	void* remote = VirtualAllocEx(hProc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!remote) {
		wprintf(L"[!] VirtualAllocEx failed: %lu\n", GetLastError());
		return false;
	}
	SIZE_T bytesWritten = 0;
	if (!WriteProcessMemory(hProc, remote, dllPath.c_str(), bytes, &bytesWritten)
		|| bytesWritten != bytes) {
		wprintf(L"[!] WriteProcessMemory failed: %lu\n", GetLastError());
		VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
		return false;
	}

	HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
	if (!kernel32) {
		wprintf(L"[!] kernel32.dll not found in loader\n");
		VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
		return false;
	}
	LPTHREAD_START_ROUTINE loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
		GetProcAddress(kernel32, "LoadLibraryW"));
	if (!loadLib) {
		wprintf(L"[!] LoadLibraryW not found: %lu\n", GetLastError());
		VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
		return false;
	}

	HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, loadLib, remote, 0, nullptr);
	if (!hThread) {
		wprintf(L"[!] CreateRemoteThread failed: %lu\n", GetLastError());
		VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
		return false;
	}

	const DWORD waitResult = WaitForSingleObject(hThread, kInjectTimeoutMs);
	if (waitResult != WAIT_OBJECT_0) {
		if (waitResult == WAIT_TIMEOUT) {
			wprintf(L"[!] Remote LoadLibraryW timed out after %lu ms; "
			        L"leaving the remote buffer allocated because the thread may still be running.\n",
			        kInjectTimeoutMs);
		} else {
			wprintf(L"[!] WaitForSingleObject failed: %lu; leaving the remote buffer allocated.\n",
			        GetLastError());
		}
		CloseHandle(hThread);
		return false;
	}

	DWORD exitCode = 0;
	if (!GetExitCodeThread(hThread, &exitCode)) {
		wprintf(L"[!] GetExitCodeThread failed: %lu\n", GetLastError());
		CloseHandle(hThread);
		VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
		return false;
	}
	CloseHandle(hThread);
	VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);

	wprintf(L"[+] LoadLibraryW returned 0x%08lX (nonzero = loaded)\n", exitCode);
	return exitCode != 0;
}

int wmain(int argc, wchar_t** argv) {
	const std::wstring dllPath = GetDllPath(argc, argv);

	wprintf(L"NOVA loader\n");
	wprintf(L"  target : %ls\n", kTargetProcess);
	wprintf(L"  dll    : %ls\n", dllPath.c_str());

	const DWORD attributes = GetFileAttributesW(dllPath.c_str());
	if (dllPath.empty() || attributes == INVALID_FILE_ATTRIBUTES
		|| (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
		wprintf(L"[!] DLL not found. Pass the path as the first argument, or put %ls\n"
		        L"    next to this loader.\n", kDefaultDllName);
		return 1;
	}

	EnablePrivilege(SE_DEBUG_NAME);

	const DWORD pid = FindProcessId(kTargetProcess);
	if (!pid) {
		wprintf(L"[!] Process \"%ls\" not found.\n", kTargetProcess);
		wprintf(L"    Start the game first, then run this loader as administrator.\n");
		return 1;
	}
	wprintf(L"[+] Found PID %lu\n", pid);

	HANDLE hProc = OpenTarget(pid);
	if (!hProc || hProc == INVALID_HANDLE_VALUE) {
		wprintf(L"[!] OpenProcess failed: %lu (run as administrator)\n", GetLastError());
		return 1;
	}
	wprintf(L"[+] Process handle opened\n");

	const bool ok = Inject(hProc, dllPath);
	CloseHandle(hProc);

	wprintf(ok ? L"[+] Injection succeeded. Toggle the menu with INSERT, unload with DELETE.\n"
	          : L"[-] Injection failed.\n");
	return ok ? 0 : 2;
}
