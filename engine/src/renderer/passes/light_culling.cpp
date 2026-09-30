#include "light_culling.h"
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
			{.binding = 3, .type = BindingType::StorageBuffer, .visibility = ShaderStage::Compute},
		};
		layout_ = device.CreateBindingLayout({ .bindings = bindings, .bindingCount = 3,
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
			{BindingType::StorageBuffer, 2 * k_FramesInFlight},
		};
		pool_ = device.CreateBindingPool({ .poolSizes = sizes, .poolSizeCount = 2,
			.maxSets = k_FramesInFlight, .debugName = "Light tiling pool" });
		for (std::uint32_t frame = 0; frame < k_FramesInFlight; ++frame) {
			tileBuffers_[frame] = device.CreateBuffer({ .size = tileBufferSize_,
				.usage = BufferUsage::Storage, .memoryUsage = MemoryUsage::GPUOnly,
				.concurrentQueues = true,
				.debugName = "Light tile masks" });
			cullingBuffers_[frame] = renderer_.CreateUploadBackedBuffer(sizeof(CullingData),
				BufferUsage::Uniform, "Light tiling data", "Light tiling upload");
			depthBinBuffers_[frame] = renderer_.CreateUploadBackedBuffer(sizeof(LightDepthBins),
				BufferUsage::Storage, "Light depth bins", "Light depth bins upload", true);
			sets_[frame] = device.AllocateBindingSet({ .pool = pool_, .layout = layout_ });
			const BindingBufferInfo lights{ .buffer = renderer_.lightingFrames_[frame].lightBuffer.gpuBuffer,
				.range = sizeof(glm::uvec4) + sizeof(glm::vec4) * 4 * k_MaxLightSources };
			const BindingBufferInfo culling{ .buffer = cullingBuffers_[frame].gpuBuffer,
				.range = sizeof(CullingData) };
			const BindingBufferInfo tiles{ .buffer = tileBuffers_[frame], .range = tileBufferSize_ };
			device.UpdateBindingSet({ .dstSet = sets_[frame], .binding = 0,
				.type = BindingType::StorageBuffer, .bufferInfo = &lights });
			device.UpdateBindingSet({ .dstSet = sets_[frame], .binding = 1,
				.type = BindingType::UniformBuffer, .bufferInfo = &culling });
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
		tileBuffers_ = {};
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
		directionalLightCount_ = 0;

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
			for (const auto& light : localLights_)
				packedLights_.push_back(scene.lights[light.sceneIndex]);
		}

		const auto uploadStart = std::chrono::steady_clock::now();
		static_assert(k_MaxLightSources < 0xffffu);
		const auto bins = BuildLightDepthBins(scene.camera.value_or(RenderCamera{}),
			std::span<const RenderLight>(packedLights_).subspan(directionalLightCount_));
		auto& binBuffer = depthBinBuffers_.at(renderer_.frame_.frameIndex);
		commands.UpdateBuffer({.buffer = binBuffer.uploadBuffer, .data = &bins, .size = sizeof(bins)});
		commands.CopyBuffer(binBuffer.uploadBuffer, binBuffer.gpuBuffer, {.size = sizeof(bins)});
		renderer_.UploadLights(commands, packedLights_, directionalLightCount_);
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
	}

	void Renderer::LightCullingPass::OnResize(
		Velos::RHI::IDevice&, std::uint32_t width, std::uint32_t height)
	{
		CreateResources(width, height);
	}

} // namespace Iryven

