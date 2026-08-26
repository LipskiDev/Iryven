#pragma once

#include <cstdint>
#include <vector>

#include <rhi/device.h>

namespace Iryven {

	using BindlessTextureIndex = uint32_t;

	class BindlessTextureManager {
	public:
		BindlessTextureManager(
			Velos::RHI::IDevice& device,
			uint32_t capacity
		);
		~BindlessTextureManager();

		BindlessTextureManager(const BindlessTextureManager&) = delete;
		BindlessTextureManager& operator=(const BindlessTextureManager&) = delete;

		BindlessTextureIndex Register(
			Velos::RHI::ImageViewHandle view,
			Velos::RHI::SamplerHandle sampler
		);

		void Release(BindlessTextureIndex index, uint64_t retirementSubmission);

		void CollectGarbage(uint64_t completedSubmission);

		[[nodiscard]] Velos::RHI::BindingLayoutHandle Layout() const;
		[[nodiscard]] Velos::RHI::BindingSetHandle BindingSet() const;

	private:
		Velos::RHI::IDevice& device_;
		Velos::RHI::BindingLayoutHandle bindingLayout_;
		Velos::RHI::BindingSetHandle bindingSet_;
		Velos::RHI::BindingPoolHandle bindingPool_;
		uint32_t capacity_;

		std::vector<BindlessTextureIndex> freeIndices_;

		struct ReleasedTexture {
			BindlessTextureIndex index;
			uint64_t retirementSubmission;
		};

		std::vector<ReleasedTexture> retiredSlots_;
		enum class SlotState : std::uint8_t { Free, Active, Retired };
		std::vector<SlotState> slotStates_;
		void WriteDescriptor(
			BindlessTextureIndex index,
			Velos::RHI::ImageViewHandle view,
			Velos::RHI::SamplerHandle sampler);

		BindlessTextureIndex nextIndex_ = 0;
	};
}
