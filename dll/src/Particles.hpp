// ============================================================================
// Particles — NOVA's single signature visual.
//
// Drawn behind the menu contents, clipped to the menu rectangle, and fully
// disabled when Windows animations are off or "Reduce motion" is enabled.
// ============================================================================
#pragma once
#include <imgui.h>

#include <vector>

namespace nova_host {

class ParticleField {
public:
	void Reset();
	void Draw(ImDrawList* drawList, const ImVec2& min, const ImVec2& max,
	          float deltaTime, bool enabled);

	[[nodiscard]] bool active() const { return enabled_; }
	[[nodiscard]] size_t count() const { return particles_.size(); }

private:
	struct Particle {
		float x = 0.0f;
		float y = 0.0f;
		float angle = 0.0f;
		float speed = 0.0f;
		float phase = 0.0f;
	};

	std::vector<Particle> particles_;
	bool enabled_ = false;

	static constexpr size_t kMaxParticles = 150;
	static constexpr float kSpawnInterval = 0.5f;
	float spawnTimer_ = 0.0f;
};

} // namespace nova_host
