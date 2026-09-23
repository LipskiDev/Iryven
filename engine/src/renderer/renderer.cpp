#include "renderer.h"
#include <iryven/log.h>
#include "imgui_renderer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/vector_uint4.hpp>
#include <glm/trigonometric.hpp>

#include <rhi/pipeline.h>
#include <shader/shader_compiler.h>
#include <rhi/upload_context.h>

#include "passes/gbuffer.h"
#include "passes/shading.h"
#include "passes/hi_z.h"
#include "passes/cloth_compute.h"
#include "passes/forward.h"

namespace {

using CpuClock = std::chrono::steady_clock;

struct TextVertex {
	glm::vec2 position;
	glm::vec2 uv;
	glm::vec4 color;
};

std::vector<char32_t> DecodeUtf8(std::string_view text)
{
	std::vector<char32_t> result;
	result.reserve(text.size());
	for (std::size_t i = 0; i < text.size();) {
		const auto first = static_cast<std::uint8_t>(text[i]);
		char32_t codepoint = U'\uFFFD';
		std::size_t length = 1;
		char32_t minimum = 0;
		if (first < 0x80) {
			codepoint = first;
		}
		else if ((first & 0xE0) == 0xC0) {
			codepoint = first & 0x1F; length = 2; minimum = 0x80;
		}
		else if ((first & 0xF0) == 0xE0) {
			codepoint = first & 0x0F; length = 3; minimum = 0x800;
		}
		else if ((first & 0xF8) == 0xF0) {
			codepoint = first & 0x07; length = 4; minimum = 0x10000;
		}

		bool valid = i + length <= text.size();
		for (std::size_t offset = 1; valid && offset < length; ++offset) {
			const auto continuation = static_cast<std::uint8_t>(text[i + offset]);
			if ((continuation & 0xC0) != 0x80) {
				valid = false;
				break;
			}
			codepoint = (codepoint << 6) | (continuation & 0x3F);
		}
		if (!valid || codepoint < minimum || codepoint > 0x10FFFF ||
			(codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
			result.push_back(U'\uFFFD');
			++i;
			continue;
		}
		result.push_back(codepoint);
		i += length;
	}
	return result;
}

} // namespace

namespace Iryven {
	constexpr std::uint32_t k_MaxMaterials = 1024;
	constexpr std::uint32_t k_MaxBindlessTextures = 4096;

	struct alignas(16) GpuMaterial {
		glm::vec4 baseColorFactor{1.0f};
		glm::vec4 emissiveFactor{0.0f};
		// x = metallic, y = roughness, z = normal scale, w = occlusion strength.
		glm::vec4 metallicRoughnessNormal{0.0f, 1.0f, 1.0f, 1.0f};
		// x=base color, y=metallic-roughness, z=normal, w=occlusion.
		glm::uvec4 textureIndices0{0u};
		// x=emissive, y=texture-presence flags.
		glm::uvec4 textureIndices1{0u};
		glm::vec4 specularGlossiness{1.0f};
		// x=transmission; remaining lanes reserved.
		glm::vec4 transmission{0.0f};
		// x=specular-glossiness texture, y=transmission texture, z=workflow (1=SG).
		glm::uvec4 extensionTextures{0u};
	};
	static_assert(sizeof(GpuMaterial) == 128);
	static_assert(offsetof(GpuMaterial, specularGlossiness) == 80);
	static_assert(offsetof(GpuMaterial, transmission) == 96);
	static_assert(offsetof(GpuMaterial, extensionTextures) == 112);

	Renderer::Renderer(Window& window, AssetUploadQueue& assetUploads, bool enableValidation)
		: window_(window), assetUploads_(assetUploads)
	{
		device_.reset(Velos::RHI::CreateDevice({
			.graphicsAPI = Velos::RHI::GraphicsAPI::Vulkan,
			.enableValidation = enableValidation,
			.applicationName = window_.GetTitle().c_str(),
		}));

		if (!device_) {
			throw std::runtime_error("Failed to create rendering device");
		}

		const int width = window_.GetFramebufferWidth();
		const int height = window_.GetFramebufferHeight();
		if (width <= 0 || height <= 0) {
			throw std::runtime_error("Cannot create a swapchain for a zero-sized framebuffer");
		}

		swapchain_ = device_->CreateSwapchain({
			.windowHandle = window_.GetNativeHandle(),
			.width = static_cast<Velos::u32>(width),
			.height = static_cast<Velos::u32>(height),
			.bufferCount = 2,
			.vsync = window_.IsVSync(),
			.debugName = "Iryven main swapchain",
		});

		if (!swapchain_) {
			throw std::runtime_error("Failed to create rendering swapchain");
		}

		CreateDepthResources(
			static_cast<std::uint32_t>(width),
			static_cast<std::uint32_t>(height));
		CreateBindlessResources();
		hiZPass_ = std::make_unique<HiZPass>(*this);
		CreatePipelineResources();
		CreateBufferResources();

		frameGraphBuilder_.Init(*device_);
		frameGraph_.Init(frameGraphBuilder_);
		gbufferPass_ = std::make_unique<GBufferPass>(*this);
		clothCompute_ = std::make_unique<ClothCompute>(*this);
		forwardPass_ = std::make_unique<ForwardPass>(*this);
		shadingPass_ = std::make_unique<ShadingPass>(*this);
		frameGraphBuilder_.RegisterRenderPass("gbuffer", *gbufferPass_);
		frameGraphBuilder_.RegisterRenderPass("shading", *shadingPass_);
		frameGraphBuilder_.RegisterRenderPass("clothCompute", *clothCompute_);
		frameGraphBuilder_.RegisterRenderPass("forward", *forwardPass_);
		frameGraphBuilder_.RegisterRenderPass("hiZ", *hiZPass_);

        std::vector<FrameGraphResourceOutputCreation> gbufferOutputs;
        for (std::size_t i = 0; i < GBufferNames.size(); ++i) {
            gbufferOutputs.push_back({.type = FrameGraphResourceType::Attachment,
                .info = FrameGraphTextureInfo{
                    .width = static_cast<std::uint32_t>(width),
                    .height = static_cast<std::uint32_t>(height),
                    .format = GBufferFormats[i],
                    .usage = ImageUsage::ColorAttachment | ImageUsage::Sampled,
                    .loadOp = RenderPassOperation::Clear,
                    .resizeWithSwapchain = true}, .name = GBufferNames[i]});
        }
        gbufferOutputs.push_back({.type = FrameGraphResourceType::Attachment,
            .info = FrameGraphTextureInfo{
                .width = static_cast<std::uint32_t>(width),
                .height = static_cast<std::uint32_t>(height),
                .format = Format::D32_FLOAT,
                .usage = ImageUsage::DepthStencil | ImageUsage::Sampled,
                .loadOp = RenderPassOperation::Clear, .concurrentQueues = true},
            .external = true, .name = "depth"});
        std::vector<FrameGraphResourceInputCreation> gbufferInputs;
        if (clothComputePipeline_) gbufferInputs.push_back({
            .type = FrameGraphResourceType::Reference, .name = "clothSimulationComplete"});
        frameGraph_.AddNode({.name = "gbuffer", .inputs = std::move(gbufferInputs),
            .outputs = std::move(gbufferOutputs)});

        std::vector<FrameGraphResourceInputCreation> shadingInputs;
        for (const auto* name : GBufferNames) shadingInputs.push_back({
            .type = FrameGraphResourceType::Texture,
            .access = FrameGraphAccess::ShaderSampledRead, .name = name});
        shadingInputs.push_back({.type = FrameGraphResourceType::Texture,
            .access = FrameGraphAccess::ShaderSampledRead, .name = "depth"});
        const Color clearColor = Color::CornflowerBlue;
        frameGraph_.AddNode({.name = "shading", .inputs = std::move(shadingInputs),
            .outputs = {{.type = FrameGraphResourceType::Attachment,
                .info = FrameGraphTextureInfo{
                    .width = static_cast<std::uint32_t>(width),
                    .height = static_cast<std::uint32_t>(height),
                    .format = Format::BGRA8_UNORM, .usage = ImageUsage::ColorAttachment,
                    .loadOp = RenderPassOperation::Clear,
                    .clearColor = {clearColor.R(), clearColor.G(), clearColor.B(), clearColor.A()}},
                .external = true, .name = "backbuffer"}}});

		if (clothComputePipeline_) {
			frameGraph_.AddNode({
				.name = "clothCompute",
				.outputs = {
					{
						.type = FrameGraphResourceType::Reference,
						.name = "clothSimulationComplete",
					},
				},
				.queue = Velos::RHI::QueueType::Compute,
			});
		}
		std::vector<FrameGraphResourceInputCreation> forwardInputs;
		forwardInputs.push_back({
			.type = FrameGraphResourceType::Attachment,
			.access = FrameGraphAccess::ColorAttachmentReadWrite,
			.info = FrameGraphTextureInfo{
				.loadOp = RenderPassOperation::Load,
			},
			.name = "backbuffer",
		});
		forwardInputs.push_back({
			.type = FrameGraphResourceType::Attachment,
			.access = FrameGraphAccess::DepthStencilReadWrite,
			.info = FrameGraphTextureInfo{
				.loadOp = RenderPassOperation::Load,
			},
			.name = "depth",
		});
		frameGraph_.AddNode({
			.name = "forward",
			.inputs = std::move(forwardInputs),
			.outputs = {{.type = FrameGraphResourceType::Reference, .name = "opaqueDepthComplete"}},
		});
		frameGraph_.AddNode({
			.name = "hiZ",
			.inputs = {
				{.type = FrameGraphResourceType::Reference, .name = "opaqueDepthComplete"},
				{.type = FrameGraphResourceType::Texture,
				 .access = FrameGraphAccess::ShaderSampledRead, .name = "depth"},
			},
			// Depth dependencies synchronize graphics -> compute and the next
			// frame's compute -> graphics history reads through graph timelines.
			.queue = Velos::RHI::QueueType::Compute,
		});
		frameGraph_.Compile();
	}

	Renderer::~Renderer()
	{
		if (!device_) {
			return;
		}

		device_->WaitIdle();
		frameGraph_.Shutdown();
		frameGraphBuilder_.Shutdown();
		forwardPass_.reset();
		clothCompute_.reset();
		gbufferPass_.reset();
		shadingPass_.reset();
		for (auto& [id, cloth] : cloths_) DestroyGpuCloth(cloth);
		cloths_.clear();
		for (auto& retired : retiredCloths_) DestroyGpuCloth(retired.resources);
		retiredCloths_.clear();
		DestroyPipelineResources();
		hiZPass_.reset();
		DestroyBindlessResources();
		DestroyMeshResources();
		DestroyFontResources();
		DestroyBufferResources();
		DestroyDepthResources();

		if (swapchain_) {
			device_->DestroySwapchain(swapchain_);
		}
	}

	void Renderer::DrawScene(const RenderScene& renderScene)
	{
		if (!frameActive_) {
			throw std::logic_error("Renderer::DrawScene called outside an active frame");
		}

		cpuTimings_ = {};
		PrepareCloths(renderScene);
		frameGraph_.Render(renderScene);
		cpuTimings_.frameGraph = frameGraph_.GetCpuTimings();
	}

	bool Renderer::BeginFrame()
	{
		const int width = window_.GetFramebufferWidth();
		const int height = window_.GetFramebufferHeight();

		if (width <= 0 || height <= 0) {
			return false;
		}

		const auto dimensions = device_->GetSwapchainDimensions();
		if (swapchainDirty_ || dimensions.width != static_cast<Velos::u32>(width) ||
			dimensions.height != static_cast<Velos::u32>(height)) {
			device_->ResizeSwapchain(
				swapchain_,
				static_cast<Velos::u32>(width),
				static_cast<Velos::u32>(height));
			DestroyDepthResources();
			CreateDepthResources(
				static_cast<std::uint32_t>(width),
				static_cast<std::uint32_t>(height));
			frameGraph_.OnResize(*device_, static_cast<std::uint32_t>(width),
				static_cast<std::uint32_t>(height));
			swapchainDirty_ = false;
		}

		frame_ = device_->BeginFrame(swapchain_);
		if (!frame_.success) {
			swapchainDirty_ = true;
			return false;
		}

		completedSubmissionSerial_ = std::max(
			completedSubmissionSerial_,
			frameSubmissionSerials_.at(frame_.frameIndex));
		bindlessTextureManager_->CollectGarbage(completedSubmissionSerial_);
		CollectRetiredModels(completedSubmissionSerial_);
		CollectRetiredCloths(completedSubmissionSerial_);
		CollectUnusedMeshes();
		CollectUnusedFonts();

		auto& retiredTextBuffers = textVertexBuffers_.at(frame_.frameIndex);
		for (auto& buffer : retiredTextBuffers) {
			DestroyUploadBackedBuffer(buffer);
		}
		retiredTextBuffers.clear();

		auto& commands = device_->GetCommandList();
		commands.Begin();
		frameGraph_.BeginFrame();

		const auto graphDimensions = device_->GetSwapchainDimensions();
		auto& backbufferInfo = std::get<FrameGraphTextureInfo>(
			frameGraph_.GetResource("backbuffer")->info);
		backbufferInfo.width = graphDimensions.width;
		backbufferInfo.height = graphDimensions.height;
		backbufferInfo.handle = frame_.backbufferImage;
		backbufferInfo.view = frame_.backbuffer;

		auto& depthInfo = std::get<FrameGraphTextureInfo>(
			frameGraph_.GetResource("depth")->info);
		depthInfo.width = graphDimensions.width;
		depthInfo.height = graphDimensions.height;
		depthInfo.handle = depthImage_;
		depthInfo.view = depthView_;

		frameActive_ = true;
		return true;
	}

	void Renderer::DrawObject(
		Velos::RHI::ICommandList& commands,
		const RenderObject& object,
		const FrameData& frameData, std::uint32_t transmissionPhase)
	{
		if (!frameActive_) {
			throw std::logic_error("Renderer::DrawObject called outside an active frame");
		}

		GpuMesh* mesh = object.mesh ? ResolveOrCreateMesh(object.mesh) : nullptr;
		GpuModel* model = object.model ? ResolveOrCreateModel(object.model) : nullptr;
		if (!mesh && !model) return;
		const auto meshPipeline = transmissionPhase == 0 ? meshletPipeline_
			: meshletTransmissionPipelines_.at(transmissionPhase - 1);
		const auto indexedPipeline = transmissionPhase == 0 ? gltfPipeline_
			: gltfTransmissionPipelines_.at(transmissionPhase - 1);

		struct MeshDrawConstants {
			glm::mat4 model;
			glm::uvec4 meshletInfo{0u};
		};
		static_assert(sizeof(MeshDrawConstants) == 80);
		const auto drawMeshlets = [&](BindingSetHandle bindings,
			std::uint32_t meshletOffset, std::uint32_t meshletCount) {

			const MaterialSlotKey materialKey{
				.model = object.model.get(),
				.material = object.material.get()
			};
			const auto materialSlot = object.material
				? materialSlots_.find(materialKey) : materialSlots_.end();

			uint32_t slot = materialSlot == materialSlots_.end() ? 0u : materialSlot->second;

			const MeshDrawConstants constants{
				.model = object.transform,
				.meshletInfo = glm::uvec4(meshletOffset, slot, 0u, transmissionPhase),
			};

			commands.BindPipeline(meshPipeline);
			commands.SetBindings(meshPipeline, 0,
				lightingFrames_.at(frame_.frameIndex).lightBindingSet);
			commands.SetBindings(meshPipeline, 1,
				bindlessTextureManager_->BindingSet());
			commands.SetBindings(meshPipeline, 2, bindings);
			commands.SetBindings(meshPipeline, 3, hiZPass_->SamplingSet());
			commands.PushConstants(
				Velos::RHI::ShaderStage::Mesh | Velos::RHI::ShaderStage::Fragment | Velos::RHI::ShaderStage::Task, 0,
				static_cast<Velos::u32>(sizeof(constants)), &constants);
			commands.DrawMeshTasks(meshletCount);
		};

		if (mesh && mesh->meshletCount > 0) {
			drawMeshlets(mesh->meshletBindingSet, 0, mesh->meshletCount);
			return;
		}
		if (model && model->meshletBindingSet) {
			const auto primitive = std::ranges::find_if(
				model->primitiveMeshlets,
				[&object](const GpuModel::PrimitiveMeshlets& candidate) {
					return candidate.firstIndex == object.firstIndex &&
						candidate.indexCount == object.indexCount &&
						candidate.vertexOffset == object.vertexOffset;
				});
			if (primitive != model->primitiveMeshlets.end() &&
				primitive->meshletCount > 0) {
				drawMeshlets(model->meshletBindingSet,
					primitive->meshletOffset, primitive->meshletCount);
				return;
			}
		}

		struct DrawConstants {
			glm::mat4 model;
			std::uint32_t materialIndex;
			glm::uvec3 padding{0u};
		};
		static_assert(sizeof(DrawConstants) == 80);
		const MaterialSlotKey materialKey{
			.model = object.model.get(),
			.material = object.material.get()
		};
		const auto materialSlot = object.material
			? materialSlots_.find(materialKey) : materialSlots_.end();
		const DrawConstants drawConstants{
			.model = object.transform,
			.materialIndex = materialSlot == materialSlots_.end() ? 0u : materialSlot->second,
			.padding = glm::uvec3(0u, 0u, transmissionPhase),
		};

		commands.BindPipeline(indexedPipeline);
		commands.SetBindings(
			indexedPipeline, 0,
			lightingFrames_.at(frame_.frameIndex).lightBindingSet);
		commands.SetBindings(
			indexedPipeline, 1, bindlessTextureManager_->BindingSet());
		commands.PushConstants(
			Velos::RHI::ShaderStage::Vertex | Velos::RHI::ShaderStage::Fragment,
			0,
			static_cast<Velos::u32>(sizeof(drawConstants)),
			&drawConstants);
		commands.BindVertexBuffer(0, mesh ? mesh->vertexBuffer : model->vertexBuffer);
		commands.BindIndexBuffer(mesh ? mesh->indexBuffer : model->indexBuffer, Velos::RHI::IndexType::U32);
		if (model) commands.DrawIndexed(object.indexCount, object.firstIndex, object.vertexOffset);
		else commands.DrawIndexed(mesh->indexCount);
	}

	Renderer::GpuCloth* Renderer::ResolveOrCreateCloth(
		const RenderCloth& cloth)
	{
		if (cloth.id == 0 || cloth.resolution.x < 2 || cloth.resolution.y < 2) {
			return nullptr;
		}
		const std::uint64_t particleCount64 =
			static_cast<std::uint64_t>(cloth.resolution.x) * cloth.resolution.y;
		const std::uint64_t indexCount64 =
			static_cast<std::uint64_t>(cloth.resolution.x - 1) *
			(cloth.resolution.y - 1) * 6;
		if (particleCount64 > std::numeric_limits<std::uint32_t>::max() ||
			indexCount64 > std::numeric_limits<std::uint32_t>::max()) {
			throw std::runtime_error("Cloth grid exceeds 32-bit draw limits");
		}

		if (auto existing = cloths_.find(cloth.id); existing != cloths_.end()) {
			const GpuCloth& gpu = existing->second;
			const bool unchanged = gpu.resolution == cloth.resolution &&
				gpu.size.x == cloth.size.x && gpu.size.y == cloth.size.y &&
				gpu.mass == cloth.mass && gpu.pinTopLeft == cloth.pinTopLeft &&
				gpu.pinTopRight == cloth.pinTopRight;
			if (unchanged) return &existing->second;

			retiredCloths_.push_back({
				.resources = std::move(existing->second),
				.retirementSubmission = lastSubmittedSerial_,
			});
			cloths_.erase(existing);
		}

		const std::uint32_t particleCount =
			static_cast<std::uint32_t>(particleCount64);
		const std::uint32_t indexCount =
			static_cast<std::uint32_t>(indexCount64);
		std::vector<ClothParticle> particles(particleCount);
		const float inverseMass = cloth.mass > 0.0f ? 1.0f / cloth.mass : 0.0f;
		for (std::uint32_t y = 0; y < cloth.resolution.y; ++y) {
			for (std::uint32_t x = 0; x < cloth.resolution.x; ++x) {
				const std::uint32_t index = y * cloth.resolution.x + x;
				const float u = static_cast<float>(x) /
					static_cast<float>(cloth.resolution.x - 1);
				const float v = static_cast<float>(y) /
					static_cast<float>(cloth.resolution.y - 1);
				const bool pinned = y == 0 &&
					((x == 0 && cloth.pinTopLeft) ||
					 (x == cloth.resolution.x - 1 && cloth.pinTopRight));
				const glm::vec3 position{
					(u - 0.5f) * cloth.size.x,
					(0.5f - v) * cloth.size.y,
					0.0f,
				};
				particles[index] = {
					.positionAndInverseMass = glm::vec4(
						position, pinned ? 0.0f : inverseMass),
					.previousPosition = glm::vec4(position, 0.0f),
					.normal = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
				};
			}
		}

		std::vector<std::uint32_t> indices;
		indices.reserve(indexCount);
		for (std::uint32_t y = 0; y + 1 < cloth.resolution.y; ++y) {
			for (std::uint32_t x = 0; x + 1 < cloth.resolution.x; ++x) {
				const std::uint32_t topLeft = y * cloth.resolution.x + x;
				const std::uint32_t topRight = topLeft + 1;
				const std::uint32_t bottomLeft = topLeft + cloth.resolution.x;
				const std::uint32_t bottomRight = bottomLeft + 1;
				indices.insert(indices.end(), {
					topLeft, bottomLeft, topRight,
					topRight, bottomLeft, bottomRight,
				});
			}
		}

		GpuCloth gpu{
			.resolution = cloth.resolution,
			.size = cloth.size,
			.mass = cloth.mass,
			.pinTopLeft = cloth.pinTopLeft,
			.pinTopRight = cloth.pinTopRight,
			.particleCount = particleCount,
			.indexCount = indexCount,
		};
		const std::uint64_t particleBytes = particles.size() * sizeof(ClothParticle);
		const std::uint64_t indexBytes = indices.size() * sizeof(std::uint32_t);
		try {
			for (BufferHandle& particleBuffer : gpu.particleBuffers) {
				particleBuffer = device_->CreateBuffer({
					.size = particleBytes,
					.usage = BufferUsage::Storage | BufferUsage::TransferDst,
					.memoryUsage = MemoryUsage::GPUOnly,
					.concurrentQueues = true,
					.debugName = "Iryven cloth particle buffer",
				});
			}
			gpu.indexBuffer = device_->CreateBuffer({
				.size = indexBytes,
				.usage = BufferUsage::Index | BufferUsage::TransferDst,
				.memoryUsage = MemoryUsage::GPUOnly,
				.debugName = "Iryven cloth index buffer",
			});

			auto upload = device_->CreateUploadContext(
				particleBytes * 2 + indexBytes + 32);
			upload->Begin();
			for (BufferHandle particleBuffer : gpu.particleBuffers) {
				upload->UploadBuffer({
					.dstBuffer = particleBuffer,
					.size = particleBytes,
					.data = particles.data(),
					.finalState = ResourceState::ShaderRead,
				});
			}
			upload->UploadBuffer({
				.dstBuffer = gpu.indexBuffer,
				.size = indexBytes,
				.data = indices.data(),
				.finalState = ResourceState::IndexBuffer,
			});
			upload->Flush();
			device_->AcquireUploadedBuffers(upload->TakePendingBufferAcquires());

			const BindingPoolSize poolSize{
				.type = BindingType::StorageBuffer,
				.count = clothComputePipeline_ ? 6u : 2u,
			};
			gpu.bindingPool = device_->CreateBindingPool({
				.poolSizes = &poolSize,
				.poolSizeCount = 1,
				.maxSets = clothComputePipeline_ ? 4u : 2u,
				.debugName = "Iryven cloth binding pool",
			});

			for (std::uint32_t index = 0; index < 2; ++index) {
				gpu.renderBindings[index] = device_->AllocateBindingSet({
					.pool = gpu.bindingPool,
					.layout = clothRenderBindingLayout_,
					.debugName = "Iryven cloth render binding set",
				});
				const BindingBufferInfo renderInfo{
					.buffer = gpu.particleBuffers[index],
					.range = particleBytes,
				};
				device_->UpdateBindingSet({
					.dstSet = gpu.renderBindings[index],
					.binding = 0,
					.type = BindingType::StorageBuffer,
					.bufferInfo = &renderInfo,
				});

				if (clothComputePipeline_) {
					gpu.simulationBindings[index] = device_->AllocateBindingSet({
						.pool = gpu.bindingPool,
						.layout = clothSimulationBindingLayout_,
						.debugName = "Iryven cloth simulation binding set",
					});
					const BindingBufferInfo inputInfo{
						.buffer = gpu.particleBuffers[index],
						.range = particleBytes,
					};
					const BindingBufferInfo outputInfo{
						.buffer = gpu.particleBuffers[1u - index],
						.range = particleBytes,
					};
					device_->UpdateBindingSet({
						.dstSet = gpu.simulationBindings[index],
						.binding = 0,
						.type = BindingType::StorageBuffer,
						.bufferInfo = &inputInfo,
					});
					device_->UpdateBindingSet({
						.dstSet = gpu.simulationBindings[index],
						.binding = 1,
						.type = BindingType::StorageBuffer,
						.bufferInfo = &outputInfo,
					});
				}
			}
		} catch (...) {
			DestroyGpuCloth(gpu);
			throw;
		}

		auto [entry, inserted] = cloths_.emplace(cloth.id, std::move(gpu));
		(void)inserted;
		return &entry->second;
	}

	void Renderer::PrepareCloths(const RenderScene& scene)
	{
		++clothSceneGeneration_;
		for (const RenderCloth& cloth : scene.cloths) {
			if (GpuCloth* gpu = ResolveOrCreateCloth(cloth)) {
				gpu->lastSeenGeneration = clothSceneGeneration_;
			}
		}

		for (auto it = cloths_.begin(); it != cloths_.end();) {
			if (it->second.lastSeenGeneration == clothSceneGeneration_) {
				++it;
				continue;
			}
			retiredCloths_.push_back({
				.resources = std::move(it->second),
				.retirementSubmission = lastSubmittedSerial_,
			});
			it = cloths_.erase(it);
		}
		CollectRetiredCloths(completedSubmissionSerial_);
	}

	void Renderer::SimulateCloths(
		ICommandList& commands, const RenderScene& scene)
	{
		if (!clothComputePipeline_ || scene.deltaTime <= 0.0f) return;

		const float deltaTime = std::clamp(scene.deltaTime, 0.0f, 1.0f / 30.0f);
		static float time = 0.0f;
		time += deltaTime;
		commands.BindComputePipeline(clothComputePipeline_);
		for (const RenderCloth& cloth : scene.cloths) {
			const auto found = cloths_.find(cloth.id);
			if (found == cloths_.end()) continue;
			GpuCloth& gpu = found->second;
			const std::uint32_t outputState = 1u - gpu.currentState;
			commands.Barrier({
				.buffer = gpu.particleBuffers[outputState],
				.oldState = ResourceState::ShaderRead,
				.newState = ResourceState::ShaderReadWrite,
				.sourceQueue = QueueType::Compute,
				.destinationQueue = QueueType::Compute,
			});
			commands.SetComputeBindings(
				clothComputePipeline_, 0,
				gpu.simulationBindings[gpu.currentState]);

			ClothSimulationConstants constants{
				.resolutionX = gpu.resolution.x,
				.resolutionY = gpu.resolution.y,
				.clothWidth = gpu.size.x,
				.clothHeight = gpu.size.y,
				.deltaTime = deltaTime,
				.stiffness = cloth.stiffness,
				.damping = cloth.damping,
				.gravityScale = cloth.gravityScale,
				.phase = static_cast<std::uint32_t>(ClothSimulationPhase::Integrate),
				.solverIterations = cloth.solverIterations,
				.particleCount = gpu.particleCount,
				.time = time,
			};
			const std::uint32_t groupCount = (gpu.particleCount + 63u) / 64u;
			commands.PushConstants(
				ShaderStage::Compute, 0, sizeof(constants), &constants);
			commands.Dispatch(groupCount, 1, 1);

			for (std::uint32_t iteration = 0;
				 iteration < cloth.solverIterations; ++iteration) {
				commands.Barrier({
					.buffer = gpu.particleBuffers[outputState],
					.oldState = ResourceState::ShaderReadWrite,
					.newState = ResourceState::ShaderReadWrite,
					.sourceQueue = QueueType::Compute,
					.destinationQueue = QueueType::Compute,
				});
				constants.phase = static_cast<std::uint32_t>(
					ClothSimulationPhase::SolveConstraints);
				constants.iteration = iteration;
				commands.PushConstants(
					ShaderStage::Compute, 0, sizeof(constants), &constants);
				commands.Dispatch(groupCount, 1, 1);
			}

			commands.Barrier({
				.buffer = gpu.particleBuffers[outputState],
				.oldState = ResourceState::ShaderReadWrite,
				.newState = ResourceState::ShaderReadWrite,
				.sourceQueue = QueueType::Compute,
				.destinationQueue = QueueType::Compute,
			});
			constants.phase = static_cast<std::uint32_t>(
				ClothSimulationPhase::RecalculateNormals);
			commands.PushConstants(
				ShaderStage::Compute, 0, sizeof(constants), &constants);
			commands.Dispatch(groupCount, 1, 1);

			const bool aliasesGraphics = device_->GetQueueRelationship(
				QueueType::Compute, QueueType::Graphics) ==
				QueueRelationship::SameQueue;
			commands.Barrier({
				.buffer = gpu.particleBuffers[outputState],
				.oldState = ResourceState::ShaderReadWrite,
				.newState = ResourceState::ShaderRead,
				.sourceQueue = QueueType::Compute,
				.destinationQueue = aliasesGraphics
					? QueueType::Graphics : QueueType::Compute,
			});
			gpu.currentState = outputState;
		}
	}

	void Renderer::DrawCloths(
		ICommandList& commands, const RenderScene& scene)
	{
		if (!scene.camera || scene.cloths.empty()) return;

		struct DrawConstants {
			glm::mat4 model;
			std::uint32_t materialIndex;
			std::uint32_t resolutionX;
			std::uint32_t resolutionY;
			std::uint32_t padding = 0;
		};
		static_assert(sizeof(DrawConstants) == 80);

		commands.BindPipeline(clothGraphicsPipeline_);
		commands.SetBindings(
			clothGraphicsPipeline_, 0,
			lightingFrames_.at(frame_.frameIndex).lightBindingSet);
		commands.SetBindings(
			clothGraphicsPipeline_, 1, bindlessTextureManager_->BindingSet());
		for (const RenderCloth& cloth : scene.cloths) {
			const auto found = cloths_.find(cloth.id);
			if (found == cloths_.end()) continue;
			const GpuCloth& gpu = found->second;
			const MaterialSlotKey materialKey{
				.model = nullptr,
				.material = cloth.material.get(),
			};
			const auto materialSlot = cloth.material
				? materialSlots_.find(materialKey) : materialSlots_.end();
			const DrawConstants constants{
				.model = cloth.transform,
				.materialIndex = materialSlot == materialSlots_.end()
					? 0u : materialSlot->second,
				.resolutionX = gpu.resolution.x,
				.resolutionY = gpu.resolution.y,
			};
			commands.SetBindings(
				clothGraphicsPipeline_, 2, gpu.renderBindings[gpu.currentState]);
			commands.PushConstants(
				ShaderStage::Vertex | ShaderStage::Fragment,
				0, sizeof(constants), &constants);
			commands.BindIndexBuffer(gpu.indexBuffer, IndexType::U32);
			commands.DrawIndexed(gpu.indexCount);
		}
	}

	Renderer::UploadBackedBuffer Renderer::CreateUploadBackedBuffer(
		std::uint64_t size,
		Velos::RHI::BufferUsage usage,
		const char* gpuDebugName,
		const char* uploadDebugName)
	{
		UploadBackedBuffer buffer;
		buffer.gpuBuffer = device_->CreateBuffer({
			.size = size,
			.usage = usage | Velos::RHI::BufferUsage::TransferDst,
			.memoryUsage = Velos::RHI::MemoryUsage::GPUOnly,
			.debugName = gpuDebugName,
		});
		try {
			buffer.uploadBuffer = device_->CreateBuffer({
				.size = size,
				.usage = Velos::RHI::BufferUsage::TransferSrc,
				.memoryUsage = Velos::RHI::MemoryUsage::CPUToGPU,
				.debugName = uploadDebugName,
			});
		} catch (...) {
			device_->DestroyBuffer(buffer.gpuBuffer);
			throw;
		}
		return buffer;
	}

	void Renderer::DestroyUploadBackedBuffer(UploadBackedBuffer& buffer)
	{
		if (buffer.uploadBuffer.IsValid()) {
			device_->DestroyBuffer(buffer.uploadBuffer);
		}
		if (buffer.gpuBuffer.IsValid()) {
			device_->DestroyBuffer(buffer.gpuBuffer);
		}
		buffer = {};
	}

	void Renderer::UploadBuffer(
		Velos::RHI::ICommandList& commands,
		UploadBackedBuffer& buffer,
		const void* data,
		std::uint64_t size,
		Velos::RHI::ResourceState finalState)
	{
		commands.UpdateBuffer({
			.buffer = buffer.uploadBuffer,
			.offset = 0,
			.data = data,
			.size = size,
		});
		commands.Barrier({
			.buffer = buffer.gpuBuffer,
			.oldState = buffer.state,
			.newState = Velos::RHI::ResourceState::TransferDst,
		});
		commands.CopyBuffer(
			buffer.uploadBuffer,
			buffer.gpuBuffer,
			{ .srcOffset = 0, .dstOffset = 0, .size = size });
		commands.Barrier({
			.buffer = buffer.gpuBuffer,
			.oldState = Velos::RHI::ResourceState::TransferDst,
			.newState = finalState,
		});
		buffer.state = finalState;
	}

	void Renderer::DrawText(
		Velos::RHI::ICommandList& commands, const RenderText& text)
	{
		if (!frameActive_ || text.text.empty() || text.fontSize <= 0.0f) {
			return;
		}
		GpuFont* gpuFont = ResolveOrCreateFont(text.font);
		if (!gpuFont) {
			return;
		}

		const auto dimensions = device_->GetSwapchainDimensions();
		if (dimensions.width == 0 || dimensions.height == 0) {
			return;
		}

		const float inverseAtlasWidth = 1.0f / text.font->GetAtlasWidth();
		const float inverseAtlasHeight = 1.0f / text.font->GetAtlasHeight();
		const float inverseScreenWidth = 1.0f / dimensions.width;
		const float inverseScreenHeight = 1.0f / dimensions.height;
		const glm::vec4 color = text.color.Vector();
		glm::vec2 pen = text.position;
		std::vector<TextVertex> vertices;
		vertices.reserve(text.text.size() * 6);

		for (const char32_t codepoint : DecodeUtf8(text.text)) {
			if (codepoint == U'\r') {
				continue;
			}
			if (codepoint == U'\n') {
				pen.x = text.position.x;
				pen.y += static_cast<float>(text.font->GetMetrics().lineHeight) * text.fontSize;
				continue;
			}

			const Glyph* glyph = text.font->FindGlyph(codepoint);
			if (!glyph) {
				glyph = text.font->FindGlyph(U'?');
			}
			if (!glyph) {
				continue;
			}

			const float left = pen.x + static_cast<float>(glyph->planeBounds.x) * text.fontSize;
			const float bottom = pen.y - static_cast<float>(glyph->planeBounds.y) * text.fontSize;
			const float right = pen.x + static_cast<float>(glyph->planeBounds.z) * text.fontSize;
			const float top = pen.y - static_cast<float>(glyph->planeBounds.w) * text.fontSize;
			const float x0 = left * inverseScreenWidth * 2.0f - 1.0f;
			const float x1 = right * inverseScreenWidth * 2.0f - 1.0f;
			const float y0 = top * inverseScreenHeight * 2.0f - 1.0f;
			const float y1 = bottom * inverseScreenHeight * 2.0f - 1.0f;
			const float u0 = static_cast<float>(glyph->atlasBounds.x) * inverseAtlasWidth;
			const float v0 = static_cast<float>(glyph->atlasBounds.y) * inverseAtlasHeight;
			const float u1 = static_cast<float>(glyph->atlasBounds.z) * inverseAtlasWidth;
			const float v1 = static_cast<float>(glyph->atlasBounds.w) * inverseAtlasHeight;

			if (left != right && top != bottom) {
				vertices.insert(vertices.end(), {
					{{x0, y0}, {u0, v1}, color},
					{{x0, y1}, {u0, v0}, color},
					{{x1, y1}, {u1, v0}, color},
					{{x0, y0}, {u0, v1}, color},
					{{x1, y1}, {u1, v0}, color},
					{{x1, y0}, {u1, v1}, color}
				});
			}
			pen.x += static_cast<float>(glyph->advance) * text.fontSize;
		}

		if (vertices.empty()) {
			return;
		}
		const std::uint64_t vertexBufferSize = vertices.size() * sizeof(TextVertex);
		auto vertexBuffer = CreateUploadBackedBuffer(
			vertexBufferSize,
			Velos::RHI::BufferUsage::Vertex,
			"Iryven text vertex buffer",
			"Iryven text vertex upload buffer");
		UploadBuffer(
			commands,
			vertexBuffer,
			vertices.data(),
			vertexBufferSize,
			Velos::RHI::ResourceState::VertexBuffer);
		const Velos::RHI::BufferHandle gpuVertexBuffer = vertexBuffer.gpuBuffer;
		textVertexBuffers_.at(frame_.frameIndex).push_back(std::move(vertexBuffer));

		commands.BindPipeline(textPipeline_);
		commands.SetBindings(textPipeline_, 0, gpuFont->bindingSet);
		commands.BindVertexBuffer(0, gpuVertexBuffer);
		commands.Draw(static_cast<Velos::u32>(vertices.size()));
	}

	void Renderer::UploadLights(
		Velos::RHI::ICommandList& commands,
		const std::vector<RenderLight>& lights)
	{
		struct alignas(16) GpuLight {
			glm::vec4 positionAndType;
			glm::vec4 directionAndRange;
			glm::vec4 colorAndIntensity;
			glm::vec4 spotAngles;
		};
		struct alignas(16) GpuLights {
			glm::uvec4 metadata{ 0u };
			std::array<GpuLight, k_MaxLightSources> lights{};
		};
		static_assert(sizeof(GpuLight) == 64);

		GpuLights gpuLights;
		const std::size_t lightCount = std::min<std::size_t>(lights.size(), k_MaxLightSources);
		gpuLights.metadata.x = static_cast<std::uint32_t>(lightCount);
		for (std::size_t i = 0; i < lightCount; ++i) {
			const RenderLight& source = lights[i];
			gpuLights.lights[i] = GpuLight{
				.positionAndType = glm::vec4(source.position, static_cast<float>(source.type)),
				.directionAndRange = glm::vec4(glm::normalize(source.direction), source.range),
				.colorAndIntensity = glm::vec4(glm::vec3(source.color.Vector()), source.intensity),
				.spotAngles = glm::vec4(
					glm::cos(glm::radians(source.innerConeAngle)),
					glm::cos(glm::radians(source.outerConeAngle)), 0.0f, 0.0f)
			};
		}

		UploadBuffer(
			commands,
			lightingFrames_.at(frame_.frameIndex).lightBuffer,
			&gpuLights,
			sizeof(gpuLights),
			Velos::RHI::ResourceState::UniformBuffer);
	}

	void Renderer::ToggleCullingCameraFreeze()
	{
		cullingCameraFrozen_ = !cullingCameraFrozen_;
		IRYVEN_CORE_INFO("Culling camera {} (P to toggle)",
			cullingCameraFrozen_ ? "frozen" : "following view");
	}

	void Renderer::UploadFrameData(
		Velos::RHI::ICommandList& commands, const FrameData& frameData)
	{
		// Retain the last displayed camera when P is pressed. Rendering always
		// uses the live frame, including after moving or resizing the viewport.
		if (!cullingCameraFrozen_ || !cullingCameraValid_) {
			cullingFrameData_ = frameData;
			cullingCameraValid_ = true;
		}
		const GpuFrameData gpuFrameData{
			.view = frameData.view,
			.projection = frameData.projection,
			.viewProjection = frameData.viewProjection,
			.cameraPosition = glm::vec4(frameData.cameraPosition, 1.0f),
			.cullingView = cullingFrameData_.view,
			.cullingProjection = cullingFrameData_.projection,
			.cullingViewProjection = cullingFrameData_.viewProjection,
			.cullingCameraPosition = glm::vec4(cullingFrameData_.cameraPosition, 1.0f)
		};
		UploadBuffer(
			commands,
			lightingFrames_.at(frame_.frameIndex).frameDataBuffer,
			&gpuFrameData,
			sizeof(gpuFrameData),
			Velos::RHI::ResourceState::UniformBuffer);
	}

	void Renderer::UploadMaterials(
		Velos::RHI::ICommandList& commands,
		const RenderScene& scene)
	{
		materialSlots_.clear();
		std::vector<GpuMaterial> materials;
		materials.reserve(std::min<std::size_t>(
			scene.objects.size() + scene.cloths.size() + 1, k_MaxMaterials));
		materials.emplace_back(); // Slot 0 is the default white material.

		const auto appendMaterial = [this, &materials](
			const ModelHandle& model, const MaterialHandle& material) {
			if (!material) return;
			const MaterialSlotKey key{
				.model = model.get(),
				.material = material.get()
			};
			if (materialSlots_.contains(key)) return;
			if (materials.size() >= k_MaxMaterials)
				throw std::runtime_error("Renderer material table exceeded its 1024 material capacity");

			GpuModel* gpuModel = model ? ResolveOrCreateModel(model) : nullptr;
			const auto resolveTextureIndex = [&](std::uint32_t localIndex) {
				if (!gpuModel || localIndex == InvalidTextureIndex ||
					localIndex >= gpuModel->bindlessTextureIndices.size()) {
					return missingTextureIndex_;
				}
				return gpuModel->bindlessTextureIndices[localIndex];
			};

			const std::uint32_t slot = static_cast<std::uint32_t>(materials.size());
			materialSlots_.emplace(key, slot);
			std::uint32_t textureFlags = 0;
			// Texture indices are local to a model's texture registry. Standalone
			// cloth materials therefore use their scalar factors only.
			if (gpuModel) {
				if (material->baseColorTexture != InvalidTextureIndex) textureFlags |= 1u;
				if (material->metallicRoughnessTexture != InvalidTextureIndex) textureFlags |= 2u;
				if (material->normalTexture != InvalidTextureIndex) textureFlags |= 4u;
				if (material->occlusionTexture != InvalidTextureIndex) textureFlags |= 8u;
				if (material->emissiveTexture != InvalidTextureIndex) textureFlags |= 16u;
				if (material->specularGlossinessTexture != InvalidTextureIndex) textureFlags |= 32u;
				if (material->transmissionTexture != InvalidTextureIndex) textureFlags |= 64u;
			}
			materials.push_back(GpuMaterial{
				.baseColorFactor = material->baseColor.Vector(),
				.emissiveFactor = material->emissive.Vector(),
				.metallicRoughnessNormal = glm::vec4(
					material->metallic,
					material->roughness,
					material->normalScale,
					material->occlusionStrength),
				.textureIndices0 = glm::uvec4(
					resolveTextureIndex(material->baseColorTexture),
					resolveTextureIndex(material->metallicRoughnessTexture),
					resolveTextureIndex(material->normalTexture),
					resolveTextureIndex(material->occlusionTexture)),
				.textureIndices1 = glm::uvec4(
					resolveTextureIndex(material->emissiveTexture),
					textureFlags, 0u, 0u),
				.specularGlossiness = glm::vec4(glm::vec3(material->specular.Vector()), material->glossiness),
				.transmission = glm::vec4(material->transmission, 0.0f, 0.0f, 0.0f),
				.extensionTextures = glm::uvec4(
					resolveTextureIndex(material->specularGlossinessTexture),
					resolveTextureIndex(material->transmissionTexture),
					material->specularGlossiness ? 1u : 0u, 0u),
			});
		};

		for (const RenderObject& object : scene.objects) {
			appendMaterial(object.model, object.material);
		}
		for (const RenderCloth& cloth : scene.cloths) {
			appendMaterial({}, cloth.material);
		}

		UploadBuffer(
			commands,
			lightingFrames_.at(frame_.frameIndex).materialBuffer,
			materials.data(),
			materials.size() * sizeof(GpuMaterial),
			Velos::RHI::ResourceState::ShaderRead);
	}

	FrameData Renderer::BuildFrameData(const RenderCamera& camera) const
	{
		const auto dimensions = device_->GetSwapchainDimensions();
		if (dimensions.width == 0 || dimensions.height == 0) {
			return FrameData{
				.view = camera.view,
				.cameraPosition = glm::vec3(glm::inverse(camera.view)[3])
			};
		}

		const float aspectRatio = static_cast<float>(dimensions.width) /
			static_cast<float>(dimensions.height);
		glm::mat4 projection = glm::perspectiveRH_ZO(
			glm::radians(camera.verticalFov),
			aspectRatio,
			camera.nearPlane,
			camera.farPlane);
		projection[1][1] *= -1.0f;

		return FrameData{
			.view = camera.view,
			.projection = projection,
			.viewProjection = projection * camera.view,
			.cameraPosition = glm::vec3(glm::inverse(camera.view)[3])
		};
	}

	void Renderer::EndFrame()
	{
		if (!frameActive_) {
			return;
		}

		auto& commands = device_->GetCommandList();
		ProcessAssetUploads();

		commands.Barrier({
			.image = frame_.backbufferImage,
			.oldLayout = device_->GetImageLayout(frame_.backbufferImage, 0),
			.newLayout = Velos::RHI::ImageLayout::Present,
			.aspect = Velos::RHI::ImageAspect::Color,
		});
		commands.End();

		device_->SubmitAndPresent(swapchain_, {
			.waits = frameGraph_.GraphicsSubmissionWaits(),
		});
		lastSubmittedSerial_ = nextSubmissionSerial_++;
		frameSubmissionSerials_.at(frame_.frameIndex) = lastSubmittedSerial_;
		frameActive_ = false;
	}

    void Renderer::InitializeImGui() { imGui_ = std::make_unique<ImGuiRenderer>(*device_); }
    void Renderer::ShutdownImGui() { imGui_.reset(); }
    void Renderer::BeginImGuiFrame() { imGui_->BeginFrame(); }

	void Renderer::DrawImGui()
	{
		if (!frameActive_ || !imGui_) return;
		auto& commands = device_->GetCommandList();
		commands.Barrier({ .image = frame_.backbufferImage,
			.oldLayout = device_->GetImageLayout(frame_.backbufferImage, 0),
			.newLayout = Velos::RHI::ImageLayout::ColorAttachment });
		const auto size = device_->GetSwapchainDimensions();
		const Velos::RHI::ColorAttachmentDesc color{
			.view = frame_.backbuffer, .loadOp = Velos::RHI::LoadOp::Load };
		commands.BeginRendering({ .renderArea = { 0, 0, size.width, size.height },
			.colorAttachments = &color, .colorAttachmentCount = 1 });
		imGui_->Draw();
		commands.EndRendering();
	}

	void Renderer::ProcessAssetUploads()
	{
		for (AssetUploadRequest& request : assetUploads_.Drain()) {
			try {
				if (request.model) {
					if (!ResolveOrCreateModel(request.model)) {
						throw std::runtime_error("Could not create GPU model resources");
					}
				}

				if (request.onComplete) request.onComplete();
			}
			catch (const std::exception& exception) {
				if (request.onFailure) request.onFailure(exception.what());
			}
			catch (...) {
				if (request.onFailure) request.onFailure("Unknown GPU upload failure");
			}
		}
	}

	void Renderer::CreatePipelineResources()
	{
		const auto gltfVertexShader = Velos::ShaderCompiler::CompileFile({
			.path = "assets/shaders/internal/gltf.vert.spv",
			.stage = Velos::RHI::ShaderStage::Vertex,
			.entryPoint = "main",
			.language = Velos::ShaderSourceLanguage::SpirvBinary,
		});
		const auto gltfFragmentShader = Velos::ShaderCompiler::CompileFile({
			.path = "assets/shaders/internal/gltf_bindless.frag.spv",
			.stage = Velos::RHI::ShaderStage::Fragment,
			.entryPoint = "main",
			.language = Velos::ShaderSourceLanguage::SpirvBinary,
		});
		gltfVertexShader_ = device_->CreateShader({
			.stage = Velos::RHI::ShaderStage::Vertex,
			.bytecode = gltfVertexShader.spirv.data(),
			.bytecodeSize = static_cast<Velos::u64>(gltfVertexShader.spirv.size() * sizeof(std::uint32_t)),
			.entryPoint = "main",
			.reflection = gltfVertexShader.reflection,
			.debugName = "Iryven glTF vertex shader",
		});
		gltfFragmentShader_ = device_->CreateShader({
			.stage = Velos::RHI::ShaderStage::Fragment,
			.bytecode = gltfFragmentShader.spirv.data(),
			.bytecodeSize = static_cast<Velos::u64>(gltfFragmentShader.spirv.size() * sizeof(std::uint32_t)),
			.entryPoint = "main",
			.reflection = gltfFragmentShader.reflection,
			.debugName = "Iryven glTF fragment shader",
		});

		const Velos::RHI::VertexBufferLayoutDesc gltfVertexLayout{
			.stride = sizeof(Vertex),
			.inputRate = Velos::RHI::VertexInputRate::PerVertex,
			.attributes = {
				{.location = 0, .binding = 0, .format = Velos::RHI::VertexFormat::Float32x3, .offset = offsetof(Vertex, position)},
				{.location = 1, .binding = 0, .format = Velos::RHI::VertexFormat::Float32x3, .offset = offsetof(Vertex, normal)},
				{.location = 2, .binding = 0, .format = Velos::RHI::VertexFormat::Float32x2, .offset = offsetof(Vertex, texCoord)},
				{.location = 3, .binding = 0, .format = Velos::RHI::VertexFormat::Float32x4, .offset = offsetof(Vertex, tangent)},
				{.location = 4, .binding = 0, .format = Velos::RHI::VertexFormat::Float32x4, .offset = offsetof(Vertex, color)},
			}
		};
		const std::array gltfReflections{
			gltfVertexShader.reflection,
			gltfFragmentShader.reflection,
		};
		auto gltfReflection =
			Velos::ShaderCompiler::MergeShaderReflection(gltfReflections);
		// Set 0 is shared by the traditional and mesh pipelines. The reflected
		// graphics shaders only expose the frame buffer to vertex/fragment, so
		// widen that binding before creating the shared layout.
		const auto frameBinding = std::ranges::find_if(
			gltfReflection.resources,
			[](const Velos::ShaderResourceBinding& resource) {
				return resource.set == 0 && resource.binding == 1;
			});
		if (frameBinding == gltfReflection.resources.end()) {
			throw std::runtime_error(
				"glTF pipeline reflection is missing the frame buffer binding");
		}
		frameBinding->stage = frameBinding->stage | Velos::RHI::ShaderStage::Mesh |
			Velos::RHI::ShaderStage::Task;
		const Velos::PipelineLayoutOverrides gltfLayoutOverrides{
			.existingSetLayouts = {
				{1, bindlessTextureManager_->Layout()},
			},
		};
		gltfGeneratedLayout_ = device_->BuildPipelineLayout(
			gltfReflection, gltfLayoutOverrides);
		if (gltfGeneratedLayout_.setLayouts.size() != 2) {
			throw std::runtime_error(
				"Reflected glTF pipeline must contain descriptor sets 0 and 1");
		}
		lightsBindingLayout_ = gltfGeneratedLayout_.setLayouts[0];

		Velos::RHI::GraphicsPipelineDesc gltfDesc{
			.vertexShader = gltfVertexShader_,
			.fragmentShader = gltfFragmentShader_,
			.vertexLayouts = { gltfVertexLayout },
			.layout = {
				.descriptorSetLayouts = gltfGeneratedLayout_.setLayouts.data(),
				.descriptorSetLayoutCount = static_cast<Velos::u32>(
					gltfGeneratedLayout_.setLayouts.size())
			},
			.topology = Velos::RHI::PrimitiveTopology::TriangleList,
			.raster = {
				.cullBackFaces = true,
				.frontFaceCCW = true,
				.wireframe = false,
			},
			.depth = {
				.depthTestEnable = true,
				.depthWriteEnable = true,
				.depthFormat = Velos::RHI::Format::D32_FLOAT,
			},
			.colorFormat = Velos::RHI::Format::BGRA8_UNORM,
			.debugName = "Iryven glTF pipeline",
		};
        const auto createGBufferShader = [&](const char* path) {
            const auto code = Velos::ShaderCompiler::CompileFile({.path = path,
                .stage = ShaderStage::Fragment, .language = Velos::ShaderSourceLanguage::SpirvBinary});
            return device_->CreateShader({.stage = ShaderStage::Fragment,
                .bytecode = code.spirv.data(), .bytecodeSize = code.spirv.size() * sizeof(std::uint32_t),
                .reflection = code.reflection, .debugName = path});
        };
        gbufferFragmentShader_ = createGBufferShader("assets/shaders/internal/gbuffer.frag.spv");
        gbufferMeshletFragmentShader_ = createGBufferShader("assets/shaders/internal/gbuffer_meshlet.frag.spv");
        gltfDesc.fragmentShader = gbufferFragmentShader_;
        gltfDesc.colorAttachments = GBufferAttachments();
        gltfPipeline_ = device_->CreateGraphicsPipeline(gltfDesc);
        gltfDesc.fragmentShader = gltfFragmentShader_;
        gltfDesc.colorAttachments.clear();
		// Thin transmission: C = C_background * transmittance + C_surface.
		// Disable depth writes so the background remains visible.
		gltfDesc.depth.depthWriteEnable = false;
		gltfDesc.raster.cullBackFaces = true;
		gltfDesc.blend = {
			.enable = true,
			.srcColor = Velos::RHI::BlendFactor::Zero,
			.dstColor = Velos::RHI::BlendFactor::SrcColor,
			.srcAlpha = Velos::RHI::BlendFactor::Zero,
			.dstAlpha = Velos::RHI::BlendFactor::One,
		};
		gltfDesc.debugName = "Iryven graphics transmission attenuation";
		gltfTransmissionPipelines_[0] = device_->CreateGraphicsPipeline(gltfDesc);
		gltfDesc.blend.srcColor = Velos::RHI::BlendFactor::One;
		gltfDesc.blend.dstColor = Velos::RHI::BlendFactor::One;
		gltfDesc.debugName = "Iryven graphics transmission surface";
		gltfTransmissionPipelines_[1] = device_->CreateGraphicsPipeline(gltfDesc);

		const auto clothVertexShader = Velos::ShaderCompiler::CompileFile({
			.path = "assets/shaders/internal/cloth.vert.spv",
			.stage = Velos::RHI::ShaderStage::Vertex,
			.entryPoint = "main",
			.language = Velos::ShaderSourceLanguage::SpirvBinary,
		});
		clothVertexShader_ = device_->CreateShader({
			.stage = Velos::RHI::ShaderStage::Vertex,
			.bytecode = clothVertexShader.spirv.data(),
			.bytecodeSize = static_cast<Velos::u64>(
				clothVertexShader.spirv.size() * sizeof(std::uint32_t)),
			.entryPoint = "main",
			.reflection = clothVertexShader.reflection,
			.debugName = "Iryven cloth vertex shader",
		});
		const std::array clothGraphicsReflections{
			clothVertexShader.reflection,
			gltfFragmentShader.reflection,
		};
		const auto clothGraphicsReflection =
			Velos::ShaderCompiler::MergeShaderReflection(clothGraphicsReflections);
		const Velos::PipelineLayoutOverrides clothGraphicsOverrides{
			.existingSetLayouts = {
				{0, lightsBindingLayout_},
				{1, bindlessTextureManager_->Layout()},
			},
		};
		clothGraphicsGeneratedLayout_ = device_->BuildPipelineLayout(
			clothGraphicsReflection, clothGraphicsOverrides);
		if (clothGraphicsGeneratedLayout_.setLayouts.size() != 3) {
			throw std::runtime_error(
				"Reflected cloth pipeline must contain descriptor sets 0, 1, and 2");
		}
		clothRenderBindingLayout_ = clothGraphicsGeneratedLayout_.setLayouts[2];
		clothGraphicsPipeline_ = device_->CreateGraphicsPipeline({
			.vertexShader = clothVertexShader_,
			.fragmentShader = gbufferFragmentShader_,
			.layout = {
				.descriptorSetLayouts =
					clothGraphicsGeneratedLayout_.setLayouts.data(),
				.descriptorSetLayoutCount = static_cast<Velos::u32>(
					clothGraphicsGeneratedLayout_.setLayouts.size()),
			},
			.topology = Velos::RHI::PrimitiveTopology::TriangleList,
			.raster = {
				.cullBackFaces = false,
				.frontFaceCCW = true,
				.wireframe = false,
			},
			.depth = {
				.depthTestEnable = true,
				.depthWriteEnable = true,
				.depthFormat = Velos::RHI::Format::D32_FLOAT,
			},
			.colorAttachments = GBufferAttachments(),
			.debugName = "Iryven cloth G-buffer pipeline",
		});

		const std::filesystem::path clothComputePath =
			"assets/shaders/internal/cloth.comp.spv";
		if (std::filesystem::exists(clothComputePath)) {
			const auto clothComputeShader = Velos::ShaderCompiler::CompileFile({
				.path = clothComputePath.string(),
				.stage = Velos::RHI::ShaderStage::Compute,
				.entryPoint = "main",
				.language = Velos::ShaderSourceLanguage::SpirvBinary,
			});
			const auto hasStorageBuffer = [&clothComputeShader](
				std::uint32_t binding) {
				return std::any_of(
					clothComputeShader.reflection.resources.begin(),
					clothComputeShader.reflection.resources.end(),
					[binding](const Velos::ShaderResourceBinding& resource) {
						return resource.set == 0 && resource.binding == binding &&
							resource.type == Velos::ShaderResourceType::StorageBuffer;
					});
			};
			const bool validPushConstants =
				clothComputeShader.reflection.pushConstants.size() == 1 &&
				clothComputeShader.reflection.pushConstants[0].offset == 0 &&
				clothComputeShader.reflection.pushConstants[0].size ==
					sizeof(ClothSimulationConstants);
			if (clothComputeShader.reflection.resources.size() != 2 ||
				!hasStorageBuffer(0) || !hasStorageBuffer(1) ||
				!validPushConstants) {
				throw std::runtime_error(
					"Cloth compute shader must declare storage buffers at set 0 "
					"bindings 0 and 1, plus the 64-byte cloth push constants");
			}
			clothComputeShader_ = device_->CreateShader({
				.stage = Velos::RHI::ShaderStage::Compute,
				.bytecode = clothComputeShader.spirv.data(),
				.bytecodeSize = static_cast<Velos::u64>(
					clothComputeShader.spirv.size() * sizeof(std::uint32_t)),
				.entryPoint = "main",
				.reflection = clothComputeShader.reflection,
				.debugName = "Iryven cloth compute shader",
			});
			const std::array clothComputeReflections{
				clothComputeShader.reflection,
			};
			const auto clothComputeReflection =
				Velos::ShaderCompiler::MergeShaderReflection(clothComputeReflections);
			clothComputeGeneratedLayout_ =
				device_->BuildPipelineLayout(clothComputeReflection);
			if (clothComputeGeneratedLayout_.setLayouts.size() != 1) {
				throw std::runtime_error(
					"Cloth compute shader must use descriptor set 0 only");
			}
			clothSimulationBindingLayout_ =
				clothComputeGeneratedLayout_.setLayouts[0];
			clothComputePipeline_ = device_->CreateComputePipeline({
				.computeShader = clothComputeShader_,
				.layout = {
					.descriptorSetLayouts =
						clothComputeGeneratedLayout_.setLayouts.data(),
					.descriptorSetLayoutCount = static_cast<Velos::u32>(
						clothComputeGeneratedLayout_.setLayouts.size()),
				},
				.debugName = "Iryven cloth compute pipeline",
			});
		}

		const auto textVertexShader = Velos::ShaderCompiler::CompileFile({
			.path = "assets/shaders/internal/ui_text.vert.spv",
			.stage = Velos::RHI::ShaderStage::Vertex,
			.entryPoint = "main",
			.language = Velos::ShaderSourceLanguage::SpirvBinary,
		});
		const auto textFragmentShader = Velos::ShaderCompiler::CompileFile({
			.path = "assets/shaders/internal/ui_text.frag.spv",
			.stage = Velos::RHI::ShaderStage::Fragment,
			.entryPoint = "main",
			.language = Velos::ShaderSourceLanguage::SpirvBinary,
		});
		textVertexShader_ = device_->CreateShader({
			.stage = Velos::RHI::ShaderStage::Vertex,
			.bytecode = textVertexShader.spirv.data(),
			.bytecodeSize = static_cast<Velos::u64>(textVertexShader.spirv.size() * sizeof(std::uint32_t)),
			.entryPoint = "main",
			.reflection = textVertexShader.reflection,
			.debugName = "Iryven text vertex shader",
		});
		textFragmentShader_ = device_->CreateShader({
			.stage = Velos::RHI::ShaderStage::Fragment,
			.bytecode = textFragmentShader.spirv.data(),
			.bytecodeSize = static_cast<Velos::u64>(textFragmentShader.spirv.size() * sizeof(std::uint32_t)),
			.entryPoint = "main",
			.reflection = textFragmentShader.reflection,
			.debugName = "Iryven text fragment shader",
		});

		const Velos::RHI::VertexBufferLayoutDesc textVertexLayout{
			.stride = sizeof(TextVertex),
			.inputRate = Velos::RHI::VertexInputRate::PerVertex,
			.attributes = {
				{.location = 0, .binding = 0, .format = Velos::RHI::VertexFormat::Float32x2, .offset = offsetof(TextVertex, position)},
				{.location = 1, .binding = 0, .format = Velos::RHI::VertexFormat::Float32x2, .offset = offsetof(TextVertex, uv)},
				{.location = 2, .binding = 0, .format = Velos::RHI::VertexFormat::Float32x4, .offset = offsetof(TextVertex, color)}
			}
		};
		const std::array textReflections{
			textVertexShader.reflection,
			textFragmentShader.reflection,
		};
		const auto textReflection =
			Velos::ShaderCompiler::MergeShaderReflection(textReflections);
		textGeneratedLayout_ = device_->BuildPipelineLayout(textReflection);
		if (textGeneratedLayout_.setLayouts.size() != 1) {
			throw std::runtime_error(
				"Reflected text pipeline must contain descriptor set 0");
		}
		fontBindingLayout_ = textGeneratedLayout_.setLayouts[0];

		textPipeline_ = device_->CreateGraphicsPipeline({
			.vertexShader = textVertexShader_,
			.fragmentShader = textFragmentShader_,
			.vertexLayouts = {textVertexLayout},
			.layout = {
				.descriptorSetLayouts = textGeneratedLayout_.setLayouts.data(),
				.descriptorSetLayoutCount = static_cast<Velos::u32>(
					textGeneratedLayout_.setLayouts.size())
			},
			.topology = Velos::RHI::PrimitiveTopology::TriangleList,
			.raster = {.cullBackFaces = false},
			.depth = {
				.depthTestEnable = false,
				.depthWriteEnable = false,
				.depthFormat = Velos::RHI::Format::D32_FLOAT
			},
			.blend = {.enable = true},
			.colorFormat = Velos::RHI::Format::BGRA8_UNORM,
			.debugName = "Iryven text pipeline",
		});

		auto ts = Velos::ShaderCompiler::CompileFile({
			.path = "assets/shaders/internal/meshlet.task.spv",
			.stage = Velos::RHI::ShaderStage::Task,
			.entryPoint = "main",
			.language = Velos::ShaderSourceLanguage::SpirvBinary,
		});

		auto ms = Velos::ShaderCompiler::CompileFile({
			.path = "assets/shaders/internal/meshlet.mesh.spv",
			.stage = Velos::RHI::ShaderStage::Mesh,
			.entryPoint = "main",
			.language = Velos::ShaderSourceLanguage::SpirvBinary,
			});

		auto fs = Velos::ShaderCompiler::CompileFile({
			.path = "assets/shaders/internal/meshlet.frag.spv",
			.stage = Velos::RHI::ShaderStage::Fragment,
			.entryPoint = "main",
			.language = Velos::ShaderSourceLanguage::SpirvBinary,
			});

		meshletTaskShader_ = device_->CreateShader({
			.stage = Velos::RHI::ShaderStage::Task,
			.bytecode = ts.spirv.data(),
			.bytecodeSize = static_cast<Velos::u64>(ts.spirv.size() * sizeof(std::uint32_t)),
			.entryPoint = "main",
			.reflection = ts.reflection,
			.debugName = "Iryven meshlet culling task shader",
		});

		meshletShader_ = device_->CreateShader({
			.stage = Velos::RHI::ShaderStage::Mesh,
			.bytecode = ms.spirv.data(),
			.bytecodeSize = static_cast<Velos::u64>(ms.spirv.size() * sizeof(std::uint32_t)),
			.entryPoint = "main",
			.reflection = ms.reflection,
			.debugName = "Iryven meshlet shader",
			});

		meshletFragmentShader_ = device_->CreateShader({
			.stage = Velos::RHI::ShaderStage::Fragment,
			.bytecode = fs.spirv.data(),
			.bytecodeSize = static_cast<Velos::u64>(fs.spirv.size() * sizeof(std::uint32_t)),
			.entryPoint = "main",
			.reflection = fs.reflection,
			.debugName = "Iryven meshlet fragment shader",
			});

		const std::array meshReflections{ts.reflection, ms.reflection, fs.reflection};
		const auto meshReflection =
			Velos::ShaderCompiler::MergeShaderReflection(meshReflections);
		const Velos::PipelineLayoutOverrides meshLayoutOverrides{
			.existingSetLayouts = {
				{0, lightsBindingLayout_},
				{1, bindlessTextureManager_->Layout()},
				{3, hiZPass_->SamplingLayout()},
			},
		};
		meshletGeneratedLayout_ = device_->BuildPipelineLayout(
			meshReflection, meshLayoutOverrides);
		if (meshletGeneratedLayout_.setLayouts.size() != 4) {
			throw std::runtime_error(
				"Mesh pipeline must contain descriptor sets 0, 1, 2, and Hi-Z set 3");
		}
		meshletBindingLayout_ = meshletGeneratedLayout_.setLayouts[2];

		Velos::RHI::MeshPipelineDesc meshletDesc{
			.taskShader = meshletTaskShader_,
			.meshShader = meshletShader_,
			.fragmentShader = meshletFragmentShader_,
			.layout = {
				.descriptorSetLayouts = meshletGeneratedLayout_.setLayouts.data(),
				.descriptorSetLayoutCount = static_cast<Velos::u32>(
					meshletGeneratedLayout_.setLayouts.size()),
			},
			.raster = {.cullBackFaces = false, .frontFaceCCW = true},
			.depth = {
				.depthTestEnable = true,
				.depthWriteEnable = true,
				.depthFormat = Velos::RHI::Format::D32_FLOAT
			},
			.colorFormat = Velos::RHI::Format::BGRA8_UNORM,
			.debugName = "Iryven meshlet pipeline",
			};
        meshletDesc.fragmentShader = gbufferMeshletFragmentShader_;
        meshletDesc.colorAttachments = GBufferAttachments();
        meshletPipeline_ = device_->CreateMeshPipeline(meshletDesc);
        meshletDesc.fragmentShader = meshletFragmentShader_;
        meshletDesc.colorAttachments.clear();
		// Thin transmission: C = C_background * transmittance + C_surface.
		// Disable depth writes so the background remains visible.
		meshletDesc.depth.depthWriteEnable = false;
		meshletDesc.raster.cullBackFaces = true;
		meshletDesc.blend = {
			.enable = true,
			.srcColor = Velos::RHI::BlendFactor::Zero,
			.dstColor = Velos::RHI::BlendFactor::SrcColor,
			.srcAlpha = Velos::RHI::BlendFactor::Zero,
			.dstAlpha = Velos::RHI::BlendFactor::One,
		};
		meshletDesc.debugName = "Iryven mesh transmission attenuation";
		meshletTransmissionPipelines_[0] = device_->CreateMeshPipeline(meshletDesc);
		meshletDesc.blend.srcColor = Velos::RHI::BlendFactor::One;
		meshletDesc.blend.dstColor = Velos::RHI::BlendFactor::One;
		meshletDesc.debugName = "Iryven mesh transmission surface";
		meshletTransmissionPipelines_[1] = device_->CreateMeshPipeline(meshletDesc);
	}

	void Renderer::CreateDepthResources(std::uint32_t width, std::uint32_t height)
	{
		depthImage_ = device_->CreateImage({
			.width = width,
			.height = height,
			.format = Velos::RHI::Format::D32_FLOAT,
			.usage = Velos::RHI::ImageUsage::DepthStencil | Velos::RHI::ImageUsage::Sampled,
			.concurrentQueues = true,
			.debugName = "Iryven main depth image"
		});
		try {
			depthView_ = device_->CreateImageView({
				.image = depthImage_,
				.format = Velos::RHI::Format::D32_FLOAT,
				.aspect = Velos::RHI::ImageAspect::Depth,
				.debugName = "Iryven main depth view"
			});
		}
		catch (...) {
			device_->DestroyImage(depthImage_);
			depthImage_ = {};
			throw;
		}
	}

	void Renderer::DestroyDepthResources()
	{
		if (depthView_) {
			device_->DestroyImageView(depthView_);
			depthView_ = {};
		}
		if (depthImage_) {
			device_->DestroyImage(depthImage_);
			depthImage_ = {};
		}
	}

	void Renderer::DestroyPipelineResources()
    {
        if (gbufferFragmentShader_) { device_->DestroyShader(gbufferFragmentShader_); gbufferFragmentShader_ = {}; }
        if (gbufferMeshletFragmentShader_) { device_->DestroyShader(gbufferMeshletFragmentShader_); gbufferMeshletFragmentShader_ = {}; }
		for (auto& pipeline : meshletTransmissionPipelines_) {
			if (pipeline) device_->DestroyPipeline(pipeline);
			pipeline = {};
		}
		for (auto& pipeline : gltfTransmissionPipelines_) {
			if (pipeline) device_->DestroyPipeline(pipeline);
			pipeline = {};
		}
		if (meshletPipeline_) {
			device_->DestroyPipeline(meshletPipeline_);
			meshletPipeline_ = {};
		}
		if (meshletFragmentShader_) {
			device_->DestroyShader(meshletFragmentShader_);
			meshletFragmentShader_ = {};
		}
		if (meshletShader_) {
			device_->DestroyShader(meshletShader_);
			meshletShader_ = {};
		}
		if (meshletTaskShader_) {
			device_->DestroyShader(meshletTaskShader_);
			meshletTaskShader_ = {};
		}
		meshletBindingLayout_ = {};
		for (const auto layout : meshletGeneratedLayout_.ownedSetLayouts) {
			device_->DestroyBindingLayout(layout);
		}
		meshletGeneratedLayout_ = {};

		if (clothComputePipeline_) {
			device_->DestroyPipeline(clothComputePipeline_);
			clothComputePipeline_ = {};
		}
		if (clothComputeShader_) {
			device_->DestroyShader(clothComputeShader_);
			clothComputeShader_ = {};
		}
		if (clothGraphicsPipeline_) {
			device_->DestroyPipeline(clothGraphicsPipeline_);
			clothGraphicsPipeline_ = {};
		}
		if (clothVertexShader_) {
			device_->DestroyShader(clothVertexShader_);
			clothVertexShader_ = {};
		}
		if (textPipeline_) {
			device_->DestroyPipeline(textPipeline_);
			textPipeline_ = {};
		}
		if (textFragmentShader_) {
			device_->DestroyShader(textFragmentShader_);
			textFragmentShader_ = {};
		}
		if (textVertexShader_) {
			device_->DestroyShader(textVertexShader_);
			textVertexShader_ = {};
		}
		if (gltfPipeline_) {
			device_->DestroyPipeline(gltfPipeline_);
			gltfPipeline_ = {};
		}
		if (gltfFragmentShader_) {
			device_->DestroyShader(gltfFragmentShader_);
			gltfFragmentShader_ = {};
		}
		if (gltfVertexShader_) {
			device_->DestroyShader(gltfVertexShader_);
			gltfVertexShader_ = {};
		}
	}

	Renderer::GpuMesh* Renderer::ResolveOrCreateMesh(
		const std::shared_ptr<const MeshData>& mesh)
	{
		if (!mesh) {
			return nullptr;
		}
		if (const auto existing = meshes_.find(mesh.get()); existing != meshes_.end()) {
			return &existing->second;
		}
		if (mesh->vertices.empty() || mesh->indices.empty()) return nullptr;

		const std::size_t vertexCount = mesh->vertices.size();
		if (mesh->indices.size() > std::numeric_limits<std::uint32_t>::max() ||
			std::ranges::any_of(mesh->indices, [vertexCount](std::uint32_t index) {
				return index >= vertexCount;
			})) {
			return nullptr;
		}

		static_assert(sizeof(Vertex) == 64,
			"meshlet.mesh expects Vertex to occupy 16 floats");
		const std::size_t vertexBufferSize = mesh->vertices.size() * sizeof(Vertex);
		const std::size_t indexBufferSize =
			mesh->indices.size() * sizeof(std::uint32_t);

		std::vector<GpuMeshlet> gpuMeshlets;
		std::vector<std::uint32_t> meshletVertexIndices;
		std::vector<std::uint32_t> meshletTriangleIndices;
		gpuMeshlets.reserve(mesh->meshlets.size());
		for (const Meshlet& meshlet : mesh->meshlets) {
			if (meshlet.vertexIndices.empty() || meshlet.triangleIndices.empty() ||
				meshlet.vertexIndices.size() > 64 ||
				meshlet.triangleIndices.size() % 3 != 0 ||
				meshlet.triangleIndices.size() / 3 > 128 ||
				std::ranges::any_of(meshlet.vertexIndices,
					[vertexCount](std::uint32_t index) { return index >= vertexCount; }) ||
				std::ranges::any_of(meshlet.triangleIndices,
					[&meshlet](std::uint32_t index) {
						return index >= meshlet.vertexIndices.size();
					})) {
				throw std::runtime_error("Mesh contains invalid meshlet data");
			}
			if (meshletVertexIndices.size() > std::numeric_limits<std::uint32_t>::max() ||
				meshletTriangleIndices.size() > std::numeric_limits<std::uint32_t>::max()) {
				throw std::runtime_error("Meshlet streams exceed 32-bit offsets");
			}
			gpuMeshlets.push_back({
				.vertexOffset = static_cast<std::uint32_t>(meshletVertexIndices.size()),
				.triangleOffset = static_cast<std::uint32_t>(meshletTriangleIndices.size()),
				.vertexCount = static_cast<std::uint32_t>(meshlet.vertexIndices.size()),
				.triangleCount = static_cast<std::uint32_t>(
					meshlet.triangleIndices.size() / 3),
				.center = meshlet.center,
				.radius = meshlet.radius,
				.coneApex = meshlet.coneApex,
				.coneAxis = meshlet.coneAxis,
				.coneCutoff = meshlet.coneCutoff,
			});
			meshletVertexIndices.insert(meshletVertexIndices.end(),
				meshlet.vertexIndices.begin(), meshlet.vertexIndices.end());
			meshletTriangleIndices.insert(meshletTriangleIndices.end(),
				meshlet.triangleIndices.begin(), meshlet.triangleIndices.end());
		}
		if (gpuMeshlets.size() > std::numeric_limits<std::uint32_t>::max()) {
			throw std::runtime_error("Mesh contains too many meshlets");
		}

		GpuMesh gpuMesh{
			.source = mesh,
			.indexCount = static_cast<std::uint32_t>(mesh->indices.size()),
			.meshletCount = static_cast<std::uint32_t>(gpuMeshlets.size()),
		};
		const auto destroyGpuMesh = [this](GpuMesh& gpu) {
			if (gpu.meshletBindingPool) device_->DestroyBindingPool(gpu.meshletBindingPool);
			if (gpu.meshletTriangleIndexBuffer) device_->DestroyBuffer(gpu.meshletTriangleIndexBuffer);
			if (gpu.meshletVertexIndexBuffer) device_->DestroyBuffer(gpu.meshletVertexIndexBuffer);
			if (gpu.meshletBuffer) device_->DestroyBuffer(gpu.meshletBuffer);
			if (gpu.vertexStorageBuffer) device_->DestroyBuffer(gpu.vertexStorageBuffer);
			if (gpu.indexBuffer) device_->DestroyBuffer(gpu.indexBuffer);
			if (gpu.vertexBuffer) device_->DestroyBuffer(gpu.vertexBuffer);
		};

		try {
			gpuMesh.vertexBuffer = device_->CreateBuffer({
				.size = vertexBufferSize,
				.usage = Velos::RHI::BufferUsage::Vertex |
					Velos::RHI::BufferUsage::TransferDst,
				.memoryUsage = Velos::RHI::MemoryUsage::GPUOnly,
				.debugName = "Iryven mesh vertex buffer"
			});
			gpuMesh.indexBuffer = device_->CreateBuffer({
				.size = indexBufferSize,
				.usage = Velos::RHI::BufferUsage::Index |
					Velos::RHI::BufferUsage::TransferDst,
				.memoryUsage = Velos::RHI::MemoryUsage::GPUOnly,
				.debugName = "Iryven mesh index buffer"
			});

			std::size_t uploadSize = vertexBufferSize + indexBufferSize + 16;
			if (!gpuMeshlets.empty()) {
				const std::size_t meshletBufferSize = gpuMeshlets.size() * sizeof(GpuMeshlet);
				const std::size_t vertexIndexBufferSize =
					meshletVertexIndices.size() * sizeof(std::uint32_t);
				const std::size_t triangleIndexBufferSize =
					meshletTriangleIndices.size() * sizeof(std::uint32_t);
				gpuMesh.vertexStorageBuffer = device_->CreateBuffer({
					.size = vertexBufferSize,
					.usage = BufferUsage::Storage | BufferUsage::TransferDst,
					.memoryUsage = MemoryUsage::GPUOnly,
					.debugName = "Iryven mesh shader vertex buffer",
				});
				gpuMesh.meshletBuffer = device_->CreateBuffer({
					.size = meshletBufferSize,
					.usage = BufferUsage::Storage | BufferUsage::TransferDst,
					.memoryUsage = MemoryUsage::GPUOnly,
					.debugName = "Iryven meshlet buffer",
				});
				gpuMesh.meshletVertexIndexBuffer = device_->CreateBuffer({
					.size = vertexIndexBufferSize,
					.usage = BufferUsage::Storage | BufferUsage::TransferDst,
					.memoryUsage = MemoryUsage::GPUOnly,
					.debugName = "Iryven meshlet vertex index buffer",
				});
				gpuMesh.meshletTriangleIndexBuffer = device_->CreateBuffer({
					.size = triangleIndexBufferSize,
					.usage = BufferUsage::Storage | BufferUsage::TransferDst,
					.memoryUsage = MemoryUsage::GPUOnly,
					.debugName = "Iryven meshlet triangle index buffer",
				});
				uploadSize += vertexBufferSize + meshletBufferSize +
					vertexIndexBufferSize + triangleIndexBufferSize + 64;
			}

			auto upload = device_->CreateUploadContext(uploadSize);
			upload->Begin();
			upload->UploadBuffer({
				.dstBuffer = gpuMesh.vertexBuffer,
				.size = vertexBufferSize,
				.data = mesh->vertices.data(),
				.finalState = Velos::RHI::ResourceState::VertexBuffer,
			});
			upload->UploadBuffer({
				.dstBuffer = gpuMesh.indexBuffer,
				.size = indexBufferSize,
				.data = mesh->indices.data(),
				.finalState = Velos::RHI::ResourceState::IndexBuffer,
			});
			if (!gpuMeshlets.empty()) {
				upload->UploadBuffer({
					.dstBuffer = gpuMesh.vertexStorageBuffer,
					.size = vertexBufferSize,
					.data = mesh->vertices.data(),
					.finalState = ResourceState::ShaderRead,
				});
				upload->UploadBuffer({
					.dstBuffer = gpuMesh.meshletBuffer,
					.size = gpuMeshlets.size() * sizeof(GpuMeshlet),
					.data = gpuMeshlets.data(),
					.finalState = ResourceState::ShaderRead,
				});
				upload->UploadBuffer({
					.dstBuffer = gpuMesh.meshletVertexIndexBuffer,
					.size = meshletVertexIndices.size() * sizeof(std::uint32_t),
					.data = meshletVertexIndices.data(),
					.finalState = ResourceState::ShaderRead,
				});
				upload->UploadBuffer({
					.dstBuffer = gpuMesh.meshletTriangleIndexBuffer,
					.size = meshletTriangleIndices.size() * sizeof(std::uint32_t),
					.data = meshletTriangleIndices.data(),
					.finalState = ResourceState::ShaderRead,
				});
			}
			upload->Flush();
			device_->AcquireUploadedBuffers(upload->TakePendingBufferAcquires());

			if (!gpuMeshlets.empty()) {
				const BindingPoolSize poolSize{
					.type = BindingType::StorageBuffer,
					.count = 4,
				};
				gpuMesh.meshletBindingPool = device_->CreateBindingPool({
					.poolSizes = &poolSize,
					.poolSizeCount = 1,
					.maxSets = 1,
					.debugName = "Iryven meshlet binding pool",
				});
				gpuMesh.meshletBindingSet = device_->AllocateBindingSet({
					.pool = gpuMesh.meshletBindingPool,
					.layout = meshletBindingLayout_,
					.debugName = "Iryven meshlet binding set",
				});
				const std::array infos{
					BindingBufferInfo{.buffer = gpuMesh.vertexStorageBuffer, .range = vertexBufferSize},
					BindingBufferInfo{.buffer = gpuMesh.meshletBuffer, .range = gpuMeshlets.size() * sizeof(GpuMeshlet)},
					BindingBufferInfo{.buffer = gpuMesh.meshletVertexIndexBuffer, .range = meshletVertexIndices.size() * sizeof(std::uint32_t)},
					BindingBufferInfo{.buffer = gpuMesh.meshletTriangleIndexBuffer, .range = meshletTriangleIndices.size() * sizeof(std::uint32_t)},
				};
				for (std::uint32_t binding = 0; binding < infos.size(); ++binding) {
					device_->UpdateBindingSet({
						.dstSet = gpuMesh.meshletBindingSet,
						.binding = binding,
						.type = BindingType::StorageBuffer,
						.bufferInfo = &infos[binding],
					});
				}
			}
		}
		catch (...) {
			destroyGpuMesh(gpuMesh);
			throw;
		}

		auto [entry, inserted] = meshes_.emplace(mesh.get(), std::move(gpuMesh));
		return &entry->second;
	}

	Renderer::GpuFont* Renderer::ResolveOrCreateFont(const std::shared_ptr<const Font>& font)
	{
		if (!font) {
			return nullptr;
		}
		if (const auto existing = fonts_.find(font.get()); existing != fonts_.end()) {
			return &existing->second;
		}
		if (!font->IsValid()) return nullptr;

		const auto pixels = font->GetAtlasPixels();

		const std::uint32_t width = font->GetAtlasWidth();
		const std::uint32_t height = font->GetAtlasHeight();

		const std::size_t expectedSize =
			static_cast<std::size_t>(width) * height * 4;

		if (pixels.size() != expectedSize) {
			return nullptr;
		}

		const auto atlasImage = device_->CreateImage({
			.width = width,
			.height = height,
			.format = Velos::RHI::Format::RGBA8_UNORM,
			.usage = Velos::RHI::ImageUsage::TransferDst |
					 Velos::RHI::ImageUsage::Sampled,
			.debugName = "Iryven font MTSDF atlas"
			});

		Velos::RHI::ImageViewHandle atlasView;
		Velos::RHI::SamplerHandle atlasSampler;
		Velos::RHI::BindingSetHandle bindingSet;
		try {
			auto upload = device_->CreateUploadContext(pixels.size());
			upload->Begin();
			upload->UploadImage(
				{
					.dstImage = atlasImage,
					.finalLayout = Velos::RHI::ImageLayout::ShaderReadOnly,
					.width = width,
					.height = height
				},
				pixels.data(),
				pixels.size()
			);
			upload->Flush();
			device_->AcquireUploadedImages(upload->TakePendingImageAcquires());

			atlasView = device_->CreateImageView({
				.image = atlasImage,
				.format = Velos::RHI::Format::RGBA8_UNORM,
				.aspect = Velos::RHI::ImageAspect::Color,
				.debugName = "Iryven font MTSDF atlas view"
			});
			atlasSampler = device_->CreateSampler({
				.minFilter = Velos::RHI::Filter::Linear,
				.magFilter = Velos::RHI::Filter::Linear,
				.addressU = Velos::RHI::SamplerAddressMode::ClampToEdge,
				.addressV = Velos::RHI::SamplerAddressMode::ClampToEdge,
				.addressW = Velos::RHI::SamplerAddressMode::ClampToEdge,
				.debugName = "Iryven font atlas sampler"
			});
			bindingSet = device_->AllocateBindingSet({
				.pool = fontBindingPool_,
				.layout = fontBindingLayout_,
				.debugName = "Iryven font binding set"
			});
			const Velos::RHI::BindingImageInfo imageInfo{
				.sampler = atlasSampler,
				.imageView = atlasView,
				.imageLayout = Velos::RHI::ImageLayout::ShaderReadOnly
			};
			device_->UpdateBindingSet({
				.dstSet = bindingSet,
				.binding = 0,
				.type = Velos::RHI::BindingType::CombinedImageSampler,
				.imageInfo = &imageInfo
			});
		}
		catch (...) {
			if (atlasSampler) {
				device_->DestroySampler(atlasSampler);
			}
			if (atlasView) {
				device_->DestroyImageView(atlasView);
			}
			device_->DestroyImage(atlasImage);
			throw;
		}

		GpuFont gpuFont{
			.source = font,
			.atlasImage = atlasImage,
			.atlasView = atlasView,
			.atlasSampler = atlasSampler,
			.bindingSet = bindingSet
		};

		auto [entry, inserted] = fonts_.emplace(font.get(), std::move(gpuFont));
		return &entry->second;
	}

	Renderer::GpuModel* Renderer::ResolveOrCreateModel(const ModelHandle& model)
	{
		if (!model) return nullptr;
		if (const auto existing = models_.find(model.get()); existing != models_.end())
			return &existing->second;
		if (!model->IsValid()) return nullptr;

		const std::size_t vertexBufferSize = model->vertices.size() * sizeof(Vertex);
		const std::size_t indexBufferSize =
			model->indices.size() * sizeof(std::uint32_t);

		std::vector<GpuMeshlet> gpuMeshlets;
		std::vector<std::uint32_t> meshletVertexIndices;
		std::vector<std::uint32_t> meshletTriangleIndices;
		std::vector<GpuModel::PrimitiveMeshlets> primitiveMeshlets;
		for (const Mesh& sourceMesh : model->meshes) {
			for (const MeshPrimitive& primitive : sourceMesh.primitives) {
				if (gpuMeshlets.size() > std::numeric_limits<std::uint32_t>::max()) {
					throw std::runtime_error("Model contains too many meshlets");
				}
				GpuModel::PrimitiveMeshlets gpuPrimitive{
					.firstIndex = primitive.firstIndex,
					.indexCount = primitive.indexCount,
					.vertexOffset = primitive.vertexOffset,
					.meshletOffset = static_cast<std::uint32_t>(gpuMeshlets.size()),
				};
				for (const Meshlet& meshlet : primitive.meshlets) {
					if (meshlet.vertexIndices.empty() || meshlet.triangleIndices.empty() ||
						meshlet.vertexIndices.size() > 64 ||
						meshlet.triangleIndices.size() % 3 != 0 ||
						meshlet.triangleIndices.size() / 3 > 128 ||
						std::ranges::any_of(meshlet.triangleIndices,
							[&meshlet](std::uint32_t index) {
								return index >= meshlet.vertexIndices.size();
							})) {
						throw std::runtime_error("Model contains invalid meshlet data");
					}
					if (meshletVertexIndices.size() > std::numeric_limits<std::uint32_t>::max() ||
						meshletTriangleIndices.size() > std::numeric_limits<std::uint32_t>::max()) {
						throw std::runtime_error("Model meshlet streams exceed 32-bit offsets");
					}
					gpuMeshlets.push_back({
						.vertexOffset = static_cast<std::uint32_t>(meshletVertexIndices.size()),
						.triangleOffset = static_cast<std::uint32_t>(meshletTriangleIndices.size()),
						.vertexCount = static_cast<std::uint32_t>(meshlet.vertexIndices.size()),
						.triangleCount = static_cast<std::uint32_t>(
							meshlet.triangleIndices.size() / 3),
						.center = meshlet.center,
						.radius = meshlet.radius,
						.coneApex = meshlet.coneApex,
						.coneAxis = meshlet.coneAxis,
						.coneCutoff = meshlet.coneCutoff,
					});
					for (const std::uint32_t localIndex : meshlet.vertexIndices) {
						const std::int64_t modelIndex =
							static_cast<std::int64_t>(localIndex) + primitive.vertexOffset;
						if (modelIndex < 0 || modelIndex >=
							static_cast<std::int64_t>(model->vertices.size())) {
							throw std::runtime_error(
								"Model meshlet vertex index is out of range");
						}
						meshletVertexIndices.push_back(
							static_cast<std::uint32_t>(modelIndex));
					}
					meshletTriangleIndices.insert(meshletTriangleIndices.end(),
						meshlet.triangleIndices.begin(), meshlet.triangleIndices.end());
				}
				gpuPrimitive.meshletCount = static_cast<std::uint32_t>(
					gpuMeshlets.size() - gpuPrimitive.meshletOffset);
				primitiveMeshlets.push_back(gpuPrimitive);
			}
		}

		GpuModel gpuModel{
			.source = model,
			.primitiveMeshlets = std::move(primitiveMeshlets),
		};
		try {
			gpuModel.vertexBuffer = device_->CreateBuffer({
				.size = vertexBufferSize,
				.usage = Velos::RHI::BufferUsage::Vertex |
					Velos::RHI::BufferUsage::TransferDst,
				.memoryUsage = Velos::RHI::MemoryUsage::GPUOnly,
				.debugName = "Iryven model vertex buffer"
			});
			gpuModel.indexBuffer = device_->CreateBuffer({
				.size = indexBufferSize,
				.usage = Velos::RHI::BufferUsage::Index |
					Velos::RHI::BufferUsage::TransferDst,
				.memoryUsage = Velos::RHI::MemoryUsage::GPUOnly,
				.debugName = "Iryven model index buffer"
			});

			std::size_t uploadSize = vertexBufferSize + indexBufferSize + 16;
			if (!gpuMeshlets.empty()) {
				const std::size_t meshletBytes = gpuMeshlets.size() * sizeof(GpuMeshlet);
				const std::size_t vertexIndexBytes =
					meshletVertexIndices.size() * sizeof(std::uint32_t);
				const std::size_t triangleIndexBytes =
					meshletTriangleIndices.size() * sizeof(std::uint32_t);
				gpuModel.vertexStorageBuffer = device_->CreateBuffer({
					.size = vertexBufferSize,
					.usage = BufferUsage::Storage | BufferUsage::TransferDst,
					.memoryUsage = MemoryUsage::GPUOnly,
					.debugName = "Iryven model mesh shader vertex buffer",
				});
				gpuModel.meshletBuffer = device_->CreateBuffer({
					.size = meshletBytes,
					.usage = BufferUsage::Storage | BufferUsage::TransferDst,
					.memoryUsage = MemoryUsage::GPUOnly,
					.debugName = "Iryven model meshlet buffer",
				});
				gpuModel.meshletVertexIndexBuffer = device_->CreateBuffer({
					.size = vertexIndexBytes,
					.usage = BufferUsage::Storage | BufferUsage::TransferDst,
					.memoryUsage = MemoryUsage::GPUOnly,
					.debugName = "Iryven model meshlet vertex index buffer",
				});
				gpuModel.meshletTriangleIndexBuffer = device_->CreateBuffer({
					.size = triangleIndexBytes,
					.usage = BufferUsage::Storage | BufferUsage::TransferDst,
					.memoryUsage = MemoryUsage::GPUOnly,
					.debugName = "Iryven model meshlet triangle index buffer",
				});
				uploadSize += vertexBufferSize + meshletBytes +
					vertexIndexBytes + triangleIndexBytes + 64;
			}

			auto upload = device_->CreateUploadContext(uploadSize);
			upload->Begin();
			upload->UploadBuffer({
				.dstBuffer = gpuModel.vertexBuffer,
				.size = vertexBufferSize,
				.data = model->vertices.data(),
				.finalState = Velos::RHI::ResourceState::VertexBuffer,
			});
			upload->UploadBuffer({
				.dstBuffer = gpuModel.indexBuffer,
				.size = indexBufferSize,
				.data = model->indices.data(),
				.finalState = Velos::RHI::ResourceState::IndexBuffer,
			});
			if (!gpuMeshlets.empty()) {
				upload->UploadBuffer({
					.dstBuffer = gpuModel.vertexStorageBuffer,
					.size = vertexBufferSize,
					.data = model->vertices.data(),
					.finalState = ResourceState::ShaderRead,
				});
				upload->UploadBuffer({
					.dstBuffer = gpuModel.meshletBuffer,
					.size = gpuMeshlets.size() * sizeof(GpuMeshlet),
					.data = gpuMeshlets.data(),
					.finalState = ResourceState::ShaderRead,
				});
				upload->UploadBuffer({
					.dstBuffer = gpuModel.meshletVertexIndexBuffer,
					.size = meshletVertexIndices.size() * sizeof(std::uint32_t),
					.data = meshletVertexIndices.data(),
					.finalState = ResourceState::ShaderRead,
				});
				upload->UploadBuffer({
					.dstBuffer = gpuModel.meshletTriangleIndexBuffer,
					.size = meshletTriangleIndices.size() * sizeof(std::uint32_t),
					.data = meshletTriangleIndices.data(),
					.finalState = ResourceState::ShaderRead,
				});
			}
			upload->Flush();
			device_->AcquireUploadedBuffers(upload->TakePendingBufferAcquires());

			if (!gpuMeshlets.empty()) {
				const BindingPoolSize poolSize{
					.type = BindingType::StorageBuffer,
					.count = 4,
				};
				gpuModel.meshletBindingPool = device_->CreateBindingPool({
					.poolSizes = &poolSize,
					.poolSizeCount = 1,
					.maxSets = 1,
					.debugName = "Iryven model meshlet binding pool",
				});
				gpuModel.meshletBindingSet = device_->AllocateBindingSet({
					.pool = gpuModel.meshletBindingPool,
					.layout = meshletBindingLayout_,
					.debugName = "Iryven model meshlet binding set",
				});
				const std::array infos{
					BindingBufferInfo{.buffer = gpuModel.vertexStorageBuffer, .range = vertexBufferSize},
					BindingBufferInfo{.buffer = gpuModel.meshletBuffer, .range = gpuMeshlets.size() * sizeof(GpuMeshlet)},
					BindingBufferInfo{.buffer = gpuModel.meshletVertexIndexBuffer, .range = meshletVertexIndices.size() * sizeof(std::uint32_t)},
					BindingBufferInfo{.buffer = gpuModel.meshletTriangleIndexBuffer, .range = meshletTriangleIndices.size() * sizeof(std::uint32_t)},
				};
				for (std::uint32_t binding = 0; binding < infos.size(); ++binding) {
					device_->UpdateBindingSet({
						.dstSet = gpuModel.meshletBindingSet,
						.binding = binding,
						.type = BindingType::StorageBuffer,
						.bufferInfo = &infos[binding],
					});
				}
			}
		} catch (...) {
			DestroyGpuModel(gpuModel);
			throw;
		}
		try {
			gpuModel.samplers.reserve(model->textureRegistry.samplers.size());
			const auto convertFilter = [](TextureFilter filter) {
				switch (filter) {
				case TextureFilter::Nearest:
				case TextureFilter::NearestMipmapNearest:
				case TextureFilter::NearestMipmapLinear:
					return Velos::RHI::Filter::Nearest;
				default:
					return Velos::RHI::Filter::Linear;
				}
			};
			const auto convertAddressMode = [](TextureWrap wrap) {
				return wrap == TextureWrap::ClampToEdge
					? Velos::RHI::SamplerAddressMode::ClampToEdge
					: Velos::RHI::SamplerAddressMode::Repeat;
			};
			for (const TextureSampler& sampler : model->textureRegistry.samplers) {
				gpuModel.samplers.push_back(device_->CreateSampler({
					.minFilter = convertFilter(sampler.minFilter),
					.magFilter = convertFilter(sampler.magFilter),
					.addressU = convertAddressMode(sampler.wrapU),
					.addressV = convertAddressMode(sampler.wrapV),
					.addressW = Velos::RHI::SamplerAddressMode::Repeat,
					.debugName = "Iryven glTF sampler"
				}));
			}

			gpuModel.textureImages.reserve(model->textureRegistry.textures.size());
			gpuModel.textureViews.reserve(model->textureRegistry.textures.size());
			std::size_t uploadSize = 0;
			for (const RegisteredTexture& registered : model->textureRegistry.textures) {
				uploadSize += registered.texture->pixels.size();
				const auto format = registered.texture->colorSpace == TextureColorSpace::SRGB
					? Velos::RHI::Format::RGBA8_SRGB : Velos::RHI::Format::RGBA8_UNORM;
				gpuModel.textureImages.push_back(device_->CreateImage({
					.width = registered.texture->width,
					.height = registered.texture->height,
					.format = format,
					.usage = Velos::RHI::ImageUsage::TransferDst | Velos::RHI::ImageUsage::Sampled,
					.debugName = "Iryven glTF texture image"
				}));
			}
			if (!gpuModel.textureImages.empty()) {
				// UploadContext aligns each image allocation to 16 bytes.
				uploadSize += 15 * (gpuModel.textureImages.size() - 1);
				auto upload = device_->CreateUploadContext(uploadSize);
				upload->Begin();
				for (std::size_t index = 0; index < gpuModel.textureImages.size(); ++index) {
					const Texture& texture = *model->textureRegistry.textures[index].texture;
					upload->UploadImage({
						.dstImage = gpuModel.textureImages[index],
						.finalLayout = Velos::RHI::ImageLayout::ShaderReadOnly,
						.width = texture.width,
						.height = texture.height
					}, texture.pixels.data(), texture.pixels.size());
				}
				upload->Flush();
				device_->AcquireUploadedImages(upload->TakePendingImageAcquires());
			}
			for (std::size_t index = 0; index < gpuModel.textureImages.size(); ++index) {
				const Texture& texture = *model->textureRegistry.textures[index].texture;
				const auto format = texture.colorSpace == TextureColorSpace::SRGB
					? Velos::RHI::Format::RGBA8_SRGB : Velos::RHI::Format::RGBA8_UNORM;
				gpuModel.textureViews.push_back(device_->CreateImageView({
					.image = gpuModel.textureImages[index],
					.format = format,
					.debugName = "Iryven glTF texture view"
				}));
			}

			gpuModel.bindlessTextureIndices.reserve(gpuModel.textureViews.size());
			for (std::size_t index = 0; index < gpuModel.textureViews.size(); ++index) {
				const RegisteredTexture& registered =
					model->textureRegistry.textures[index];
				gpuModel.bindlessTextureIndices.push_back(
					bindlessTextureManager_->Register(
						gpuModel.textureViews[index],
						gpuModel.samplers[registered.samplerIndex]));
			}
		}
		catch (...) {
			for (const auto index : gpuModel.bindlessTextureIndices) {
				bindlessTextureManager_->Release(index, completedSubmissionSerial_);
			}
			bindlessTextureManager_->CollectGarbage(completedSubmissionSerial_);
			DestroyGpuModel(gpuModel);
			throw;
		}
		auto [entry, inserted] = models_.emplace(model.get(), std::move(gpuModel));
		return &entry->second;
	}

	void Renderer::CollectUnusedMeshes()
	{
		for (auto it = meshes_.begin(); it != meshes_.end();) {
			if (!it->second.source.expired()) {
				++it;
				continue;
			}

			GpuMesh& mesh = it->second;
			if (mesh.meshletBindingPool) device_->DestroyBindingPool(mesh.meshletBindingPool);
			if (mesh.meshletTriangleIndexBuffer) device_->DestroyBuffer(mesh.meshletTriangleIndexBuffer);
			if (mesh.meshletVertexIndexBuffer) device_->DestroyBuffer(mesh.meshletVertexIndexBuffer);
			if (mesh.meshletBuffer) device_->DestroyBuffer(mesh.meshletBuffer);
			if (mesh.vertexStorageBuffer) device_->DestroyBuffer(mesh.vertexStorageBuffer);
			device_->DestroyBuffer(mesh.indexBuffer);
			device_->DestroyBuffer(mesh.vertexBuffer);
			it = meshes_.erase(it);
		}
		for (auto it = models_.begin(); it != models_.end();) {
			if (!it->second.source.expired()) { ++it; continue; }
			for (const auto index : it->second.bindlessTextureIndices) {
				bindlessTextureManager_->Release(index, lastSubmittedSerial_);
			}
			retiredModels_.push_back({
				.resources = std::move(it->second),
				.retirementSubmission = lastSubmittedSerial_
			});
			it = models_.erase(it);
		}
	}

	void Renderer::CollectRetiredModels(std::uint64_t completedSubmission)
	{
		auto it = retiredModels_.begin();
		while (it != retiredModels_.end()) {
			if (it->retirementSubmission > completedSubmission) {
				++it;
				continue;
			}
			DestroyGpuModel(it->resources);
			it = retiredModels_.erase(it);
		}
	}

	void Renderer::CollectRetiredCloths(std::uint64_t completedSubmission)
	{
		auto it = retiredCloths_.begin();
		while (it != retiredCloths_.end()) {
			if (it->retirementSubmission > completedSubmission) {
				++it;
				continue;
			}
			DestroyGpuCloth(it->resources);
			it = retiredCloths_.erase(it);
		}
	}

	void Renderer::DestroyGpuModel(GpuModel& model)
	{
		if (model.meshletBindingPool) device_->DestroyBindingPool(model.meshletBindingPool);
		for (const auto sampler : model.samplers) device_->DestroySampler(sampler);
		for (const auto view : model.textureViews) device_->DestroyImageView(view);
		for (const auto image : model.textureImages) device_->DestroyImage(image);
		if (model.meshletTriangleIndexBuffer) device_->DestroyBuffer(model.meshletTriangleIndexBuffer);
		if (model.meshletVertexIndexBuffer) device_->DestroyBuffer(model.meshletVertexIndexBuffer);
		if (model.meshletBuffer) device_->DestroyBuffer(model.meshletBuffer);
		if (model.vertexStorageBuffer) device_->DestroyBuffer(model.vertexStorageBuffer);
		if (model.indexBuffer) device_->DestroyBuffer(model.indexBuffer);
		if (model.vertexBuffer) device_->DestroyBuffer(model.vertexBuffer);
		model = {};
	}

	void Renderer::DestroyGpuCloth(GpuCloth& cloth)
	{
		if (cloth.bindingPool) device_->DestroyBindingPool(cloth.bindingPool);
		if (cloth.indexBuffer) device_->DestroyBuffer(cloth.indexBuffer);
		for (BufferHandle buffer : cloth.particleBuffers) {
			if (buffer) device_->DestroyBuffer(buffer);
		}
		cloth = {};
	}

	void Renderer::CollectUnusedFonts()
	{
		for (auto it = fonts_.begin(); it != fonts_.end();) {
			if (!it->second.source.expired()) {
				++it;
				continue;
			}

			device_->DestroySampler(it->second.atlasSampler);
			device_->DestroyImageView(it->second.atlasView);
			device_->DestroyImage(it->second.atlasImage);
			it = fonts_.erase(it);
		}
	}

	void Renderer::CreateBufferResources()
	{
		const Velos::RHI::BindingPoolSize fontPoolSize{
			.type = Velos::RHI::BindingType::CombinedImageSampler,
			.count = 256
		};
		fontBindingPool_ = device_->CreateBindingPool({
			.poolSizes = &fontPoolSize,
			.poolSizeCount = 1,
			.maxSets = 256,
			.debugName = "Iryven font binding pool"
		});

		constexpr std::uint64_t gpuLightSize = sizeof(glm::vec4) * 4;
		constexpr std::uint64_t lightsBufferSize = sizeof(glm::uvec4) + gpuLightSize * k_MaxLightSources;
		const Velos::RHI::BindingPoolSize poolSizes[]{
			{
				.type = Velos::RHI::BindingType::UniformBuffer,
				.count = k_FramesInFlight * 2
			},
			{
				.type = Velos::RHI::BindingType::StorageBuffer,
				.count = k_FramesInFlight
			}
		};
		lightsBindingPool_ = device_->CreateBindingPool({
			.poolSizes = poolSizes,
			.poolSizeCount = 2,
			.maxSets = k_FramesInFlight,
			.debugName = "Lights Binding Pool"
		});

		for (std::uint32_t frameIndex = 0; frameIndex < k_FramesInFlight; ++frameIndex) {
			auto& frame = lightingFrames_[frameIndex];
			frame.lightBuffer = CreateUploadBackedBuffer(
				lightsBufferSize,
				Velos::RHI::BufferUsage::Uniform,
				"Frame Lights Buffer",
				"Frame Lights Upload Buffer");
			frame.frameDataBuffer = CreateUploadBackedBuffer(
				sizeof(GpuFrameData),
				Velos::RHI::BufferUsage::Uniform,
				"Frame Data Buffer",
				"Frame Data Upload Buffer");
			frame.materialBuffer = CreateUploadBackedBuffer(
				sizeof(GpuMaterial) * k_MaxMaterials,
				Velos::RHI::BufferUsage::Storage,
				"Frame Material Buffer",
				"Frame Material Upload Buffer");
			frame.lightBindingSet = device_->AllocateBindingSet({
				.pool = lightsBindingPool_,
				.layout = lightsBindingLayout_,
				.debugName = "Frame Lights Binding Set"
			});
			const Velos::RHI::BindingBufferInfo bufferInfo{
				.buffer = frame.lightBuffer.gpuBuffer,
				.offset = 0,
				.range = lightsBufferSize
			};
			device_->UpdateBindingSet({
				.dstSet = frame.lightBindingSet,
				.binding = 0,
				.type = Velos::RHI::BindingType::UniformBuffer,
				.bufferInfo = &bufferInfo
			});
			const Velos::RHI::BindingBufferInfo frameDataBufferInfo{
				.buffer = frame.frameDataBuffer.gpuBuffer,
				.offset = 0,
				.range = sizeof(GpuFrameData)
			};
			device_->UpdateBindingSet({
				.dstSet = frame.lightBindingSet,
				.binding = 1,
				.type = Velos::RHI::BindingType::UniformBuffer,
				.bufferInfo = &frameDataBufferInfo
			});
			const Velos::RHI::BindingBufferInfo materialBufferInfo{
				.buffer = frame.materialBuffer.gpuBuffer,
				.offset = 0,
				.range = sizeof(GpuMaterial) * k_MaxMaterials
			};
			device_->UpdateBindingSet({
				.dstSet = frame.lightBindingSet,
				.binding = 2,
				.type = Velos::RHI::BindingType::StorageBuffer,
				.bufferInfo = &materialBufferInfo
			});
		}
	}

	void Renderer::DestroyBufferResources()
	{
		for (auto& buffers : textVertexBuffers_) {
			for (auto& buffer : buffers) {
				DestroyUploadBackedBuffer(buffer);
			}
			buffers.clear();
		}
		if (fontBindingPool_) {
			device_->DestroyBindingPool(fontBindingPool_);
			fontBindingPool_ = {};
		}
		if (lightsBindingPool_.IsValid()) {
			device_->DestroyBindingPool(lightsBindingPool_);
			lightsBindingPool_ = {};
		}
		for (auto& frame : lightingFrames_) {
			frame.lightBindingSet = {};
			DestroyUploadBackedBuffer(frame.lightBuffer);
			DestroyUploadBackedBuffer(frame.frameDataBuffer);
			DestroyUploadBackedBuffer(frame.materialBuffer);
		}
		fontBindingLayout_ = {};
		lightsBindingLayout_ = {};
		clothSimulationBindingLayout_ = {};
		clothRenderBindingLayout_ = {};
		for (const auto layout : clothComputeGeneratedLayout_.ownedSetLayouts) {
			device_->DestroyBindingLayout(layout);
		}
		clothComputeGeneratedLayout_ = {};
		for (const auto layout : clothGraphicsGeneratedLayout_.ownedSetLayouts) {
			device_->DestroyBindingLayout(layout);
		}
		clothGraphicsGeneratedLayout_ = {};
		for (const auto layout : textGeneratedLayout_.ownedSetLayouts) {
			device_->DestroyBindingLayout(layout);
		}
		textGeneratedLayout_ = {};
		for (const auto layout : gltfGeneratedLayout_.ownedSetLayouts) {
			device_->DestroyBindingLayout(layout);
		}
		gltfGeneratedLayout_ = {};
	}

	void Renderer::DestroyMeshResources()
	{
		for (const auto& [source, mesh] : meshes_) {
			if (mesh.meshletBindingPool) device_->DestroyBindingPool(mesh.meshletBindingPool);
			if (mesh.meshletTriangleIndexBuffer) device_->DestroyBuffer(mesh.meshletTriangleIndexBuffer);
			if (mesh.meshletVertexIndexBuffer) device_->DestroyBuffer(mesh.meshletVertexIndexBuffer);
			if (mesh.meshletBuffer) device_->DestroyBuffer(mesh.meshletBuffer);
			if (mesh.vertexStorageBuffer) device_->DestroyBuffer(mesh.vertexStorageBuffer);
			device_->DestroyBuffer(mesh.indexBuffer);
			device_->DestroyBuffer(mesh.vertexBuffer);
		}
		meshes_.clear();
		for (auto& [source, model] : models_) {
			DestroyGpuModel(model);
		}
		models_.clear();
		for (auto& retired : retiredModels_) DestroyGpuModel(retired.resources);
		retiredModels_.clear();
	}

	void Renderer::DestroyFontResources()
	{
		for (const auto& [source, font] : fonts_) {
			device_->DestroySampler(font.atlasSampler);
			device_->DestroyImageView(font.atlasView);
			device_->DestroyImage(font.atlasImage);
		}
		fonts_.clear();
	}

	void Renderer::CreateBindlessResources()
	{
		bindlessTextureManager_ = std::make_unique<BindlessTextureManager>(
			*device_, k_MaxBindlessTextures);

		// Magenta-Black checkerboard texture for missing textures
		missingTextureImage_ = device_->CreateImage({
			.width = 2,
			.height = 2,
			.format = Velos::RHI::Format::RGBA8_SRGB,
			.usage = Velos::RHI::ImageUsage::TransferDst | Velos::RHI::ImageUsage::Sampled,
			.debugName = "Iryven missing texture image"
			});

		{
			auto upload = device_->CreateUploadContext(16);
			upload->Begin();
			upload->UploadImage({
				.dstImage = missingTextureImage_,
				.finalLayout = Velos::RHI::ImageLayout::ShaderReadOnly,
				.width = 2,
				.height = 2
				}, std::array<std::uint8_t, 16>{
					255, 0, 255, 255, 0, 0, 0, 255,
					0, 0, 0, 255, 255, 0, 255, 255
				}.data(), 16);
			upload->Flush();
			device_->AcquireUploadedImages(upload->TakePendingImageAcquires());
		}

		missingTextureView_ = device_->CreateImageView({
			.image = missingTextureImage_,
			.format = Velos::RHI::Format::RGBA8_SRGB,
			.aspect = Velos::RHI::ImageAspect::Color,
			.debugName = "Iryven missing texture view"
			});

		missingTextureSampler_ = device_->CreateSampler({
			.minFilter = Velos::RHI::Filter::Linear,
			.magFilter = Velos::RHI::Filter::Linear,
			.addressU = Velos::RHI::SamplerAddressMode::Repeat,
			.addressV = Velos::RHI::SamplerAddressMode::Repeat,
			.addressW = Velos::RHI::SamplerAddressMode::Repeat,
			.debugName = "Iryven missing texture sampler"
			});


		missingTextureIndex_ = bindlessTextureManager_->Register(
			missingTextureView_, missingTextureSampler_);
		if (missingTextureIndex_ != 0) {
			throw std::logic_error("Missing texture must occupy bindless slot 0");
		}
	}

	void Renderer::DestroyBindlessResources()
	{
		bindlessTextureManager_.reset();
		if (missingTextureSampler_) {
			device_->DestroySampler(missingTextureSampler_);
			missingTextureSampler_ = {};
		}
		if (missingTextureView_) {
			device_->DestroyImageView(missingTextureView_);
			missingTextureView_ = {};
		}
		if (missingTextureImage_) {
			device_->DestroyImage(missingTextureImage_);
			missingTextureImage_ = {};
		}
	}

}
