#include "particle_system.h"

namespace Iryven {

ParticleSystem::ParticleSystem(Velos::RHI::IDevice *device) : device_(device) {
  CreateResources();
}

ParticleSystem::~ParticleSystem() {
  Destroy();
}

void ParticleSystem::Update(float deltaTime) {
  UploadEmitterChanges();
  UploadSpawnRequests();
}

void ParticleSystem::Render() {
  ResetCountersPass();
  SimulationPass();
  SpawnPass();
  BuildIndirectArgsPass();
  RenderPass();
}

void ParticleSystem::CreateResources() {
  particleBuffer_ = device_->CreateBuffer({
      .size = sizeof(GPUParticle) * kMaxParticles,
      .usage =
          Velos::RHI::BufferUsage::Storage | Velos::RHI::BufferUsage::Indirect,
      .memoryUsage = Velos::RHI::MemoryUsage::GPUOnly,
      .concurrentQueues = false,
  });

  aliveBuffer_ = device_->CreateBuffer({
      .size = sizeof(uint32_t) * kMaxParticles,
      .usage =
          Velos::RHI::BufferUsage::Storage | Velos::RHI::BufferUsage::Indirect,
      .memoryUsage = Velos::RHI::MemoryUsage::GPUOnly,
      .concurrentQueues = false,
  });

  deadBuffer_ = device_->CreateBuffer({
      .size = sizeof(uint32_t) * kMaxParticles,
      .usage =
          Velos::RHI::BufferUsage::Storage | Velos::RHI::BufferUsage::Indirect,
      .memoryUsage = Velos::RHI::MemoryUsage::GPUOnly,
      .concurrentQueues = false,
  });

  spawnRequestBuffer_ = device_->CreateBuffer({
      .size = sizeof(uint32_t) * kMaxParticles,
      .usage =
          Velos::RHI::BufferUsage::Storage | Velos::RHI::BufferUsage::Indirect,
      .memoryUsage = Velos::RHI::MemoryUsage::GPUOnly,
      .concurrentQueues = false,
  });

  counterBuffer_ = device_->CreateBuffer({
      .size = sizeof(uint32_t) *
              4,  // aliveCount, deadCount, spawnRequestCount, padding
      .usage =
          Velos::RHI::BufferUsage::Storage | Velos::RHI::BufferUsage::Indirect,
      .memoryUsage = Velos::RHI::MemoryUsage::GPUOnly,
      .concurrentQueues = false,
  });

  indirectArgsBuffer_ = device_->CreateBuffer({
      .size = sizeof(Velos::RHI::DrawIndexedIndirectCommand) * 2,
      .usage =
          Velos::RHI::BufferUsage::Storage | Velos::RHI::BufferUsage::Indirect,
      .memoryUsage = Velos::RHI::MemoryUsage::GPUOnly,
      .concurrentQueues = false,
  });
}

void ParticleSystem::Destroy() {}

void ParticleSystem::UploadEmitterChanges() {}

void ParticleSystem::UploadSpawnRequests() {}

void ParticleSystem::ResetCountersPass() {}

void ParticleSystem::SimulationPass() {}

void ParticleSystem::SpawnPass() {}

void ParticleSystem::BuildIndirectArgsPass() {}

void ParticleSystem::RenderPass() {}
}  // namespace Iryven