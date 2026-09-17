// ============================================================================
// ProcessMemory — Win32 ReadOnlyMemory implementation.
//
// Guarded reads use SEH so a stale pointer fails closed instead of crashing
// the game. There is no write path of any kind.
// ============================================================================
#pragma once
#include "mythos/ReadOnlyMemory.hpp"

#include <Windows.h>

namespace mythos_host {

class ProcessMemory final : public mythos::ReadOnlyMemory {
public:
	ProcessMemory() = default;

	bool Attach(const wchar_t* moduleName);

	[[nodiscard]] mythos::ModuleInfo module() const override;
	[[nodiscard]] bool read(uintptr_t address, void* out, size_t size) const override;
	int sections(bool executable, mythos::SectionRange* out, int maxOut) const override;

	[[nodiscard]] bool attached() const { return moduleHandle_ != nullptr; }

private:
	HMODULE moduleHandle_ = nullptr;
	mythos::ModuleInfo info_;
};

} // namespace mythos_host
