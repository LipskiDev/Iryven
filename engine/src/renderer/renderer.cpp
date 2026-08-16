#include "renderer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/vector_uint4.hpp>
#include <glm/trigonometric.hpp>

#include <rhi/pipeline.h>
#include <shader/shader_compiler.h>

namespace Iryven {

	Renderer::Renderer(Window& window) : window_(window)
	{
		device_.reset(Velos::RHI::CreateDevice({
			.graphicsAPI = Velos::RHI::GraphicsAPI::Vulkan,
			.enableValidation = true,
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
		CreateBufferResources();
		CreatePipelineResources();
	}

	Renderer::~Renderer()
	{
		if (!device_) {
			return;
		}

		device_->WaitIdle();
		DestroyMeshResources();
		DestroyPipelineResources();
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

		BeginScenePass();

		UploadLights(renderScene.lights);

		if (renderScene.camera) {
			const FrameData frameData = BuildFrameData(*renderScene.camera);
			UploadFrameData(frameData);
			for (const RenderObject& object : renderScene.objects) {
				DrawObject(object, frameData);
			}
		}
	}

	bool Renderer::BeginFrame()
	{
		CollectUnusedMeshes();

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

		auto& commands = device_->GetCommandList();
		commands.Begin();
		commands.Barrier({
			.image = frame_.backbufferImage,
			.newLayout = Velos::RHI::ImageLayout::ColorAttachment,
			.aspect = Velos::RHI::ImageAspect::Color,
		});
		commands.Barrier({
			.image = depthImage_,
			.newLayout = Velos::RHI::ImageLayout::DepthAttachment,
			.aspect = Velos::RHI::ImageAspect::Depth,
		});

		frameActive_ = true;
		return true;
	}

	void Renderer::BeginScenePass()
	{
		if (!frameActive_) {
			throw std::logic_error("Renderer::BeginScenePass called outside an active frame");
		}

		const Color clearColor = Color::CornflowerBlue;

		const auto dimensions = device_->GetSwapchainDimensions();
		const Velos::RHI::ColorAttachmentDesc attachment{
			.view = frame_.backbuffer,
			.loadOp = Velos::RHI::LoadOp::Clear,
			.storeOp = Velos::RHI::StoreOp::Store,
			.clearValue = {
				clearColor.R(),
				clearColor.G(),
				clearColor.B(),
				clearColor.A(),
			},
		};
		const Velos::RHI::DepthAttachmentDesc depthAttachment{
			.view = depthView_,
			.loadOp = Velos::RHI::LoadOp::Clear,
			.storeOp = Velos::RHI::StoreOp::Store,
			.clearDepth = 1.0f,
			.clearStencil = 0
		};

		auto& commands = device_->GetCommandList();
		commands.BeginRendering({
			.renderArea = {{0, 0}, dimensions},
			.colorAttachments = &attachment,
			.colorAttachmentCount = 1,
			.depthAttachment = &depthAttachment,
		});
		commands.SetViewport({
			.x = 0.0f,
			.y = 0.0f,
			.width = static_cast<float>(dimensions.width),
			.height = static_cast<float>(dimensions.height),
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		});
		commands.SetScissor({
			.offset = {0, 0},
			.extent = dimensions,
		});

		scenePassActive_ = true;
	}

	void Renderer::DrawObject(
		const RenderObject& object,
		const FrameData& frameData)
	{
		if (!scenePassActive_) {
			throw std::logic_error("Renderer::DrawObject called outside an active scene pass");
		}

		GpuMesh* mesh = ResolveOrCreateMesh(object.mesh);
		if (!mesh) {
			return;
		}

		struct DrawConstants {
			glm::mat4 model;
			glm::vec4 baseColor;
		};
		const DrawConstants drawConstants{
			.model = object.transform,
			.baseColor = object.material ? object.material->baseColor.Vector() : Color::White.Vector()
		};

		auto& commands = device_->GetCommandList();
		commands.BindPipeline(vertexColorPipeline_);
		commands.SetBindings(
			vertexColorPipeline_, 0,
			lightingFrames_.at(frame_.frameIndex).lightBindingSet);
		commands.PushConstants(
			Velos::RHI::ShaderStage::Vertex | Velos::RHI::ShaderStage::Fragment,
			0,
			static_cast<Velos::u32>(sizeof(drawConstants)),
			&drawConstants);
		commands.BindVertexBuffer(0, mesh->vertexBuffer);
		commands.BindIndexBuffer(mesh->indexBuffer, Velos::RHI::IndexType::U32);
		commands.DrawIndexed(mesh->indexCount);
	}

	void Renderer::UploadLights(const std::vector<RenderLight>& lights)
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

		device_->GetCommandList().UpdateBuffer({
			.buffer = lightingFrames_.at(frame_.frameIndex).lightBuffer,
			.offset = 0,
			.data = &gpuLights,
			.size = sizeof(gpuLights)
		});
	}

	void Renderer::UploadFrameData(const FrameData& frameData)
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
		device_->GetCommandList().UpdateBuffer({
			.buffer = lightingFrames_.at(frame_.frameIndex).frameDataBuffer,
			.offset = 0,
			.data = &gpuFrameData,
			.size = sizeof(gpuFrameData)
		});
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
		// GLM's projection uses an upward-positive clip-space Y axis. Vulkan's
		// framebuffer coordinates point downward, so flip Y at the projection.
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
		if (scenePassActive_) {
			commands.EndRendering();
			scenePassActive_ = false;
		}

		commands.Barrier({
			.image = frame_.backbufferImage,
			.newLayout = Velos::RHI::ImageLayout::Present,
			.aspect = Velos::RHI::ImageAspect::Color,
		});
		commands.End();

		device_->SubmitAndPresent(swapchain_);
		frameActive_ = false;
	}

	void Renderer::CreatePipelineResources()
	{
		const auto vertexShader = Velos::ShaderCompiler::CompileFile({
			.path = "assets/shaders/internal/vertex_color.hlsl",
			.stage = Velos::RHI::ShaderStage::Vertex,
			.entryPoint = "VSMain",
			.language = Velos::ShaderSourceLanguage::HLSL,
		});
		const auto fragmentShader = Velos::ShaderCompiler::CompileFile({
			.path = "assets/shaders/internal/vertex_color.hlsl",
			.stage = Velos::RHI::ShaderStage::Fragment,
			.entryPoint = "PSMain",
			.language = Velos::ShaderSourceLanguage::HLSL,
		});

		vertexColorVertexShader_ = device_->CreateShader({
			.stage = Velos::RHI::ShaderStage::Vertex,
			.bytecode = vertexShader.spirv.data(),
			.bytecodeSize = static_cast<Velos::u64>(
				vertexShader.spirv.size() * sizeof(std::uint32_t)),
			.entryPoint = "VSMain",
			.reflection = vertexShader.reflection,
			.debugName = "Iryven vertex color vertex shader",
		});
		vertexColorFragmentShader_ = device_->CreateShader({
			.stage = Velos::RHI::ShaderStage::Fragment,
			.bytecode = fragmentShader.spirv.data(),
			.bytecodeSize = static_cast<Velos::u64>(
				fragmentShader.spirv.size() * sizeof(std::uint32_t)),
			.entryPoint = "PSMain",
			.reflection = fragmentShader.reflection,
			.debugName = "Iryven vertex color fragment shader",
		});

		const Velos::RHI::VertexBufferLayoutDesc vertexLayout{
			.stride = sizeof(Vertex),
			.inputRate = Velos::RHI::VertexInputRate::PerVertex,
			.attributes = {
				{
					.location = 0,
					.binding = 0,
					.format = Velos::RHI::VertexFormat::Float32x3,
					.offset = offsetof(Vertex, position)
				},
				{
					.location = 1,
					.binding = 0,
					.format = Velos::RHI::VertexFormat::Float32x3,
					.offset = offsetof(Vertex, normal)
				}
			}
		};

		vertexColorPipeline_ = device_->CreateGraphicsPipeline({
			.vertexShader = vertexColorVertexShader_,
			.fragmentShader = vertexColorFragmentShader_,
			.vertexLayouts = { vertexLayout },
			.layout = {
				.descriptorSetLayouts = &lightsBindingLayout_,
				.descriptorSetLayoutCount = 1
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
			.colorFormat = Velos::RHI::Format::BGRA8_UNORM,
			.debugName = "Iryven vertex color pipeline",
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
		if (vertexColorPipeline_) {
			device_->DestroyPipeline(vertexColorPipeline_);
			vertexColorPipeline_ = {};
		}
		if (vertexColorFragmentShader_) {
			device_->DestroyShader(vertexColorFragmentShader_);
			vertexColorFragmentShader_ = {};
		}
		if (vertexColorVertexShader_) {
			device_->DestroyShader(vertexColorVertexShader_);
			vertexColorVertexShader_ = {};
		}
	}

	Renderer::GpuMesh* Renderer::ResolveOrCreateMesh(
		const std::shared_ptr<const MeshData>& mesh)
	{
		if (!mesh || mesh->vertices.empty() || mesh->indices.empty()) {
			return nullptr;
		}
		const std::size_t vertexCount = mesh->vertices.size();
		if (mesh->indices.size() > std::numeric_limits<std::uint32_t>::max() ||
			std::ranges::any_of(mesh->indices, [vertexCount](std::uint32_t index) {
				return index >= vertexCount;
			})) {
			return nullptr;
		}

		if (const auto existing = meshes_.find(mesh.get()); existing != meshes_.end()) {
			return &existing->second;
		}

		const auto vertexBuffer = device_->CreateBuffer({
			.size = mesh->vertices.size() * sizeof(Vertex),
			.usage = Velos::RHI::BufferUsage::Vertex,
			.memoryUsage = Velos::RHI::MemoryUsage::CPUToGPU,
			.initialData = mesh->vertices.data(),
			.debugName = "Iryven mesh vertex buffer"
		});

		Velos::RHI::BufferHandle indexBuffer;
		try {
			indexBuffer = device_->CreateBuffer({
				.size = mesh->indices.size() * sizeof(std::uint32_t),
				.usage = Velos::RHI::BufferUsage::Index,
				.memoryUsage = Velos::RHI::MemoryUsage::CPUToGPU,
				.initialData = mesh->indices.data(),
				.debugName = "Iryven mesh index buffer"
			});
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
	}

	void Renderer::CreateBufferResources()
	{
		constexpr std::uint64_t gpuLightSize = sizeof(glm::vec4) * 4;
		constexpr std::uint64_t lightsBufferSize = sizeof(glm::uvec4) + gpuLightSize * k_MaxLightSources;
		const Velos::RHI::BindingDesc bindings[]{
			{
				.binding = 0,
				.type = Velos::RHI::BindingType::UniformBuffer,
				.count = 1,
				.visibility = Velos::RHI::ShaderStage::Fragment
			},
			{
				.binding = 1,
				.type = Velos::RHI::BindingType::UniformBuffer,
				.count = 1,
				.visibility = Velos::RHI::ShaderStage::Vertex | Velos::RHI::ShaderStage::Fragment
			}
		};
		lightsBindingLayout_ = device_->CreateBindingLayout({
			.bindings = bindings,
			.bindingCount = 2,
			.debugName = "Lights Binding Layout"
		});
		const Velos::RHI::BindingPoolSize poolSize{
			.type = Velos::RHI::BindingType::UniformBuffer,
			.count = k_FramesInFlight * 2
		};
		lightsBindingPool_ = device_->CreateBindingPool({
			.poolSizes = &poolSize,
			.poolSizeCount = 1,
			.maxSets = k_FramesInFlight,
			.debugName = "Lights Binding Pool"
		});

		for (std::uint32_t frameIndex = 0; frameIndex < k_FramesInFlight; ++frameIndex) {
			auto& frame = lightingFrames_[frameIndex];
			frame.lightBuffer = device_->CreateBuffer({
				.size = lightsBufferSize,
				.usage = Velos::RHI::BufferUsage::Uniform,
				.memoryUsage = Velos::RHI::MemoryUsage::CPUToGPU,
				.debugName = "Frame Lights Buffer"
			});
			frame.frameDataBuffer = device_->CreateBuffer({
				.size = sizeof(glm::mat4) * 3 + sizeof(glm::vec4),
				.usage = Velos::RHI::BufferUsage::Uniform,
				.memoryUsage = Velos::RHI::MemoryUsage::CPUToGPU,
				.debugName = "Frame Data Buffer"
			});
			frame.lightBindingSet = device_->AllocateBindingSet({
				.pool = lightsBindingPool_,
				.layout = lightsBindingLayout_,
				.debugName = "Frame Lights Binding Set"
			});
			const Velos::RHI::BindingBufferInfo bufferInfo{
				.buffer = frame.lightBuffer,
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
				.buffer = frame.frameDataBuffer,
				.offset = 0,
				.range = sizeof(glm::mat4) * 3 + sizeof(glm::vec4)
			};
			device_->UpdateBindingSet({
				.dstSet = frame.lightBindingSet,
				.binding = 1,
				.type = Velos::RHI::BindingType::UniformBuffer,
				.bufferInfo = &frameDataBufferInfo
			});
		}
	}

	void Renderer::DestroyBufferResources()
	{
		if (lightsBindingPool_.IsValid()) {
			device_->DestroyBindingPool(lightsBindingPool_);
			lightsBindingPool_ = {};
		}
		for (auto& frame : lightingFrames_) {
			frame.lightBindingSet = {};
			if (frame.lightBuffer.IsValid()) {
				device_->DestroyBuffer(frame.lightBuffer);
				frame.lightBuffer = {};
			}
			if (frame.frameDataBuffer.IsValid()) {
				device_->DestroyBuffer(frame.frameDataBuffer);
				frame.frameDataBuffer = {};
			}
		}
		if (lightsBindingLayout_.IsValid()) {
			device_->DestroyBindingLayout(lightsBindingLayout_);
			lightsBindingLayout_ = {};
		}
	}

	void Renderer::DestroyMeshResources()
	{
		for (const auto& [source, mesh] : meshes_) {
			device_->DestroyBuffer(mesh.indexBuffer);
			device_->DestroyBuffer(mesh.vertexBuffer);
		}
		meshes_.clear();
	}

}
