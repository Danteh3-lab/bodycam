#include "EngineCalls.hpp"

#include "Offsets.hpp"

#include <Windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

namespace nova_host {
namespace {

__declspec(noinline) bool SafeInvoke(void(__fastcall* function)(void*, float),
                                     void* playerController, float value) {
	__try {
		function(playerController, value);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

__declspec(noinline) bool GuardedWriteDouble(uintptr_t address, double value) {
	__try {
		std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool ReadDouble(const nova::ReadOnlyMemory& memory, uintptr_t address, double& out) {
	return memory.readRaw<double>(address, out) && std::isfinite(out);
}

} // namespace

EngineCalls::EngineCalls(const nova::ReadOnlyMemory& memory, GameThreadExecutor& gameThread)
	: memory_(memory), gameThread_(gameThread), scanner_(memory, true) {}

void EngineCalls::RefreshStatus() {
	status_.functionsResolved = ready();
	status_.gameThreadAvailable = gameThread_.verified();
	status_.engineCallsEnabled = engineCallsEnabled_;
	status_.engineMethodAvailable = enginePathVerified();
	// The game-thread fact is reported on its own; the opt-in state is
	// reported separately so an unavailable thread path is never mislabeled.
	status_.enginePath = gameThread_.message();
}

void EngineCalls::Resolve(std::size_t scanBudgetBytes) {
	if (!engineCallsEnabled_) {
		status_.message = "engine calls disabled; resolution skipped";
		return;
	}
	if (ready()) return;

	if (!status_.attempted) {
		status_.attempted = true;
		TryKnownRvas();
		if (ready()) return;
		scanner_.Refresh();
		scanStarted_ = true;
	}

	if (!scanStarted_ || scanCursor_.finished) return;

	const uint64_t start = GetTickCount64();
	const nova::ModuleScanner::StepResult result =
		scanner_.Step(scanCursor_, scanBudgetBytes, &EngineCalls::ScanChunk, this);
	status_.scanMs += static_cast<uint32_t>(GetTickCount64() - start);

	if (pitchAddress_ != 0 && yawAddress_ != 0) {
		Adopt(pitchAddress_, yawAddress_, true);
		return;
	}
	status_.message = result == nova::ModuleScanner::StepResult::Exhausted
		? "AddPitch/AddYawInput not verified; using direct rotation write"
		: "scanning for AddPitch/AddYawInput...";
}

bool EngineCalls::VerifyAddInput(uintptr_t function, uint32_t wantOffset) const {
	if (!nova::IsPlausiblePointer(function)) return false;

	uint8_t bytes[Offsets::Signatures::AddInputFnSize] = {};
	if (!memory_.read(function, bytes, sizeof(bytes))) return false;
	if (std::memcmp(bytes, Offsets::Signatures::AddInputPrologue,
	                sizeof(Offsets::Signatures::AddInputPrologue)) != 0) {
		return false;
	}

	const uint8_t* tail = bytes + Offsets::Signatures::AddInputTailOffset;
	if (!(tail[0] == 0x0F && tail[1] == 0x5A && tail[2] == 0xC0)) return false;
	if (!(tail[3] == 0xF2 && tail[4] == 0x0F && tail[5] == 0x58 && tail[6] == 0x83)) return false;

	uint32_t offset1 = 0;
	std::memcpy(&offset1, tail + Offsets::Signatures::AddInputTailArg1, sizeof(offset1));
	if (offset1 != wantOffset) return false;

	if (!(tail[11] == 0xF2 && tail[12] == 0x0F && tail[13] == 0x11 && tail[14] == 0x83)) return false;

	uint32_t offset2 = 0;
	std::memcpy(&offset2, tail + Offsets::Signatures::AddInputTailArg2, sizeof(offset2));
	if (offset2 != wantOffset) return false;

	if (!(tail[19] == 0x48 && tail[20] == 0x83 && tail[21] == 0xC4 && tail[22] == 0x30 &&
	      tail[23] == 0x5B && tail[24] == 0xC3)) {
		return false;
	}
	return true;
}

bool EngineCalls::ScanChunk(void* context, uintptr_t address, const uint8_t* data, size_t size) {
	auto* self = static_cast<EngineCalls*>(context);
	const std::size_t tailLength = Offsets::Signatures::AddInputTailLength;
	if (size < tailLength) return false;

	const uint32_t wantPitch = static_cast<uint32_t>(Offsets::Aim::RotationInputPitch);
	const uint32_t wantYaw = static_cast<uint32_t>(Offsets::Aim::RotationInputYaw);
	const std::size_t last = size - tailLength;

	for (std::size_t i = 0; i <= last; ++i) {
		if (data[i] != 0x0F || data[i + 1] != 0x5A || data[i + 2] != 0xC0) continue;
		if (data[i + 3] != 0xF2 || data[i + 4] != 0x0F || data[i + 5] != 0x58 ||
		    data[i + 6] != 0x83) {
			continue;
		}
		if (data[i + 11] != 0xF2 || data[i + 12] != 0x0F || data[i + 13] != 0x11 ||
		    data[i + 14] != 0x83) {
			continue;
		}
		if (data[i + 19] != 0x48 || data[i + 20] != 0x83 || data[i + 21] != 0xC4 ||
		    data[i + 22] != 0x30 || data[i + 23] != 0x5B || data[i + 24] != 0xC3) {
			continue;
		}

		uint32_t offset1 = 0;
		uint32_t offset2 = 0;
		std::memcpy(&offset1, data + i + Offsets::Signatures::AddInputTailArg1, sizeof(offset1));
		std::memcpy(&offset2, data + i + Offsets::Signatures::AddInputTailArg2, sizeof(offset2));
		if (offset1 != offset2) continue;

		const uintptr_t tail = address + i;
		if (tail < Offsets::Signatures::AddInputTailOffset) continue;
		const uintptr_t function = tail - Offsets::Signatures::AddInputTailOffset;

		if (offset1 == wantPitch && self->pitchAddress_ == 0 &&
		    self->VerifyAddInput(function, offset1)) {
			self->pitchAddress_ = function;
		} else if (offset1 == wantYaw && self->yawAddress_ == 0 &&
		           self->VerifyAddInput(function, offset1)) {
			self->yawAddress_ = function;
		}

		if (self->pitchAddress_ != 0 && self->yawAddress_ != 0) return true;
	}
	return false;
}

void EngineCalls::TryKnownRvas() {
	const uintptr_t base = memory_.module().base;
	if (base == 0) {
		status_.message = "game module not mapped";
		return;
	}

	const uintptr_t pitch = base + Offsets::EngineCalls::AddPitchInput;
	const uintptr_t yaw = base + Offsets::EngineCalls::AddYawInput;
	const bool verifiedPitch =
		VerifyAddInput(pitch, static_cast<uint32_t>(Offsets::Aim::RotationInputPitch));
	const bool verifiedYaw =
		VerifyAddInput(yaw, static_cast<uint32_t>(Offsets::Aim::RotationInputYaw));

	if (verifiedPitch && verifiedYaw) {
		Adopt(pitch, yaw, false);
		return;
	}
	status_.message = "known RVAs not verified; starting signature scan";
}

void EngineCalls::Adopt(uintptr_t pitch, uintptr_t yaw, bool foundByScan) {
	pitchAddress_ = pitch;
	yawAddress_ = yaw;
	addPitch_ = reinterpret_cast<AddInputFn>(pitch);
	addYaw_ = reinterpret_cast<AddInputFn>(yaw);

	const uintptr_t base = memory_.module().base;
	status_.ok = true;
	status_.foundByScan = foundByScan;
	status_.rvaPitch = (base != 0 && pitch >= base) ? pitch - base : 0;
	status_.rvaYaw = (base != 0 && yaw >= base) ? yaw - base : 0;

	char buffer[192] = {};
	std::snprintf(buffer, sizeof(buffer),
	              "AddPitchInput 0x%llX / AddYawInput 0x%llX verified (%s)",
	              static_cast<unsigned long long>(status_.rvaPitch),
	              static_cast<unsigned long long>(status_.rvaYaw),
	              foundByScan ? "by signature scan" : "known RVA");
	status_.message = buffer;
}

void EngineCalls::ProbeScale(uintptr_t playerController, bool yaw) {
	if (!ready() || !nova::IsPlausiblePointer(playerController)) return;

	AddInputFn function = yaw ? addYaw_ : addPitch_;
	if (function == nullptr) return;

	const uintptr_t field = playerController +
		(yaw ? Offsets::Aim::RotationInputYaw : Offsets::Aim::RotationInputPitch);
	double& scale = yaw ? yawScale_ : pitchScale_;
	bool& calibrated = yaw ? yawCalibrated_ : pitchCalibrated_;

	constexpr float kProbe = 0.05f;
	double before = 0.0;
	double after = 0.0;
	if (!ReadDouble(memory_, field, before)) return;
	if (!InvokeOnGameThread(function, playerController, kProbe)) return;
	if (!ReadDouble(memory_, field, after)) return;
	CalibrateFrom(before, after, kProbe, scale, calibrated);
	SyncStatus();
}

void EngineCalls::SyncStatus() {
	status_.yawScale = yawScale_;
	status_.pitchScale = pitchScale_;
	status_.yawCalibrated = yawCalibrated_;
	status_.pitchCalibrated = pitchCalibrated_;
}

void EngineCalls::CalibrateFrom(double before, double after, float sent, double& scale,
                                bool& calibrated) {
	if (std::fabs(sent) < 1e-5) return;

	const double applied = after - before;
	if (std::fabs(applied) < 1e-9) return;

	const double measured = applied / static_cast<double>(sent);
	if (!(measured == measured)) return;
	if (std::fabs(measured) < 0.01 || std::fabs(measured) > 100.0) return;

	if (!calibrated) {
		scale = measured;
		calibrated = true;
		return;
	}
	if ((measured < 0.0) != (scale < 0.0)) return;
	const double ratio = std::fabs(measured / scale);
	if (ratio < 0.34 || ratio > 3.0) return;

	scale = scale * 0.75 + measured * 0.25;
}

bool EngineCalls::InvokeOnGameThread(AddInputFn function, uintptr_t playerController, float value) {
	if (!gameThread_.verified() || function == nullptr) return false;

	struct Invocation {
		AddInputFn          function = nullptr;
		void*               controller = nullptr;
		float               value = 0.0f;
		std::atomic<bool>   succeeded{ false };
	};

	auto invocation = std::make_shared<Invocation>();
	invocation->function = function;
	invocation->controller = reinterpret_cast<void*>(playerController);
	invocation->value = value;

	const GameThreadExecutor::Result result = gameThread_.Execute(
		[invocation] {
			invocation->succeeded =
				SafeInvoke(invocation->function, invocation->controller, invocation->value);
		},
		kEngineCallTimeoutMs);
	if (result != GameThreadExecutor::Result::Completed) return false;
	return invocation->succeeded.load();
}

bool EngineCalls::AddLookInput(uintptr_t playerController, double deltaYaw, double deltaPitch,
                               double maxStep) {
	if (!enginePathVerified() || !nova::IsPlausiblePointer(playerController)) return false;

	if (!yawCalibrated_ && std::fabs(deltaYaw) > 1e-4) ProbeScale(playerController, true);
	if (!pitchCalibrated_ && std::fabs(deltaPitch) > 1e-4) ProbeScale(playerController, false);

	if (!(maxStep >= 0.1)) maxStep = 0.1;
	if (deltaYaw > maxStep) deltaYaw = maxStep;
	if (deltaYaw < -maxStep) deltaYaw = -maxStep;
	if (deltaPitch > maxStep) deltaPitch = maxStep;
	if (deltaPitch < -maxStep) deltaPitch = -maxStep;

	const auto clampInput = [](double value) -> float {
		if (value > 4000.0) value = 4000.0;
		if (value < -4000.0) value = -4000.0;
		return static_cast<float>(value);
	};

	bool any = false;

	if (std::fabs(deltaYaw) > 1e-4) {
		const double scale = std::fabs(yawScale_) > 1e-6 ? yawScale_ : 1.0;
		const float value = clampInput(deltaYaw / scale);
		double before = 0.0;
		double after = 0.0;
		const bool readBefore =
			ReadDouble(memory_, playerController + Offsets::Aim::RotationInputYaw, before);
		// No cross-method fallback: a partial or timed-out call is reported as
		// failure, never retried, so an axis can never be applied twice.
		if (!InvokeOnGameThread(addYaw_, playerController, value)) return false;
		const bool readAfter =
			ReadDouble(memory_, playerController + Offsets::Aim::RotationInputYaw, after);
		if (readBefore && readAfter) {
			CalibrateFrom(before, after, value, yawScale_, yawCalibrated_);
		}
		any = true;
	}

	if (std::fabs(deltaPitch) > 1e-4) {
		const double scale = std::fabs(pitchScale_) > 1e-6 ? pitchScale_ : 1.0;
		const float value = clampInput(deltaPitch / scale);
		double before = 0.0;
		double after = 0.0;
		const bool readBefore =
			ReadDouble(memory_, playerController + Offsets::Aim::RotationInputPitch, before);
		if (!InvokeOnGameThread(addPitch_, playerController, value)) return false;
		const bool readAfter =
			ReadDouble(memory_, playerController + Offsets::Aim::RotationInputPitch, after);
		if (readBefore && readAfter) {
			CalibrateFrom(before, after, value, pitchScale_, pitchCalibrated_);
		}
		any = true;
	}

	SyncStatus();
	return any;
}

bool EngineCalls::AddLookInputDirect(uintptr_t playerController, double deltaYaw, double deltaPitch,
                                     double maxStep) {
	if (!nova::IsPlausiblePointer(playerController)) return false;
	if (!gameThread_.verified()) return false;

	if (!(maxStep >= 0.1)) maxStep = 0.1;
	if (deltaYaw > maxStep) deltaYaw = maxStep;
	if (deltaYaw < -maxStep) deltaYaw = -maxStep;
	if (deltaPitch > maxStep) deltaPitch = maxStep;
	if (deltaPitch < -maxStep) deltaPitch = -maxStep;

	// Both axes are read-modify-written inside ONE game-thread task so the
	// engine observes a consistent pair and never a mixed update. All
	// requested values are read first; if any read fails, nothing is written,
	// and every requested write must succeed for the call to report success.
	struct Writes {
		uintptr_t controller = 0;
		double    deltaYaw = 0.0;
		double    deltaPitch = 0.0;
		bool      wantYaw = false;
		bool      wantPitch = false;
		bool      ok = false;
	};

	auto writes = std::make_shared<Writes>();
	writes->controller = playerController;
	writes->deltaYaw = deltaYaw;
	writes->deltaPitch = deltaPitch;
	writes->wantYaw = std::fabs(deltaYaw) > 1e-4;
	writes->wantPitch = std::fabs(deltaPitch) > 1e-4;

	if (!writes->wantYaw && !writes->wantPitch) return false;

	const GameThreadExecutor::Result result = gameThread_.Execute(
		[this, writes] {
			double currentYaw = 0.0;
			double currentPitch = 0.0;
			const bool readYaw = !writes->wantYaw ||
				ReadDouble(memory_, writes->controller + Offsets::Aim::RotationInputYaw,
				           currentYaw);
			const bool readPitch = !writes->wantPitch ||
				ReadDouble(memory_, writes->controller + Offsets::Aim::RotationInputPitch,
				           currentPitch);
			if (!readYaw || !readPitch) return;

			bool ok = true;
			if (writes->wantYaw) {
				ok = GuardedWriteDouble(writes->controller + Offsets::Aim::RotationInputYaw,
				                        currentYaw + writes->deltaYaw) && ok;
			}
			if (writes->wantPitch) {
				ok = GuardedWriteDouble(writes->controller + Offsets::Aim::RotationInputPitch,
				                        currentPitch + writes->deltaPitch) && ok;
			}
			writes->ok = ok;
		},
		kEngineCallTimeoutMs);

	if (result != GameThreadExecutor::Result::Completed) return false;
	return writes->ok;
}

bool EngineCalls::SetControlRotation(uintptr_t playerController, const nova::FRotator& rotation) {
	if (!nova::IsPlausiblePointer(playerController)) return false;
	if (!gameThread_.verified()) return false;

	struct Writes {
		uintptr_t base = 0;
		double    pitch = 0.0;
		double    yaw = 0.0;
		bool      ok = false;
	};

	auto writes = std::make_shared<Writes>();
	writes->base = playerController + Offsets::Aim::ControlRotation;
	writes->pitch = rotation.pitch;
	writes->yaw = rotation.yaw;

	const GameThreadExecutor::Result result = gameThread_.Execute(
		[writes] {
			const bool pitchOk = GuardedWriteDouble(writes->base + 0x00, writes->pitch);
			const bool yawOk = GuardedWriteDouble(writes->base + 0x08, writes->yaw);
			writes->ok = pitchOk && yawOk;
		},
		kEngineCallTimeoutMs);

	if (result != GameThreadExecutor::Result::Completed) return false;
	return writes->ok;
}

} // namespace nova_host
