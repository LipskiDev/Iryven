#pragma once

#include <stdint.h>
#include <rhi/handles.h>
#include <rhi/device.h>
#include <glm/glm.hpp>

namespace Iryven {

using ParticleEmitterHandle = uint32_t;

struct GPUParticle {
  glm::vec4 positionAge;
  glm::vec4 velocityLifetime;
  glm::vec4 color;
  glm::vec4 sizeRotation;
};  // = 64 bytes

constexpr uint32_t kMaxParticles = 1'000'000;

class ParticleSystem {
 public:
  ParticleSystem(Velos::RHI::IDevice *device);
  ~ParticleSystem();

  void Update(float deltaTime);
  void Render();

  void CreateResources();
  void Destroy();

 private:
  void UploadEmitterChanges();
  void UploadSpawnRequests();

  void ResetCountersPass();
  void SimulationPass();
  void SpawnPass();
  void BuildIndirectArgsPass();
  void RenderPass();

  // Particle, alive/dead, requests, counter and indirect buffers
  Velos::RHI::IDevice *device_;

  Velos::RHI::BufferHandle particleBuffer_;
  Velos::RHI::BufferHandle aliveBuffer_;
  Velos::RHI::BufferHandle deadBuffer_;
  Velos::RHI::BufferHandle spawnRequestBuffer_;
  Velos::RHI::BufferHandle counterBuffer_;
  Velos::RHI::BufferHandle indirectArgsBuffer_;
};

}  // namespace Iryven
