#pragma once

namespace Iryven {

struct ParticleEmitter {
  float emissionRate = 10.0f;  // Particles emitted per second
  float lifetime = 1.0f;

  glm::vec3 velocity =
      glm::vec3(0.0f, 1.0f, 0.0f);  // Initial velocity of particles
  glm::vec3 velocityVariance = glm::vec3(0.0f);
  glm::vec3 position = glm::vec3(0.0f);

  uint32_t material = 0;
  bool enabled = true;
  bool oneshot = false;
};

}  // namespace Iryven
