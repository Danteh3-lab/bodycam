#include "mythos/RuntimeDiagnostics.hpp"

namespace mythos {

const char* RuntimeStateName(RuntimeState state) {
	switch (state) {
	case RuntimeState::Starting: return "Starting";
	case RuntimeState::WaitingForWindow: return "Waiting for match";
	case RuntimeState::Resolving: return "Resolving offsets";
	case RuntimeState::Ready: return "Ready";
	case RuntimeState::OffsetsInvalid: return "Offsets invalid";
	case RuntimeState::Stopping: return "Stopping";
	}
	return "Unknown";
}

const char* RuntimeStateDescription(RuntimeState state) {
	switch (state) {
	case RuntimeState::Starting: return "MYTHOS is starting its worker threads.";
	case RuntimeState::WaitingForWindow: return "Waiting for the game window and a loaded world.";
	case RuntimeState::Resolving: return "Locating and validating the read-only world chain.";
	case RuntimeState::Ready: return "World, camera, roster and name pool are validated.";
	case RuntimeState::OffsetsInvalid: return "Build mismatch: ESP stays disabled.";
	case RuntimeState::Stopping: return "Shutting down cleanly.";
	}
	return "Unknown state.";
}

} // namespace mythos
