#include "bindless_texture_manager.h"

#include <iryven/log.h>
#include <stdexcept>

namespace Iryven {
	BindlessTextureManager::BindlessTextureManager(Velos::RHI::IDevice& device, uint32_t capacity)
		: device_(device), capacity_(capacity), slotStates_(capacity, SlotState::Free)
	{
		if (capacity_ == 0) {
			throw std::invalid_argument("BindlessTextureManager capacity must be greater than zero");
		}
		Velos::RHI::BindingDesc bindingDesc = {
					.binding = 0,
					.type = Velos::RHI::BindingType::CombinedImageSampler,
					.count = capacity_,
					.visibility = Velos::RHI::ShaderStage::Vertex | Velos::RHI::ShaderStage::Fragment | Velos::RHI::ShaderStage::Compute,
					.flags = Velos::RHI::BindingFlags::UpdateAfterBind | Velos::RHI::BindingFlags::VariableCount | Velos::RHI::BindingFlags::PartiallyBound
		};

		bindingLayout_ = device_.CreateBindingLayout({
			.bindings = &bindingDesc,
			.bindingCount = 1,
			.debugName = "Iryven bindless texture layout"
		});

		Velos::RHI::BindingPoolSize poolSize = {
			.type = Velos::RHI::BindingType::CombinedImageSampler,
			.count = capacity_
		};

		try {
			bindingPool_ = device_.CreateBindingPool({
			.poolSizes = &poolSize,
			.poolSizeCount = 1,
			.maxSets = 1,
			.flags = Velos::RHI::BindingPoolFlags::UpdateAfterBind,
			.debugName = "Iryven bindless texture pool"
			});

			bindingSet_ = device_.AllocateBindingSet({
			.pool = bindingPool_,
			.layout = bindingLayout_,
			.variableBindingCount = capacity_,
			.debugName = "Iryven bindless texture set"
			});
		} catch (...) {
			if (bindingPool_) device_.DestroyBindingPool(bindingPool_);
			device_.DestroyBindingLayout(bindingLayout_);
			throw;
		}
	}

	BindlessTextureManager::~BindlessTextureManager()
	{
		device_.DestroyBindingPool(bindingPool_);
		device_.DestroyBindingLayout(bindingLayout_);
	}

	BindlessTextureIndex BindlessTextureManager::Register(Velos::RHI::ImageViewHandle view, Velos::RHI::SamplerHandle sampler)
	{
		if (!freeIndices_.empty())
		{
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
			return index;
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

		return index;
	}

	void BindlessTextureManager::WriteDescriptor(
		BindlessTextureIndex index,
		Velos::RHI::ImageViewHandle view,
		Velos::RHI::SamplerHandle sampler)
	{
		Velos::RHI::BindingImageInfo imageInfo = {
					.sampler = sampler,
					.imageView = view,
					.imageLayout = Velos::RHI::ImageLayout::ShaderReadOnly
		};

		device_.UpdateBindingSet({
			.dstSet = bindingSet_,
			.binding = 0,
			.arrayElement = index,
			.type = Velos::RHI::BindingType::CombinedImageSampler,
			.imageInfo = &imageInfo,
			.descriptorCount = 1
		});
	}

	void BindlessTextureManager::Release(BindlessTextureIndex index, uint64_t retirementSubmission)
	{
		if (index == 0) {
			IRYVEN_CORE_ERROR("BindlessTextureManager: Cannot release error texture at index 0");
			return;
		}
		if (index >= nextIndex_) {
			throw std::out_of_range("Cannot release an unallocated bindless texture index");
		}
		if (slotStates_[index] != SlotState::Active) {
			throw std::logic_error("Bindless texture index is not active");
		}

		slotStates_[index] = SlotState::Retired;
		retiredSlots_.push_back({ index, retirementSubmission });
	}

	void BindlessTextureManager::CollectGarbage(uint64_t completedSubmission)
	{
		auto it = retiredSlots_.begin();

		while (it != retiredSlots_.end())
		{
			if (it->retirementSubmission <= completedSubmission)
			{
				slotStates_[it->index] = SlotState::Free;
				freeIndices_.push_back(it->index);
				it = retiredSlots_.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	Velos::RHI::BindingLayoutHandle BindlessTextureManager::Layout() const
	{
		return bindingLayout_;
	}

	Velos::RHI::BindingSetHandle BindlessTextureManager::BindingSet() const
	{
		return bindingSet_;
	}

}
