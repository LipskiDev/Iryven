#include "bindless_texture_manager.h"

#include <iryven/log.h>
#include <stdexcept>
#include <tuple>

namespace Iryven {
BindlessTextureManager::BindlessTextureManager(Velos::RHI::IDevice &device,
                                               uint32_t capacity)
    : device_(device),
      capacity_(capacity),
      textureSamplers_(capacity, 0),
      slotStates_(capacity, SlotState::Free) {
  if (capacity_ == 0 || capacity_ > 4096) {
    throw std::invalid_argument(
        "BindlessTextureManager capacity must be between 1 and 4096");
  }
  sharedSamplers_.reserve(kSamplerCapacity);
  const Velos::RHI::BindingFlags sharedFlags =
      Velos::RHI::BindingFlags::UpdateAfterBind |
      Velos::RHI::BindingFlags::PartiallyBound;
  Velos::RHI::BindingDesc bindingDescs[] = {
      {.binding = 0,
       .type = Velos::RHI::BindingType::SampledTexture,
       .count = capacity_,
       .visibility = Velos::RHI::ShaderStage::Vertex |
                     Velos::RHI::ShaderStage::Fragment |
                     Velos::RHI::ShaderStage::Compute,
       .flags = sharedFlags},
      {
          .binding = 1,
          .type = Velos::RHI::BindingType::Sampler,
          .count = kSamplerCapacity,
          .visibility = Velos::RHI::ShaderStage::Vertex |
                        Velos::RHI::ShaderStage::Fragment |
                        Velos::RHI::ShaderStage::Compute,
          .flags = sharedFlags | Velos::RHI::BindingFlags::VariableCount,
      }};

  bindingLayout_ = device_.CreateBindingLayout(
      {.bindings = bindingDescs,
       .bindingCount = 2,
       .debugName = "Iryven bindless texture layout"});

  Velos::RHI::BindingPoolSize poolSizes[] = {
      {.type = Velos::RHI::BindingType::SampledTexture, .count = capacity_},
      {Velos::RHI::BindingType::Sampler, kSamplerCapacity}};

  try {
    bindingPool_ = device_.CreateBindingPool(
        {.poolSizes = poolSizes,
         .poolSizeCount = 2,
         .maxSets = 1,
         .flags = Velos::RHI::BindingPoolFlags::UpdateAfterBind,
         .debugName = "Iryven bindless texture pool"});

    bindingSet_ = device_.AllocateBindingSet(
        {.pool = bindingPool_,
         .layout = bindingLayout_,
         .variableBindingCount = kSamplerCapacity,
         .debugName = "Iryven bindless texture set"});
  } catch (...) {
    if (bindingPool_)
      device_.DestroyBindingPool(bindingPool_);
    device_.DestroyBindingLayout(bindingLayout_);
    throw;
  }
}

BindlessTextureManager::~BindlessTextureManager() {
  device_.DestroyBindingPool(bindingPool_);
  device_.DestroyBindingLayout(bindingLayout_);
  for (const auto &sampler : sharedSamplers_)
    device_.DestroySampler(sampler.handle);
}

BindlessTextureIndex BindlessTextureManager::Register(
    Velos::RHI::ImageViewHandle view, const Velos::RHI::SamplerDesc &sampler) {
  if (!freeIndices_.empty()) {
    const BindlessTextureIndex index = freeIndices_.back();
    freeIndices_.pop_back();
    slotStates_[index] = SlotState::Active;
    try {
      WriteDescriptor(index, view, sampler);
    } catch (...) {
      slotStates_[index] = SlotState::Free;
      freeIndices_.push_back(index);
      throw;
    }
    return index | (textureSamplers_[index] << 12u);
  }

  if (nextIndex_ >= capacity_) {
    throw std::runtime_error("Bindless texture descriptor capacity exhausted");
  }

  const BindlessTextureIndex index = nextIndex_;
  slotStates_[index] = SlotState::Active;
  try {
    WriteDescriptor(index, view, sampler);
    ++nextIndex_;
  } catch (...) {
    slotStates_[index] = SlotState::Free;
    throw;
  }

  return index | (textureSamplers_[index] << 12u);
}

uint32_t BindlessTextureManager::FindOrCreateSampler(
    const Velos::RHI::SamplerDesc &desc) {
  const auto key = [](const Velos::RHI::SamplerDesc &value) {
    return std::tuple{
        value.minFilter,     value.magFilter, value.addressU,
        value.addressV,      value.addressW,  value.enableAnisotropy,
        value.maxAnisotropy, value.minLod,    value.maxLod};
  };
  for (uint32_t index = 0; index < sharedSamplers_.size(); ++index)
    if (key(sharedSamplers_[index].desc) == key(desc))
      return index;
  uint32_t index = 0;
  while (index < sharedSamplers_.size() && sharedSamplers_[index].references)
    ++index;
  if (index == kSamplerCapacity)
    throw std::runtime_error("Bindless shared sampler capacity exhausted");
  const auto handle = device_.CreateSampler(desc);
  try {
    const Velos::RHI::BindingImageInfo info{.sampler = handle};
    device_.UpdateBindingSet({.dstSet = bindingSet_,
                              .binding = 1,
                              .arrayElement = index,
                              .type = Velos::RHI::BindingType::Sampler,
                              .imageInfo = &info});
  } catch (...) {
    device_.DestroySampler(handle);
    throw;
  }
  if (index == sharedSamplers_.size())
    sharedSamplers_.push_back({desc, handle, 0});
  else {
    device_.DestroySampler(sharedSamplers_[index].handle);
    sharedSamplers_[index] = {desc, handle, 0};
  }
  return index;
}

void BindlessTextureManager::WriteDescriptor(
    BindlessTextureIndex index, Velos::RHI::ImageViewHandle view,
    const Velos::RHI::SamplerDesc &sampler) {
  const uint32_t samplerIndex = FindOrCreateSampler(sampler);
  const Velos::RHI::BindingImageInfo info{
      .imageView = view,
      .imageLayout = Velos::RHI::ImageLayout::ShaderReadOnly};
  device_.UpdateBindingSet({.dstSet = bindingSet_,
                            .binding = 0,
                            .arrayElement = index,
                            .type = Velos::RHI::BindingType::SampledTexture,
                            .imageInfo = &info});
  textureSamplers_[index] = samplerIndex;
  ++sharedSamplers_[samplerIndex].references;
}

void BindlessTextureManager::Release(BindlessTextureIndex index,
                                     uint64_t retirementSubmission) {
  const uint32_t samplerIndex = SamplerSlot(index);
  index = TextureSlot(index);
  if (index == 0) {
    IRYVEN_CORE_ERROR(
        "BindlessTextureManager: Cannot release error texture at index 0");
    return;
  }
  if (index >= nextIndex_) {
    throw std::out_of_range(
        "Cannot release an unallocated bindless texture index");
  }
  if (slotStates_[index] != SlotState::Active ||
      textureSamplers_[index] != samplerIndex) {
    throw std::logic_error("Bindless texture index is not active");
  }

  slotStates_[index] = SlotState::Retired;
  retiredSlots_.push_back({index, retirementSubmission});
}

void BindlessTextureManager::CollectGarbage(uint64_t completedSubmission) {
  auto it = retiredSlots_.begin();

  while (it != retiredSlots_.end()) {
    if (it->retirementSubmission <= completedSubmission) {
      --sharedSamplers_[textureSamplers_[it->index]].references;
      slotStates_[it->index] = SlotState::Free;
      freeIndices_.push_back(it->index);
      it = retiredSlots_.erase(it);
    } else {
      ++it;
    }
  }
}

Velos::RHI::BindingLayoutHandle BindlessTextureManager::Layout() const {
  return bindingLayout_;
}

Velos::RHI::BindingSetHandle BindlessTextureManager::BindingSet() const {
  return bindingSet_;
}

}  // namespace Iryven
