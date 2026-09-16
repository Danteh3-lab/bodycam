#include "Particles.hpp"

#include <cmath>
#include <cstdlib>

namespace nova_host {
namespace {

float RandomRange(float low, float high) {
	const float unit = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
	return low + unit * (high - low);
}

} // namespace

void ParticleField::Reset() {
	particles_.clear();
	spawnTimer_ = 0.0f;
	enabled_ = false;
}

void ParticleField::Draw(ImDrawList* drawList, const ImVec2& min, const ImVec2& max,
                         float deltaTime, bool enabled) {
	if (drawList == nullptr) return;

	const float width = max.x - min.x;
	const float height = max.y - min.y;
	if (width <= 1.0f || height <= 1.0f) return;

	if (!enabled) {
		enabled_ = false;
		particles_.clear();
		return;
	}

	if (!enabled_) {
		enabled_ = true;
		particles_.clear();
		for (size_t i = 0; i < 100; ++i) {
			Particle particle;
			particle.x = min.x + RandomRange(0.0f, width);
			particle.y = min.y + RandomRange(0.0f, height);
			particle.angle = RandomRange(0.0f, 6.28318f);
			particle.speed = RandomRange(0.2f, 0.5f);
			particle.phase = RandomRange(0.0f, 6.28318f);
			particles_.push_back(particle);
		}
	}

	if (deltaTime > 0.25f) deltaTime = 0.25f;
	spawnTimer_ += deltaTime;
	if (spawnTimer_ >= kSpawnInterval && particles_.size() < kMaxParticles) {
		Particle particle;
		particle.x = min.x + RandomRange(0.0f, width);
		particle.y = min.y + RandomRange(0.0f, height);
		particle.angle = RandomRange(0.0f, 6.28318f);
		particle.speed = RandomRange(0.2f, 0.5f);
		particle.phase = RandomRange(0.0f, 6.28318f);
		particles_.push_back(particle);
		spawnTimer_ = 0.0f;
	}

	const ImVec2 mouse = ImGui::GetMousePos();
	const float influenceMax = 150.0f;
	const float influenceMin = 50.0f;
	const double time = ImGui::GetTime();

	drawList->PushClipRect(min, max, true);
	for (Particle& particle : particles_) {
		particle.x += static_cast<float>(std::cos(particle.angle)) * particle.speed;
		particle.y += static_cast<float>(std::sin(particle.angle)) * particle.speed;
		particle.angle += deltaTime * 0.3f;

		const float dx = particle.x - mouse.x;
		const float dy = particle.y - mouse.y;
		const float distance = std::sqrt(dx * dx + dy * dy);
		if (distance < influenceMax && distance > 0.001f) {
			const float falloff = distance < influenceMin
				? 1.0f
				: 1.0f - (distance - influenceMin) / (influenceMax - influenceMin);
			particle.x += dx / distance * falloff * 3.0f;
			particle.y += dy / distance * falloff * 3.0f;
		}

		if (particle.x < min.x) particle.x = max.x;
		if (particle.x > max.x) particle.x = min.x;
		if (particle.y < min.y) particle.y = max.y;
		if (particle.y > max.y) particle.y = min.y;

		const float opacity = 0.30f + 0.15f * static_cast<float>(
			std::sin(time * static_cast<double>(particle.speed) * 2.0 + static_cast<double>(particle.phase)));
		const int alpha = static_cast<int>(opacity * 255.0f);
		drawList->AddCircleFilled(ImVec2(particle.x, particle.y), 1.8f,
		                          IM_COL32(120, 220, 228, alpha), 12);
	}
	drawList->PopClipRect();
}

} // namespace nova_host
