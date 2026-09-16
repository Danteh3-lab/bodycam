// ============================================================================
// ProcessMemory — Win32 ReadOnlyMemory implementation.
//
// Guarded reads use SEH so a stale pointer fails closed instead of crashing
// the game. There is no write path of any kind.
// ============================================================================
#pragma once
#include "nova/ReadOnlyMemory.hpp"

#include <Windows.h>

namespace nova_host {

class ProcessMemory final : public nova::ReadOnlyMemory {
public:
	ProcessMemory() = default;

	bool Attach(const wchar_t* moduleName);

	[[nodiscard]] nova::ModuleInfo module() const override;
	[[nodiscard]] bool read(uintptr_t address, void* out, size_t size) const override;
	int sections(bool executable, nova::SectionRange* out, int maxOut) const override;

	[[nodiscard]] bool attached() const { return moduleHandle_ != nullptr; }

private:
	HMODULE moduleHandle_ = nullptr;
	nova::ModuleInfo info_;
};

} // namespace nova_host
