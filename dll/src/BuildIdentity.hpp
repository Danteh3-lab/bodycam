// ============================================================================
// BuildIdentity — in-process build fingerprint used to fail closed on a
// known build mismatch. Records module path/size and the PE FileVersion when
// the game ships one (Steam builds usually do not).
// ============================================================================
#pragma once
#include "Offsets.hpp"

#include <cstdint>
#include <string>

namespace nova_host {

struct BuildIdentity {
	std::wstring modulePath;
	std::string  fileVersion;       // empty when the image has no version resource
	std::string  fingerprint;       // one-line identity for logs and diagnostics
	uint64_t     moduleSize = 0;
	Offsets::ImageIdentity image;   // in-process PE identity

	// True when the image contradicts the pinned profile (PE identity or a
	// pinned FileVersion). Always false for an unpinned profile: unknown
	// builds may inject and are gated by the world invariants instead.
	[[nodiscard]] bool knownMismatch() const;
};

[[nodiscard]] BuildIdentity QueryBuildIdentity();

[[nodiscard]] std::string BuildFingerprintString(const BuildIdentity& identity);

} // namespace nova_host
