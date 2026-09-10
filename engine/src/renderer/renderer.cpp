#include "renderer.h"
#include "imgui_renderer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <stdexcept>
#include <utility>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/vector_uint4.hpp>
#include <glm/trigonometric.hpp>

#include <rhi/pipeline.h>
#include <shader/shader_compiler.h>
#include <rhi/upload_context.h>

namespace {

using CpuClock = std::chrono::steady_clock;

[[nodiscard]] float ElapsedMilliseconds(CpuClock::time_point start)
{
	return std::chrono::duration<float, std::milli>(CpuClock::now() - start)
		.count();
}

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
	};
	static_assert(sizeof(GpuMaterial) == 80);

	class Renderer::OpaquePass final : public FrameGraphRenderPass {
	public:
		explicit OpaquePass(Renderer& renderer) : renderer_(renderer) {}

		void AddUI() override {}

		void PreRender(
			Velos::RHI::ICommandList& commands, const RenderScene& scene) override
		{
			const auto lightsStart = CpuClock::now();
			renderer_.UploadLights(commands, scene.lights);
			renderer_.cpuTimings_.uploadLightsMs += ElapsedMilliseconds(lightsStart);

			const auto materialsStart = CpuClock::now();
			renderer_.UploadMaterials(commands, scene.objects);
			renderer_.cpuTimings_.uploadMaterialsMs +=
				ElapsedMilliseconds(materialsStart);
			hasCamera_ = scene.camera.has_value();
			if (hasCamera_) {
				frameData_ = renderer_.BuildFrameData(*scene.camera);
				const auto frameDataStart = CpuClock::now();
				renderer_.UploadFrameData(commands, frameData_);
				renderer_.cpuTimings_.uploadFrameDataMs +=
					ElapsedMilliseconds(frameDataStart);
			}
		}

		void Render(
			Velos::RHI::ICommandList& commands, const RenderScene& scene) override
		{
			if (hasCamera_) {
				for (const RenderObject& object : scene.objects) {
					renderer_.DrawObject(commands, object, frameData_);
				}
			}
			for (const RenderText& text : scene.texts) {
				renderer_.DrawText(commands, text);
			}
		}

		void OnResize(Velos::RHI::IDevice&, std::uint32_t, std::uint32_t) override {}

	private:
		Renderer& renderer_;
		FrameData frameData_{};
		bool hasCamera_ = false;
	};

	Renderer::Renderer(Window& window, AssetUploadQueue& assetUploads)
		: window_(window), assetUploads_(assetUploads)
	{
		device_.reset(Velos::RHI::CreateDevice({
			.graphicsAPI = Velos::RHI::GraphicsAPI::Vulkan,
			.enableValidation = false,
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
		CreatePipelineResources();
		CreateBufferResources();

		frameGraphBuilder_.Init(*device_);
		frameGraph_.Init(frameGraphBuilder_);
		opaquePass_ = std::make_unique<OpaquePass>(*this);
		frameGraphBuilder_.RegisterRenderPass("opaque", *opaquePass_);

		const Color clearColor = Color::CornflowerBlue;
		frameGraph_.AddNode({
			.name = "opaque",
			.outputs = {
				{
					.type = FrameGraphResourceType::Attachment,
					.info = FrameGraphTextureInfo{
						.width = static_cast<std::uint32_t>(width),
						.height = static_cast<std::uint32_t>(height),
						.format = Velos::RHI::Format::BGRA8_UNORM,
						.usage = Velos::RHI::ImageUsage::ColorAttachment,
						.loadOp = RenderPassOperation::Clear,
						.clearColor = {
							clearColor.R(), clearColor.G(), clearColor.B(), clearColor.A() },
					},
					.external = true,
					.name = "backbuffer",
				},
				{
					.type = FrameGraphResourceType::Attachment,
					.info = FrameGraphTextureInfo{
						.width = static_cast<std::uint32_t>(width),
						.height = static_cast<std::uint32_t>(height),
						.format = Velos::RHI::Format::D32_FLOAT,
						.usage = Velos::RHI::ImageUsage::DepthStencil,
						.loadOp = RenderPassOperation::Clear,
					},
					.external = true,
					.name = "depth",
				},
			},
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
		opaquePass_.reset();
		DestroyPipelineResources();
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
		const FrameData& frameData)
	{
		if (!frameActive_) {
			throw std::logic_error("Renderer::DrawObject called outside an active frame");
		}

		GpuMesh* mesh = object.mesh ? ResolveOrCreateMesh(object.mesh) : nullptr;
		GpuModel* model = object.model ? ResolveOrCreateModel(object.model) : nullptr;
		if (!mesh && !model) return;

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
			.materialIndex = materialSlot == materialSlots_.end() ? 0u : materialSlot->second
		};

		commands.BindPipeline(gltfPipeline_);
		commands.SetBindings(
			gltfPipeline_, 0,
			lightingFrames_.at(frame_.frameIndex).lightBindingSet);
		commands.SetBindings(
			gltfPipeline_, 1, bindlessTextureManager_->BindingSet());
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

	void Renderer::UploadFrameData(
		Velos::RHI::ICommandList& commands, const FrameData& frameData)
	{
		struct alignas(16) GpuFrameData {
			glm::mat4 view;
			glm::mat4 projection;
			glm::mat4 viewProjection;
			glm::vec4 cameraPosition;
		};
		const GpuFrameData gpuFrameData{
			.view = frameData.view,
			.projection = frameData.projection,
			.viewProjection = frameData.viewProjection,
			.cameraPosition = glm::vec4(frameData.cameraPosition, 1.0f)
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
		const std::vector<RenderObject>& objects)
	{
		materialSlots_.clear();
		std::vector<GpuMaterial> materials;
		materials.reserve(std::min<std::size_t>(objects.size() + 1, k_MaxMaterials));
		materials.emplace_back(); // Slot 0 is the default white material.

		for (const RenderObject& object : objects) {
			if (!object.material) continue;
			const MaterialSlotKey key{
				.model = object.model.get(),
				.material = object.material.get()
			};
			if (materialSlots_.contains(key)) continue;
			if (materials.size() >= k_MaxMaterials)
				throw std::runtime_error("Renderer material table exceeded its 1024 material capacity");

			GpuModel* gpuModel = object.model
				? ResolveOrCreateModel(object.model) : nullptr;
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
			if (object.material->baseColorTexture != InvalidTextureIndex) textureFlags |= 1u;
			if (object.material->metallicRoughnessTexture != InvalidTextureIndex) textureFlags |= 2u;
			if (object.material->normalTexture != InvalidTextureIndex) textureFlags |= 4u;
			if (object.material->occlusionTexture != InvalidTextureIndex) textureFlags |= 8u;
			if (object.material->emissiveTexture != InvalidTextureIndex) textureFlags |= 16u;
			materials.push_back(GpuMaterial{
				.baseColorFactor = object.material->baseColor.Vector(),
				.emissiveFactor = object.material->emissive.Vector(),
				.metallicRoughnessNormal = glm::vec4(
					object.material->metallic,
					object.material->roughness,
					object.material->normalScale,
					object.material->occlusionStrength),
				.textureIndices0 = glm::uvec4(
					resolveTextureIndex(object.material->baseColorTexture),
					resolveTextureIndex(object.material->metallicRoughnessTexture),
					resolveTextureIndex(object.material->normalTexture),
					resolveTextureIndex(object.material->occlusionTexture)),
				.textureIndices1 = glm::uvec4(
					resolveTextureIndex(object.material->emissiveTexture),
					textureFlags, 0u, 0u),
			});
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
		const auto gltfReflection =
			Velos::ShaderCompiler::MergeShaderReflection(gltfReflections);
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

		gltfPipeline_ = device_->CreateGraphicsPipeline({
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
		});

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
	}

	void Renderer::CreateDepthResources(std::uint32_t width, std::uint32_t height)
	{
		depthImage_ = device_->CreateImage({
			.width = width,
			.height = height,
			.format = Velos::RHI::Format::D32_FLOAT,
			.usage = Velos::RHI::ImageUsage::DepthStencil,
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

		const std::size_t vertexBufferSize = mesh->vertices.size() * sizeof(Vertex);
		const std::size_t indexBufferSize =
			mesh->indices.size() * sizeof(std::uint32_t);
		const auto vertexBuffer = device_->CreateBuffer({
			.size = vertexBufferSize,
			.usage = Velos::RHI::BufferUsage::Vertex |
				Velos::RHI::BufferUsage::TransferDst,
			.memoryUsage = Velos::RHI::MemoryUsage::GPUOnly,
			.debugName = "Iryven mesh vertex buffer"
		});

		Velos::RHI::BufferHandle indexBuffer;
		try {
			indexBuffer = device_->CreateBuffer({
				.size = indexBufferSize,
				.usage = Velos::RHI::BufferUsage::Index |
					Velos::RHI::BufferUsage::TransferDst,
				.memoryUsage = Velos::RHI::MemoryUsage::GPUOnly,
				.debugName = "Iryven mesh index buffer"
			});

			// The second staging allocation may need up to 15 bytes of padding.
			auto upload = device_->CreateUploadContext(
				vertexBufferSize + indexBufferSize + 15);
			upload->Begin();
			upload->UploadBuffer({
				.dstBuffer = vertexBuffer,
				.size = vertexBufferSize,
				.data = mesh->vertices.data(),
				.finalState = Velos::RHI::ResourceState::VertexBuffer,
			});
			upload->UploadBuffer({
				.dstBuffer = indexBuffer,
				.size = indexBufferSize,
				.data = mesh->indices.data(),
				.finalState = Velos::RHI::ResourceState::IndexBuffer,
			});
			upload->Flush();
			device_->AcquireUploadedBuffers(upload->TakePendingBufferAcquires());
		}
		catch (...) {
			device_->DestroyBuffer(vertexBuffer);
			throw;
		}

		GpuMesh gpuMesh{
			.source = mesh,
			.vertexBuffer = vertexBuffer,
			.indexBuffer = indexBuffer,
			.indexCount = static_cast<std::uint32_t>(mesh->indices.size())
		};

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
		const auto vertexBuffer = device_->CreateBuffer({
			.size = vertexBufferSize,
			.usage = Velos::RHI::BufferUsage::Vertex |
				Velos::RHI::BufferUsage::TransferDst,
			.memoryUsage = Velos::RHI::MemoryUsage::GPUOnly,
			.debugName = "Iryven model vertex buffer"
		});
		Velos::RHI::BufferHandle indexBuffer;
		try {
			indexBuffer = device_->CreateBuffer({
				.size = indexBufferSize,
				.usage = Velos::RHI::BufferUsage::Index |
					Velos::RHI::BufferUsage::TransferDst,
				.memoryUsage = Velos::RHI::MemoryUsage::GPUOnly,
				.debugName = "Iryven model index buffer"
			});

			// The second staging allocation may need up to 15 bytes of padding.
			auto upload = device_->CreateUploadContext(
				vertexBufferSize + indexBufferSize + 15);
			upload->Begin();
			upload->UploadBuffer({
				.dstBuffer = vertexBuffer,
				.size = vertexBufferSize,
				.data = model->vertices.data(),
				.finalState = Velos::RHI::ResourceState::VertexBuffer,
			});
			upload->UploadBuffer({
				.dstBuffer = indexBuffer,
				.size = indexBufferSize,
				.data = model->indices.data(),
				.finalState = Velos::RHI::ResourceState::IndexBuffer,
			});
			upload->Flush();
			device_->AcquireUploadedBuffers(upload->TakePendingBufferAcquires());
		} catch (...) {
			device_->DestroyBuffer(vertexBuffer);
			throw;
		}
		GpuModel gpuModel{
			.source = model,
			.vertexBuffer = vertexBuffer,
			.indexBuffer = indexBuffer
		};
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
			for (const auto view : gpuModel.textureViews) device_->DestroyImageView(view);
			for (const auto image : gpuModel.textureImages) device_->DestroyImage(image);
			for (const auto sampler : gpuModel.samplers) device_->DestroySampler(sampler);
			device_->DestroyBuffer(indexBuffer);
			device_->DestroyBuffer(vertexBuffer);
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

			device_->DestroyBuffer(it->second.indexBuffer);
			device_->DestroyBuffer(it->second.vertexBuffer);
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

	void Renderer::DestroyGpuModel(GpuModel& model)
	{
		for (const auto sampler : model.samplers) device_->DestroySampler(sampler);
		for (const auto view : model.textureViews) device_->DestroyImageView(view);
		for (const auto image : model.textureImages) device_->DestroyImage(image);
		device_->DestroyBuffer(model.indexBuffer);
		device_->DestroyBuffer(model.vertexBuffer);
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
				sizeof(glm::mat4) * 3 + sizeof(glm::vec4),
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
				.range = sizeof(glm::mat4) * 3 + sizeof(glm::vec4)
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
