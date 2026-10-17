#pragma once

#include <cstdint>
#include <vector>

#include <rhi/device.h>

namespace Iryven {

// Packed texture slot (low 12 bits) and shared sampler slot (upper bits).
using BindlessTextureIndex = uint32_t;

class BindlessTextureManager {
 public:
  BindlessTextureManager(Velos::RHI::IDevice &device, uint32_t capacity);
  ~BindlessTextureManager();

  BindlessTextureManager(const BindlessTextureManager &) = delete;
  BindlessTextureManager &operator=(const BindlessTextureManager &) = delete;

  BindlessTextureIndex Register(Velos::RHI::ImageViewHandle view,
                                const Velos::RHI::SamplerDesc &sampler);

  static constexpr uint32_t TextureSlot(BindlessTextureIndex reference) {
    return reference & 4095u;
  }
  static constexpr uint32_t SamplerSlot(BindlessTextureIndex reference) {
    return reference >> 12u;
  }

  void Release(BindlessTextureIndex index, uint64_t retirementSubmission);

  void CollectGarbage(uint64_t completedSubmission);

  [[nodiscard]] Velos::RHI::BindingLayoutHandle Layout() const;
  [[nodiscard]] Velos::RHI::BindingSetHandle BindingSet() const;

 private:
  Velos::RHI::IDevice &device_;
  Velos::RHI::BindingLayoutHandle bindingLayout_;
  Velos::RHI::BindingSetHandle bindingSet_;
  Velos::RHI::BindingPoolHandle bindingPool_;
  uint32_t capacity_;

  std::vector<BindlessTextureIndex> freeIndices_;
  static constexpr uint32_t kSamplerCapacity = 128;
  struct SharedSampler {
    Velos::RHI::SamplerDesc desc;
    Velos::RHI::SamplerHandle handle;
    uint32_t references = 0;
  };
  std::vector<SharedSampler> sharedSamplers_;
  std::vector<uint32_t> textureSamplers_;
  uint32_t FindOrCreateSampler(const Velos::RHI::SamplerDesc &desc);

  struct ReleasedTexture {
    BindlessTextureIndex index;
    uint64_t retirementSubmission;
  };

  std::vector<ReleasedTexture> retiredSlots_;
  enum class SlotState : std::uint8_t { Free, Active, Retired };
  std::vector<SlotState> slotStates_;
  void WriteDescriptor(BindlessTextureIndex index,
                       Velos::RHI::ImageViewHandle view,
                       const Velos::RHI::SamplerDesc &sampler);

  BindlessTextureIndex nextIndex_ = 0;
};
}  // namespace Iryven
