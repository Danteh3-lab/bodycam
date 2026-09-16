#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <cstdio>
#include <cstring>

static const char* kTargetProcess = "Bodycam-Win64-Shipping.exe";
static const char* kDefaultDllName = "NOVA.dll";

static std::string GetExeDir() {
	char buf[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, buf, MAX_PATH);
	std::string p = buf;
	const size_t slash = p.find_last_of("\\/");
	if (slash != std::string::npos) p.resize(slash + 1);
	return p;
}

static std::string GetDllPath(int argc, char** argv) {
	if (argc > 1 && argv[1][0]) {
		std::string p = argv[1];
		if (p.find_first_of("\\/") == std::string::npos) p = GetExeDir() + p;
		return p;
	}
	return GetExeDir() + kDefaultDllName;
}

static DWORD FindProcessId(const char* name) {
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) return 0;
	PROCESSENTRY32 pe = { sizeof(pe) };
	DWORD pid = 0;
	if (Process32First(snap, &pe)) {
		do {
			if (_stricmp(pe.szExeFile, name) == 0) { pid = pe.th32ProcessID; break; }
		} while (Process32Next(snap, &pe));
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

static bool Inject(HANDLE hProc, const std::string& dllPath) {
	const SIZE_T len = dllPath.size() + 1;
	void* remote = VirtualAllocEx(hProc, nullptr, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!remote) {
		printf("[!] VirtualAllocEx failed: %lu\n", GetLastError());
		return false;
	}
	if (!WriteProcessMemory(hProc, remote, dllPath.c_str(), len, nullptr)) {
		printf("[!] WriteProcessMemory failed: %lu\n", GetLastError());
		VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
		return false;
	}

	HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
	if (!kernel32) {
		printf("[!] kernel32.dll not found in loader\n");
		VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
		return false;
	}
	LPTHREAD_START_ROUTINE loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
		GetProcAddress(kernel32, "LoadLibraryA"));
	if (!loadLib) {
		printf("[!] LoadLibraryA not found: %lu\n", GetLastError());
		VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
		return false;
	}

	HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, loadLib, remote, 0, nullptr);
	if (!hThread) {
		printf("[!] CreateRemoteThread failed: %lu\n", GetLastError());
		VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
		return false;
	}

	WaitForSingleObject(hThread, INFINITE);

	DWORD exitCode = 0;
	GetExitCodeThread(hThread, &exitCode);
	CloseHandle(hThread);
	VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);

	printf("[+] LoadLibraryA returned handle 0x%08lX (nonzero = loaded)\n", exitCode);
	return exitCode != 0;
}

int main(int argc, char** argv) {
	const std::string dllPath = GetDllPath(argc, argv);

	printf("NOVA loader\n");
	printf("  target : %s\n", kTargetProcess);
	printf("  dll    : %s\n", dllPath.c_str());

	{
		FILE* f = nullptr;
		if (fopen_s(&f, dllPath.c_str(), "rb") != 0 || !f) {
			printf("[!] DLL not found. Pass the path as the first argument, or put %s\n"
			       "    next to this loader.\n", kDefaultDllName);
			return 1;
		}
		fclose(f);
	}

	EnablePrivilege(SE_DEBUG_NAME);

	const DWORD pid = FindProcessId(kTargetProcess);
	if (!pid) {
		printf("[!] Process \"%s\" not found.\n", kTargetProcess);
		printf("    Start the game first, then run this loader as administrator.\n");
		return 1;
	}
	printf("[+] Found PID %lu\n", pid);

	HANDLE hProc = OpenTarget(pid);
	if (!hProc || hProc == INVALID_HANDLE_VALUE) {
		printf("[!] OpenProcess failed: %lu (run as administrator)\n", GetLastError());
		return 1;
	}
	printf("[+] Process handle opened\n");

	const bool ok = Inject(hProc, dllPath);
	CloseHandle(hProc);

	printf(ok ? "[+] Injection succeeded. Toggle the menu with INSERT, unload with DELETE.\n"
	          : "[-] Injection failed.\n");
	return ok ? 0 : 2;
}