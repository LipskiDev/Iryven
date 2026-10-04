#include "light_culling.h"
#include "shadow_mapping.h"
#include "../light_depth_bins.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <shader/shader_compiler.h>

namespace Iryven {

	Renderer::LightCullingPass::LightCullingPass(Renderer& renderer) : renderer_(renderer)
	{
		auto& device = *renderer_.device_;
		const BindingDesc bindings[]{
			{.binding = 0, .type = BindingType::StorageBuffer, .visibility = ShaderStage::Compute},
			{.binding = 1, .type = BindingType::UniformBuffer, .visibility = ShaderStage::Compute},
			{.binding = 2, .type = BindingType::StorageBuffer, .visibility = ShaderStage::Compute},
			{.binding = 3, .type = BindingType::StorageBuffer, .visibility = ShaderStage::Compute},
		};
		layout_ = device.CreateBindingLayout({ .bindings = bindings, .bindingCount = 4,
			.debugName = "Light tiling layout" });
		const auto code = Velos::ShaderCompiler::CompileFile({
			.path = "assets/shaders/internal/light_culling.comp.spv", .stage = ShaderStage::Compute,
			.language = Velos::ShaderSourceLanguage::SpirvBinary });
		shader_ = device.CreateShader({ .stage = ShaderStage::Compute, .bytecode = code.spirv.data(),
			.bytecodeSize = code.spirv.size() * sizeof(std::uint32_t),
			.reflection = code.reflection, .debugName = "Light tiling shader" });
		pipeline_ = device.CreateComputePipeline({ .computeShader = shader_,
			.layout = {.descriptorSetLayouts = &layout_, .descriptorSetLayoutCount = 1},
			.debugName = "Light tiling pipeline" });
		const auto dimensions = device.GetSwapchainDimensions();
		CreateResources(dimensions.width, dimensions.height);
	}

	Renderer::LightCullingPass::~LightCullingPass() { Destroy(); }

	void Renderer::LightCullingPass::CreateResources(std::uint32_t width, std::uint32_t height)
	{
		DestroyResources();
		if (width == 0 || height == 0) return;
		auto& device = *renderer_.device_;
		width_ = width;
		height_ = height;
		tilesX_ = (width + kTileSize - 1) / kTileSize;
		tilesY_ = (height + kTileSize - 1) / kTileSize;
		tileBufferSize_ = sizeof(glm::uvec4) +
			static_cast<std::size_t>(tilesX_) * tilesY_ * kWordsPerTile * sizeof(std::uint32_t);
		const BindingPoolSize sizes[]{
			{BindingType::UniformBuffer, k_FramesInFlight},
			{BindingType::StorageBuffer, 3 * k_FramesInFlight},
		};
		pool_ = device.CreateBindingPool({ .poolSizes = sizes, .poolSizeCount = 2,
			.maxSets = k_FramesInFlight, .debugName = "Light tiling pool" });
		for (std::uint32_t frame = 0; frame < k_FramesInFlight; ++frame) {
			tileBuffers_[frame] = device.CreateBuffer({ .size = tileBufferSize_,
				.usage = BufferUsage::Storage, .memoryUsage = MemoryUsage::GPUOnly,
				.concurrentQueues = true,
				.debugName = "Light tile masks" });
			shadowResolutionBuffers_[frame] = device.CreateBuffer({
				.size = sizeof(std::uint32_t) * k_MaxLightSources,
				.usage = BufferUsage::Storage | BufferUsage::TransferSrc | BufferUsage::TransferDst,
				.memoryUsage = MemoryUsage::GPUOnly, .concurrentQueues = true,
				.debugName = "Point shadow resolution estimates"});
			shadowResolutionResetUploads_[frame] = device.CreateBuffer({
				.size = sizeof(std::uint32_t) * k_MaxLightSources,
				.usage = BufferUsage::TransferSrc,
				.memoryUsage = MemoryUsage::CPUToGPU,
				.debugName = "Point shadow resolution reset upload"});
			shadowResolutionReadbacks_[frame] = device.CreateBuffer({
				.size = sizeof(std::uint32_t) * k_MaxLightSources,
				.usage = BufferUsage::TransferDst,
				.memoryUsage = MemoryUsage::GPUToCPU,
				.debugName = "Point shadow resolution readback"});
			cullingBuffers_[frame] = renderer_.CreateUploadBackedBuffer(sizeof(CullingData),
				BufferUsage::Uniform, "Light tiling data", "Light tiling upload");
			depthBinBuffers_[frame] = renderer_.CreateUploadBackedBuffer(sizeof(LightDepthBins),
				BufferUsage::Storage, "Light depth bins", "Light depth bins upload", true);
			sets_[frame] = device.AllocateBindingSet({ .pool = pool_, .layout = layout_ });
			const BindingBufferInfo lights{ .buffer = renderer_.lightingFrames_[frame].lightBuffer.gpuBuffer,
				.range = sizeof(glm::uvec4) + sizeof(glm::vec4) * 5 * k_MaxLightSources };
			const BindingBufferInfo culling{ .buffer = cullingBuffers_[frame].gpuBuffer,
				.range = sizeof(CullingData) };
			const BindingBufferInfo tiles{ .buffer = tileBuffers_[frame], .range = tileBufferSize_ };
			const BindingBufferInfo resolutions{.buffer = shadowResolutionBuffers_[frame],
				.range = sizeof(std::uint32_t) * k_MaxLightSources};
			device.UpdateBindingSet({ .dstSet = sets_[frame], .binding = 0,
				.type = BindingType::StorageBuffer, .bufferInfo = &lights });
			device.UpdateBindingSet({ .dstSet = sets_[frame], .binding = 1,
				.type = BindingType::UniformBuffer, .bufferInfo = &culling });
			device.UpdateBindingSet({.dstSet = sets_[frame], .binding = 2,
				.type = BindingType::StorageBuffer, .bufferInfo = &resolutions});
			device.UpdateBindingSet({ .dstSet = sets_[frame], .binding = 3,
				.type = BindingType::StorageBuffer, .bufferInfo = &tiles });
			// Set 0 is shared by deferred, indexed forward, and meshlet forward shading.
			device.UpdateBindingSet({ .dstSet = renderer_.lightingFrames_[frame].lightBindingSet,
				.binding = 3, .type = BindingType::StorageBuffer, .bufferInfo = &tiles });
			const BindingBufferInfo bins{.buffer = depthBinBuffers_[frame].gpuBuffer,
				.range = sizeof(LightDepthBins)};
			device.UpdateBindingSet({.dstSet = renderer_.lightingFrames_[frame].lightBindingSet,
				.binding = 4, .type = BindingType::StorageBuffer, .bufferInfo = &bins});
		}
	}

	FrameGraphBufferInfo Renderer::LightCullingPass::TileBufferInfo() const
	{
		return { .size = tileBufferSize_, .usage = BufferUsage::Storage,
			.handle = tileBuffers_.at(renderer_.frame_.frameIndex), .concurrentQueues = true };
	}

	FrameGraphBufferInfo Renderer::LightCullingPass::DepthBinBufferInfo() const
	{
		return {.size = sizeof(LightDepthBins), .usage = BufferUsage::Storage | BufferUsage::TransferDst,
			.handle = depthBinBuffers_.at(renderer_.frame_.frameIndex).gpuBuffer, .concurrentQueues = true};
	}

	void Renderer::LightCullingPass::DestroyResources()
	{
		auto& device = *renderer_.device_;
		if (pool_) device.DestroyBindingPool(pool_);
		pool_ = {};
		sets_ = {};
		for (auto& buffer : cullingBuffers_) renderer_.DestroyUploadBackedBuffer(buffer);
		for (auto& buffer : depthBinBuffers_) renderer_.DestroyUploadBackedBuffer(buffer);
		for (auto buffer : tileBuffers_) if (buffer) device.DestroyBuffer(buffer);
		for (auto buffer : shadowResolutionBuffers_) if (buffer) device.DestroyBuffer(buffer);
		for (auto buffer : shadowResolutionResetUploads_) if (buffer) device.DestroyBuffer(buffer);
		for (auto buffer : shadowResolutionReadbacks_) if (buffer) device.DestroyBuffer(buffer);
		tileBuffers_ = {};
		shadowResolutionBuffers_ = {};
		shadowResolutionResetUploads_ = {};
		shadowResolutionReadbacks_ = {};
		resolutionSceneIndices_ = {};
		resolutionReadbackReady_ = {};
		width_ = height_ = tilesX_ = tilesY_ = 0;
		tileBufferSize_ = 0;
	}

	void Renderer::LightCullingPass::Destroy()
	{
		DestroyResources();
		auto& device = *renderer_.device_;
		if (pipeline_) device.DestroyPipeline(pipeline_);
		if (shader_) device.DestroyShader(shader_);
		if (layout_) device.DestroyBindingLayout(layout_);
		pipeline_ = {};
		shader_ = {};
		layout_ = {};
	}

	void Renderer::LightCullingPass::PreRender(
		Velos::RHI::ICommandList& commands, const RenderScene& scene)
	{
		packedLights_.clear();
		localLights_.clear();
		shadowCandidates_.clear();
		renderer_.pointShadowAssignments_.clear();
		directionalLightCount_ = 0;
		measuredShadowResolutions_.clear();
		const auto frameIndex = renderer_.frame_.frameIndex;
		if (resolutionReadbackReady_[frameIndex]) {
			const auto* measured = static_cast<const std::uint32_t*>(
				renderer_.device_->MapBuffer(shadowResolutionReadbacks_[frameIndex]));
			const auto& indices = resolutionSceneIndices_[frameIndex];
			for (std::size_t i = 0; i < indices.size(); ++i) {
				if (measured[i] != 0u) measuredShadowResolutions_[indices[i]] = measured[i];
			}
			renderer_.device_->UnmapBuffer(shadowResolutionReadbacks_[frameIndex]);
		}

		if (scene.camera) {
			const auto count = std::min<std::size_t>(scene.lights.size(), k_MaxLightSources);
			packedLights_.reserve(count);
			localLights_.reserve(count);
			for (std::size_t i = 0; i < count; ++i) {
				const auto& light = scene.lights[i];
				if (light.type == LightType::Directional) {
					packedLights_.push_back(light);
				}
				else {
					const float depth = -(scene.camera->view * glm::vec4(light.position, 1.0f)).z;
					localLights_.push_back({ i, std::isfinite(depth)
						? depth : std::numeric_limits<float>::infinity() });
				}
			}
			directionalLightCount_ = static_cast<std::uint32_t>(packedLights_.size());
			std::sort(localLights_.begin(), localLights_.end(),
				[](const LocalLight& a, const LocalLight& b) {
					return a.depth == b.depth ? a.sceneIndex < b.sceneIndex : a.depth < b.depth;
				});
			shadowCandidates_.reserve(localLights_.size());
			const auto frameData = renderer_.BuildFrameData(*scene.camera);
			const float projectionScale = std::abs(frameData.projection[1][1]);
			for (std::size_t localIndex = 0; localIndex < localLights_.size(); ++localIndex) {
				const auto& local = localLights_[localIndex];
				const auto& light = scene.lights[local.sceneIndex];
				const std::uint64_t stableId = light.sourceId != 0
					? light.sourceId : static_cast<std::uint64_t>(local.sceneIndex + 1);
				packedLights_.push_back(light);
				const glm::vec3 viewPosition = glm::vec3(
					scene.camera->view * glm::vec4(light.position, 1.0f));
				const float horizontalExtent = local.depth /
					std::max(std::abs(frameData.projection[0][0]), 0.0001f);
				const float verticalExtent = local.depth /
					std::max(projectionScale, 0.0001f);
				const bool intersectsView =
					local.depth > scene.camera->nearPlane &&
					local.depth - light.range < scene.camera->farPlane &&
					std::abs(viewPosition.x) <= horizontalExtent + light.range &&
					std::abs(viewPosition.y) <= verticalExtent + light.range;
				if (light.type != LightType::Point || light.intensity <= 0.0f ||
					light.range <= 0.0f || !std::isfinite(local.depth) ||
					!intersectsView) {
					continue;
				}
				const float influence = light.range * projectionScale /
					std::max(local.depth, scene.camera->nearPlane);
				// Chapter 8 chooses shadow-map demand from the light's projected
				// influence rather than its rank among the other lights.  The
				// projected sphere diameter is expressed in framebuffer pixels.
				const float projectedDiameter = influence * static_cast<float>(height_);
				const float measuredDemand = measuredShadowResolutions_.contains(stableId)
					? static_cast<float>(measuredShadowResolutions_.at(stableId))
					: projectedDiameter;
				std::uint32_t resolution = measuredDemand > 512.0f ? 1024u
					: measuredDemand > 256.0f ? 512u
					: measuredDemand > 128.0f ? 256u : 128u;
				if (const auto previous = renderer_.pointShadowHistory_.find(stableId);
					previous != renderer_.pointShadowHistory_.end()) {
					// Resolution hysteresis prevents a light near a mip boundary from
					// reallocating sparse pages on alternating frames.
					resolution = previous->second.resolution;
					if (resolution == 128u && measuredDemand > 147.2f) resolution = 256u;
					else if (resolution == 256u && measuredDemand < 108.8f) resolution = 128u;
					else if (resolution == 256u && measuredDemand > 294.4f) resolution = 512u;
					else if (resolution == 512u && measuredDemand < 217.6f) resolution = 256u;
					else if (resolution == 512u && measuredDemand > 588.8f) resolution = 1024u;
					else if (resolution == 1024u && measuredDemand < 435.2f) resolution = 512u;
				}
				shadowCandidates_.push_back({
					.sceneIndex = local.sceneIndex,
					.stableId = stableId,
					.packedLightIndex = directionalLightCount_ +
						static_cast<std::uint32_t>(localIndex),
					.projectedInfluence = influence,
					.requestedResolution = resolution,
				});
			}

			std::ranges::sort(shadowCandidates_,
				[&renderer = renderer_](const ShadowCandidate& a, const ShadowCandidate& b) {
					const float aPriority = a.projectedInfluence *
						(renderer.pointShadowHistory_.contains(a.stableId) ? 1.1f : 1.0f);
					const float bPriority = b.projectedInfluence *
						(renderer.pointShadowHistory_.contains(b.stableId) ? 1.1f : 1.0f);
					return aPriority == bPriority
						? a.sceneIndex < b.sceneIndex
						: aPriority > bPriority;
				});
			const auto assignmentCount = std::min<std::size_t>(shadowCandidates_.size(),
				Renderer::ShadowMappingPass::kMaxShadowLights);
			shadowCandidates_.resize(assignmentCount);

			std::array<bool, Renderer::ShadowMappingPass::kMaxShadowLights> usedSlots{};
			for (auto& candidate : shadowCandidates_) {
				const auto previous = renderer_.pointShadowHistory_.find(candidate.stableId);
				if (previous != renderer_.pointShadowHistory_.end() &&
					previous->second.slot < usedSlots.size() &&
					!usedSlots[previous->second.slot]) {
					candidate.slot = previous->second.slot;
					usedSlots[candidate.slot] = true;
				}
				else {
					const auto freeSlot = std::ranges::find(usedSlots, false);
					candidate.slot = static_cast<std::uint32_t>(freeSlot - usedSlots.begin());
					usedSlots[candidate.slot] = true;
				}
			}
			renderer_.pointShadowAssignments_.reserve(
				static_cast<std::size_t>(assignmentCount));
			std::vector<SparseShadowAllocator::LightRequest> requests;
			requests.reserve(static_cast<std::size_t>(assignmentCount));
			for (std::uint32_t index = 0; index < assignmentCount; ++index) {
				requests.push_back({.lightIndex = index,
					.shadowSlot = shadowCandidates_[index].slot,
					.requestedResolution = shadowCandidates_[index].requestedResolution});
			}
			const auto allocationPlan =
				renderer_.sparseShadowAllocator_.BuildPlan(requests);
			std::unordered_map<std::uint64_t, Renderer::PointShadowHistory> nextHistory;
			for (const auto& allocation : allocationPlan) {
				const auto& candidate = shadowCandidates_[allocation.lightIndex];
				const std::uint32_t mipLevel = allocation.resolution == 1024
					? 0u : allocation.resolution == 512 ? 1u
					: allocation.resolution == 256 ? 2u : 3u;
				renderer_.pointShadowAssignments_.push_back({
					.sceneIndex = candidate.sceneIndex,
					.packedLightIndex = candidate.packedLightIndex,
					.localLightIndex = candidate.packedLightIndex - directionalLightCount_,
					.mipLevel = mipLevel,
					.slot = allocation.shadowSlot,
					.resolution = allocation.resolution,
					.firstPage = allocation.firstPage,
					.pageCount = allocation.pageCount,
					.projectedInfluence = candidate.projectedInfluence,
				});
				nextHistory.emplace(candidate.stableId, Renderer::PointShadowHistory{
					.slot = allocation.shadowSlot, .resolution = allocation.resolution});
			}
			renderer_.pointShadowHistory_ = std::move(nextHistory);
		}

		const auto uploadStart = std::chrono::steady_clock::now();
		static_assert(k_MaxLightSources < 0xffffu);
		const auto bins = BuildLightDepthBins(scene.camera.value_or(RenderCamera{}),
			std::span<const RenderLight>(packedLights_).subspan(directionalLightCount_));
		auto& binBuffer = depthBinBuffers_.at(renderer_.frame_.frameIndex);
		commands.UpdateBuffer({.buffer = binBuffer.uploadBuffer, .data = &bins, .size = sizeof(bins)});
		commands.CopyBuffer(binBuffer.uploadBuffer, binBuffer.gpuBuffer, {.size = sizeof(bins)});
		renderer_.UploadLights(commands, packedLights_, directionalLightCount_);
		std::array<std::uint32_t, k_MaxLightSources> zeroResolutions{};
		commands.Barrier({.buffer = shadowResolutionBuffers_[frameIndex],
			.oldState = resolutionReadbackReady_[frameIndex]
				? ResourceState::TransferSrc : ResourceState::Undefined,
			.newState = ResourceState::TransferDst,
			.sourceQueue = QueueType::Compute, .destinationQueue = QueueType::Compute});
		commands.UpdateBuffer({.buffer = shadowResolutionResetUploads_[frameIndex],
			.data = zeroResolutions.data(), .size = sizeof(zeroResolutions)});
		commands.CopyBuffer(shadowResolutionResetUploads_[frameIndex],
			shadowResolutionBuffers_[frameIndex],
			{.srcOffset = 0, .dstOffset = 0, .size = sizeof(zeroResolutions)});
		commands.Barrier({.buffer = shadowResolutionBuffers_[frameIndex],
			.oldState = ResourceState::TransferDst, .newState = ResourceState::ShaderWrite,
			.sourceQueue = QueueType::Compute, .destinationQueue = QueueType::Compute});
		resolutionSceneIndices_[frameIndex].clear();
		resolutionSceneIndices_[frameIndex].reserve(localLights_.size());
		for (const auto& local : localLights_) {
			const auto& light = scene.lights[local.sceneIndex];
			resolutionSceneIndices_[frameIndex].push_back(light.sourceId != 0
				? light.sourceId : static_cast<std::uint64_t>(local.sceneIndex + 1));
		}
		renderer_.cpuTimings_.uploadLightsMs += std::chrono::duration<float, std::milli>(
			std::chrono::steady_clock::now() - uploadStart).count();

		CullingData data;
		if (scene.camera) {
			const auto frame = renderer_.BuildFrameData(*scene.camera);
			data.view = frame.view;
			data.projection = frame.projection;
		}
		data.viewport = glm::uvec4(width_, height_, tilesX_, tilesY_);
		renderer_.UploadBuffer(commands, cullingBuffers_.at(renderer_.frame_.frameIndex),
			&data, sizeof(data), ResourceState::UniformBuffer, QueueType::Compute);
	}

	void Renderer::LightCullingPass::Render(
		Velos::RHI::ICommandList& commands, const RenderScene&)
	{
		if (tilesX_ == 0 || tilesY_ == 0) return;
		commands.BindComputePipeline(pipeline_);
		commands.SetComputeBindings(pipeline_, 0, sets_.at(renderer_.frame_.frameIndex));
		commands.Dispatch(tilesX_, tilesY_, 1);
		const auto frameIndex = renderer_.frame_.frameIndex;
		commands.Barrier({.buffer = shadowResolutionBuffers_[frameIndex],
			.oldState = ResourceState::ShaderWrite, .newState = ResourceState::TransferSrc,
			.sourceQueue = QueueType::Compute, .destinationQueue = QueueType::Compute});
		commands.CopyBuffer(shadowResolutionBuffers_[frameIndex],
			shadowResolutionReadbacks_[frameIndex],
			{.srcOffset = 0, .dstOffset = 0,
				.size = sizeof(std::uint32_t) * k_MaxLightSources});
		resolutionReadbackReady_[frameIndex] = true;
	}

	void Renderer::LightCullingPass::OnResize(
		Velos::RHI::IDevice&, std::uint32_t width, std::uint32_t height)
	{
		CreateResources(width, height);
	}

} // namespace Iryven
