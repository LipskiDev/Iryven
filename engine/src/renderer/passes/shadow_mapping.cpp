#include "shadow_mapping.h"
#include "light_culling.h"
#include <shader/shader_compiler.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Iryven {

namespace {
	constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
	constexpr std::uint64_t kFnvPrime = 1099511628211ull;

	void HashBytes(std::uint64_t& hash, const void* data, std::size_t size)
	{
		const auto* bytes = static_cast<const unsigned char*>(data);
		for (std::size_t index = 0; index < size; ++index) {
			hash ^= bytes[index];
			hash *= kFnvPrime;
		}
	}

	template <typename T>
	void HashValue(std::uint64_t& hash, const T& value)
	{
		HashBytes(hash, &value, sizeof(value));
	}
}

Renderer::ShadowMappingPass::ShadowMappingPass(Renderer& renderer)
    : renderer_(renderer)
{
    try {
        CreateResources();
    } catch (...) {
        Destroy();
        throw;
    }
}

Renderer::ShadowMappingPass::~ShadowMappingPass()
{
    Destroy();
}

void Renderer::ShadowMappingPass::PreRender(
    ICommandList& commands, const RenderScene& scene)
{
	activeLightCount_ = 0;
	dirtyLightCount_ = 0;
	shadowFrameData_ = {};
	renderItems_.clear();
	dirtyAssignments_.clear();
	pendingSignatures_.clear();
	std::vector<ShadowCullObject> cullObjects;
	std::vector<std::uint64_t> objectSignatures;
	renderItems_.reserve(std::min<std::size_t>(scene.objects.size(), kMaxShadowObjects));
	cullObjects.reserve(renderItems_.capacity());
	objectSignatures.reserve(renderItems_.capacity());
	for (const auto& object : scene.objects) {
		if (renderItems_.size() >= kMaxShadowObjects) break;
		BindingSetHandle meshSet{};
		std::uint32_t meshletOffset = 0;
		std::uint32_t meshletCount = 0;
		glm::vec3 localCenter(0.0f);
		float localRadius = 0.0f;
		if (object.mesh) {
			if (auto* mesh = renderer_.ResolveOrCreateMesh(object.mesh);
				mesh && mesh->meshletCount > 0) {
				meshSet = mesh->meshletBindingSet;
				meshletCount = mesh->meshletCount;
				if (!object.mesh->vertices.empty()) {
					glm::vec3 minimum = object.mesh->vertices.front().position;
					glm::vec3 maximum = minimum;
					for (const auto& vertex : object.mesh->vertices) {
						minimum = glm::min(minimum, vertex.position);
						maximum = glm::max(maximum, vertex.position);
					}
					localCenter = (minimum + maximum) * 0.5f;
					for (const auto& vertex : object.mesh->vertices)
						localRadius = std::max(localRadius,
							glm::length(vertex.position - localCenter));
				}
			}
		} else if (object.model) {
			if (auto* model = renderer_.ResolveOrCreateModel(object.model);
				model && model->meshletBindingSet) {
				const auto gpuPrimitive = std::ranges::find_if(model->primitiveMeshlets,
					[&object](const GpuModel::PrimitiveMeshlets& candidate) {
						return candidate.firstIndex == object.firstIndex &&
							candidate.indexCount == object.indexCount &&
							candidate.vertexOffset == object.vertexOffset;
					});
				if (gpuPrimitive != model->primitiveMeshlets.end()) {
					meshSet = model->meshletBindingSet;
					meshletOffset = gpuPrimitive->meshletOffset;
					meshletCount = gpuPrimitive->meshletCount;
					for (const auto& mesh : object.model->meshes) {
						const auto primitive = std::ranges::find_if(mesh.primitives,
							[&object](const MeshPrimitive& candidate) {
								return candidate.firstIndex == object.firstIndex &&
									candidate.indexCount == object.indexCount &&
									candidate.vertexOffset == object.vertexOffset;
							});
						if (primitive != mesh.primitives.end()) {
							localCenter = primitive->boundingSphere.center;
							localRadius = primitive->boundingSphere.radius;
							break;
						}
					}
				}
			}
		}
		if (!meshSet || meshletCount == 0) continue;
		const glm::vec3 worldCenter = glm::vec3(
			object.transform * glm::vec4(localCenter, 1.0f));
		const float scale = std::max({glm::length(glm::vec3(object.transform[0])),
			glm::length(glm::vec3(object.transform[1])),
			glm::length(glm::vec3(object.transform[2]))});
		renderItems_.push_back({object.transform, meshSet, meshletOffset, meshletCount});
		std::uint64_t objectSignature = kFnvOffset;
		HashBytes(objectSignature, &object.transform, sizeof(object.transform));
		const auto meshIdentity = reinterpret_cast<std::uintptr_t>(object.mesh.get());
		const auto modelIdentity = reinterpret_cast<std::uintptr_t>(object.model.get());
		HashValue(objectSignature, meshIdentity);
		HashValue(objectSignature, modelIdentity);
		HashValue(objectSignature, object.firstIndex);
		HashValue(objectSignature, object.indexCount);
		HashValue(objectSignature, object.vertexOffset);
		objectSignatures.push_back(objectSignature);
		cullObjects.push_back({
			.centerAndRadius = glm::vec4(worldCenter, localRadius * scale),
			.info = glm::uvec4(meshletCount, 0u, 0u, 0u),
		});
	}
	std::vector<ShadowCullLight> cullLights;
	cullLights.reserve(renderer_.pointShadowAssignments_.size());
	std::vector<SparseImagePageBind> pageBindings;
	std::array<bool, kMaxShadowLights> assignedSlots{};
	for (const auto& assignment : renderer_.pointShadowAssignments_) {
		if (assignment.slot >= kMaxShadowLights ||
			assignment.sceneIndex >= scene.lights.size()) continue;
		const auto mip = assignment.mipLevel;
		assignedSlots[assignment.slot] = true;
		const auto memoryInfo = renderer_.device_->GetSparseImageMemoryInfo(shadowImage_);
		const auto blocksX = (assignment.resolution + memoryInfo.pageWidth - 1) /
			memoryInfo.pageWidth;
		const auto blocksY = (assignment.resolution + memoryInfo.pageHeight - 1) /
			memoryInfo.pageHeight;
		std::uint32_t page = assignment.firstPage;
		for (std::uint32_t face = 0; face < kFacesPerLight; ++face) {
			for (std::uint32_t y = 0; y < blocksY; ++y) {
				for (std::uint32_t x = 0; x < blocksX; ++x) {
					pageBindings.push_back({.mipLevel = mip,
						.arrayLayer = assignment.slot * kFacesPerLight + face,
						.blockX = x, .blockY = y, .memoryPage = page++});
				}
			}
		}
		const auto& light = scene.lights[assignment.sceneIndex];
		std::uint64_t signature = kFnvOffset;
		const auto stableLightId = light.sourceId != 0
			? light.sourceId : static_cast<std::uint64_t>(assignment.sceneIndex + 1);
		HashValue(signature, stableLightId);
		HashValue(signature, light.position);
		HashValue(signature, light.range);
		HashValue(signature, assignment.mipLevel);
		HashValue(signature, assignment.resolution);
		HashValue(signature, assignment.firstPage);
		HashValue(signature, assignment.pageCount);
		for (std::size_t objectIndex = 0; objectIndex < cullObjects.size(); ++objectIndex) {
			const auto& bounds = cullObjects[objectIndex].centerAndRadius;
			const glm::vec3 delta = glm::vec3(bounds) - light.position;
			const float radius = bounds.w + light.range;
			if (glm::dot(delta, delta) <= radius * radius)
				HashValue(signature, objectSignatures[objectIndex]);
		}
		const bool dirty = !cachedSlots_[assignment.slot] ||
			cachedSignatures_[assignment.slot] != signature;
		dirtyAssignments_.push_back(dirty);
		pendingSignatures_.push_back(signature);
		dirtyLightCount_ += dirty ? 1u : 0u;
		cullLights.push_back({
			.positionAndRange = glm::vec4(light.position, light.range),
			.info = glm::uvec4(assignment.localLightIndex, dirty ? 1u : 0u, 0u, 0u),
		});
		const auto matrixBase = assignment.slot * kFacesPerLight;
		auto projection = glm::perspectiveRH_ZO(
			glm::radians(90.0f), 1.0f, 0.1f, light.range);
		const glm::vec3 directions[kFacesPerLight] = {
			{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
			{0, -1, 0}, {0, 0, 1}, {0, 0, -1},
		};
		const glm::vec3 ups[kFacesPerLight] = {
			{0, -1, 0}, {0, -1, 0}, {0, 0, 1},
			{0, 0, -1}, {0, -1, 0}, {0, -1, 0},
		};
		for (std::uint32_t face = 0; face < kFacesPerLight; ++face) {
			shadowFrameData_.viewProjections[matrixBase + face] =
				projection * glm::lookAt(light.position,
					light.position + directions[face], ups[face]);
		}
		++activeLightCount_;
	}
	for (std::uint32_t slot = 0; slot < kMaxShadowLights; ++slot)
		if (!assignedSlots[slot]) cachedSlots_[slot] = false;
	renderer_.device_->BindSparseImagePages(shadowImage_, pageBindings);
	if (activeLightCount_ == 0) return;
	shadowFrameData_.counts = glm::uvec4(activeLightCount_, 0, 0, 0);
	const auto frameIndex = renderer_.frame_.frameIndex;
    renderer_.UploadBuffer(commands,
        frameData_.at(renderer_.frame_.frameIndex), &shadowFrameData_,
        sizeof(shadowFrameData_), ResourceState::UniformBuffer);
	const ShadowCullData cullData{glm::uvec4(activeLightCount_,
		static_cast<std::uint32_t>(renderItems_.size()),
		renderer_.lightCullingPass_->TileCount(),
		Renderer::LightCullingPass::WordsPerTile())};
	renderer_.UploadBuffer(commands, cullData_[frameIndex], &cullData,
		sizeof(cullData), ResourceState::UniformBuffer);
	if (!cullLights.empty()) renderer_.UploadBuffer(commands, cullLights_[frameIndex],
		cullLights.data(), cullLights.size() * sizeof(ShadowCullLight),
		ResourceState::ShaderRead);
	if (!cullObjects.empty()) renderer_.UploadBuffer(commands, cullObjects_[frameIndex],
		cullObjects.data(), cullObjects.size() * sizeof(ShadowCullObject),
		ResourceState::ShaderRead);
}

void Renderer::ShadowMappingPass::Render(
    ICommandList& cmd, const RenderScene& scene)
{
	if (activeLightCount_ == 0) return;
	const auto frameIndex = renderer_.frame_.frameIndex;
	if (dirtyLightCount_ > 0 && !renderItems_.empty()) {
		cmd.Barrier({.buffer = indirectCommands_[frameIndex],
			.oldState = indirectCommandsInitialized_[frameIndex]
				? ResourceState::IndirectArgument : ResourceState::Undefined,
			.newState = ResourceState::ShaderWrite,
			.sourceQueue = QueueType::Graphics,
			.destinationQueue = QueueType::Graphics});
		indirectCommandsInitialized_[frameIndex] = true;
		cmd.BindComputePipeline(instanceCullPipeline_);
		cmd.SetComputeBindings(instanceCullPipeline_, 0, cullSets_[frameIndex]);
		cmd.Dispatch(activeLightCount_, 1, 1);
		cmd.Barrier({.buffer = indirectCommands_[frameIndex],
			.oldState = ResourceState::ShaderWrite,
			.newState = ResourceState::IndirectArgument,
			.sourceQueue = QueueType::Graphics,
			.destinationQueue = QueueType::Graphics});
	}

    struct ShadowDrawConstants {
        glm::mat4 model{1.0f};
        glm::uvec4 meshletInfo{};
    };
    static_assert(sizeof(ShadowDrawConstants) == 80);

    const auto draw = [&](const ShadowRenderItem& item, std::uint32_t objectIndex,
		std::uint32_t assignmentIndex, std::uint32_t slot) {
        cmd.BindPipeline(shadowPipeline_);
        cmd.SetBindings(shadowPipeline_, 0,
            shadowSets_.at(renderer_.frame_.frameIndex));
		cmd.SetBindings(shadowPipeline_, 1, item.meshSet);
		const ShadowDrawConstants constants{
			.model = item.model,
			.meshletInfo = glm::uvec4(item.meshletOffset,
				slot * kFacesPerLight, 0u, 0u),
		};
		cmd.PushConstants(ShaderStage::Mesh, 0,
			static_cast<Velos::u32>(sizeof(constants)), &constants);
		const auto commandIndex = static_cast<std::uint64_t>(assignmentIndex) *
			renderItems_.size() + objectIndex;
		cmd.DrawMeshTasksIndirect(indirectCommands_[frameIndex],
			commandIndex * sizeof(ShadowDrawCommand), 1,
			sizeof(ShadowDrawCommand));
    };

	std::array<bool, 4> dirtyMips{};
	for (std::uint32_t assignmentIndex = 0;
		assignmentIndex < renderer_.pointShadowAssignments_.size(); ++assignmentIndex) {
		if (!dirtyAssignments_[assignmentIndex]) continue;
		const auto mip = renderer_.pointShadowAssignments_[assignmentIndex].mipLevel;
		if (mip < dirtyMips.size()) dirtyMips[mip] = true;
	}
	for (std::uint32_t mip = 1; mip < dirtyMips.size(); ++mip) {
		if (!dirtyMips[mip]) continue;
		cmd.Barrier({.image = shadowImage_,
			.oldLayout = renderer_.device_->GetImageLayout(shadowImage_, mip),
			.newLayout = ImageLayout::DepthAttachment,
			.aspect = ImageAspect::Depth,
			.baseMipLevel = mip, .mipLevelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = kMaxShadowLights * kFacesPerLight});
	}
	for (std::uint32_t assignmentIndex = 0;
		assignmentIndex < renderer_.pointShadowAssignments_.size(); ++assignmentIndex) {
		if (!dirtyAssignments_[assignmentIndex]) continue;
		const auto& assignment = renderer_.pointShadowAssignments_[assignmentIndex];
		if (assignment.slot >= kMaxShadowLights ||
			assignment.mipLevel >= shadowSlotDepthViews_.size()) continue;
		const auto resolution = 1024u >> assignment.mipLevel;
		const DepthAttachmentDesc depth{
			.view = shadowSlotDepthViews_[assignment.mipLevel][assignment.slot],
			.loadOp = LoadOp::Clear,
			.storeOp = StoreOp::Store,
			.clearDepth = 1.0f,
		};
		cmd.SetViewport({.width = static_cast<float>(resolution),
			.height = static_cast<float>(resolution)});
		cmd.SetScissor({.extent = {resolution, resolution}});
		cmd.BeginRendering({
			.renderArea = {.extent = {resolution, resolution}},
			.depthAttachment = &depth,
			.layerCount = kFacesPerLight,
		});
		for (std::uint32_t objectIndex = 0; objectIndex < renderItems_.size(); ++objectIndex)
			draw(renderItems_[objectIndex], objectIndex, assignmentIndex, assignment.slot);
		cmd.EndRendering();
		cachedSignatures_[assignment.slot] = pendingSignatures_[assignmentIndex];
		cachedSlots_[assignment.slot] = true;
	}
	for (std::uint32_t mip = 1; mip < dirtyMips.size(); ++mip) {
		if (!dirtyMips[mip]) continue;
		cmd.Barrier({.image = shadowImage_,
			.oldLayout = ImageLayout::DepthAttachment,
			.newLayout = ImageLayout::ShaderReadOnly,
			.aspect = ImageAspect::Depth,
			.baseMipLevel = mip, .mipLevelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = kMaxShadowLights * kFacesPerLight});
	}
}

FrameGraphTextureInfo Renderer::ShadowMappingPass::ShadowMapInfo() const
{
    return {
        .width = 1024,
        .height = 1024,
        .arrayLayers = kMaxShadowLights * kFacesPerLight,
        .format = Format::D32_FLOAT,
        .imageType = ImageType::Cube,
        .viewType = ImageViewType::View2DArray,
        .usage = ImageUsage::DepthStencil | ImageUsage::Sampled,
        .loadOp = RenderPassOperation::Clear,
        .clearDepth = 1.0f,
        .handle = shadowImage_,
		.view = shadowCubeView_
    };
}

FrameGraphBufferInfo Renderer::ShadowMappingPass::IndirectCommandsInfo() const
{
    return {
		.size = sizeof(ShadowDrawCommand) * kMaxShadowLights * kMaxShadowObjects,
        .usage = BufferUsage::Storage | BufferUsage::Indirect,
		.handle = indirectCommands_.at(renderer_.frame_.frameIndex)
    };
}

void Renderer::ShadowMappingPass::OnResize(
	IDevice& device, std::uint32_t, std::uint32_t)
{
	for (std::uint32_t frame = 0; frame < k_FramesInFlight; ++frame) {
		const BindingBufferInfo tiles{
			.buffer = renderer_.lightCullingPass_->TileBuffer(frame),
			.range = renderer_.lightCullingPass_->TileBufferSize(),
		};
		device.UpdateBindingSet({.dstSet = cullSets_[frame], .binding = 3,
			.type = BindingType::StorageBuffer, .bufferInfo = &tiles});
	}
}

void Renderer::ShadowMappingPass::CreateResources()
{
    auto& device = *renderer_.device_;
    shadowImage_ = device.CreateSparseImage({
        .width = 1024,
        .height = 1024,
        .depth = 1,
        .mipLevels = 4,
        .arrayLayers = kMaxShadowLights * kFacesPerLight,
        .format = Format::D32_FLOAT,
        .type = ImageType::Cube,
        .usage = ImageUsage::DepthStencil | ImageUsage::Sampled,
        .debugName = "Sparse point shadow cubemap",
    }, renderer_.pointShadowMemoryBudget_);
	const auto sparseInfo = device.GetSparseImageMemoryInfo(shadowImage_);
	renderer_.sparseShadowAllocator_.Configure(sparseInfo.pageWidth,
		sparseInfo.pageHeight, sparseInfo.pageCount);

    // A cube view is used when the completed lighting pass samples the map.
    shadowCubeView_ = device.CreateImageView({
        .image = shadowImage_,
        .format = Format::D32_FLOAT,
        .type = ImageViewType::CubeArray,
        .aspect = ImageAspect::Depth,
		.baseMipLevel = 0,
		.mipLevelCount = 4,
		.baseArrayLayer = 0,
        .arrayLayerCount = kMaxShadowLights * kFacesPerLight,
        .debugName = "Point shadow cubemap sampling view",
    });

    // Dynamic rendering requires a 2D-array view to address gl_Layer 0..5.
	for (std::uint32_t mip = 0; mip < shadowMipDepthViews_.size(); ++mip) {
		shadowMipDepthViews_[mip] = device.CreateImageView({
			.image = shadowImage_, .format = Format::D32_FLOAT,
			.type = ImageViewType::View2DArray, .aspect = ImageAspect::Depth,
			.baseMipLevel = mip, .mipLevelCount = 1,
			.baseArrayLayer = 0,
			.arrayLayerCount = kMaxShadowLights * kFacesPerLight,
			.debugName = "Sparse point shadow mip depth view",
		});
		for (std::uint32_t slot = 0; slot < kMaxShadowLights; ++slot) {
			shadowSlotDepthViews_[mip][slot] = device.CreateImageView({
				.image = shadowImage_, .format = Format::D32_FLOAT,
				.type = ImageViewType::View2DArray, .aspect = ImageAspect::Depth,
				.baseMipLevel = mip, .mipLevelCount = 1,
				.baseArrayLayer = slot * kFacesPerLight,
				.arrayLayerCount = kFacesPerLight,
				.debugName = "Cached point shadow slot depth view",
			});
		}
	}

    // Velos does not expose sampler comparison state yet, so this remains a
    // regular sampler until shadow sampling is connected to lighting.
    shadowSampler_ = device.CreateSampler({
        .minFilter = Filter::Linear,
        .magFilter = Filter::Linear,
        .addressU = SamplerAddressMode::ClampToEdge,
        .addressV = SamplerAddressMode::ClampToEdge,
        .addressW = SamplerAddressMode::ClampToEdge,
		.maxLod = 3.0f,
        .debugName = "Point shadow sampler",
    });

    for (std::uint32_t frame = 0; frame < k_FramesInFlight; ++frame) {
        frameData_[frame] = renderer_.CreateUploadBackedBuffer(
            sizeof(ShadowFrameData), BufferUsage::Uniform,
            "Shadow frame data", "Shadow frame upload buffer");
		cullData_[frame] = renderer_.CreateUploadBackedBuffer(
			sizeof(ShadowCullData), BufferUsage::Uniform,
			"Shadow cull data", "Shadow cull data upload");
		cullLights_[frame] = renderer_.CreateUploadBackedBuffer(
			sizeof(ShadowCullLight) * kMaxShadowLights, BufferUsage::Storage,
			"Shadow cull lights", "Shadow cull lights upload");
		cullObjects_[frame] = renderer_.CreateUploadBackedBuffer(
			sizeof(ShadowCullObject) * kMaxShadowObjects, BufferUsage::Storage,
			"Shadow cull objects", "Shadow cull objects upload");
		indirectCommands_[frame] = device.CreateBuffer({
			.size = sizeof(ShadowDrawCommand) * kMaxShadowLights * kMaxShadowObjects,
			.usage = BufferUsage::Storage | BufferUsage::Indirect,
			.memoryUsage = MemoryUsage::GPUOnly,
			.debugName = "Shadow mesh task indirect commands",
		});
    }

    const BindingDesc shadowBindings[] = {
    {
        .binding = 0,
        .type = BindingType::UniformBuffer,
        .visibility = ShaderStage::Mesh,
    },
    };

	shadowLayout_ = device.CreateBindingLayout({
		.bindings = shadowBindings,
		.bindingCount = static_cast<std::uint32_t>(std::size(shadowBindings)),
		.debugName = "Shadow mapping layout",
		});

    const BindingPoolSize size{BindingType::UniformBuffer, k_FramesInFlight};
    pool_ = device.CreateBindingPool({ .poolSizes = &size, .poolSizeCount = 1,
        .maxSets = k_FramesInFlight, .debugName = "Shadow mapping pool" });
    for (std::uint32_t frame = 0; frame < k_FramesInFlight; ++frame) {
        shadowSets_[frame] = device.AllocateBindingSet({
            .pool = pool_, .layout = shadowLayout_,
            .debugName = "Shadow frame binding set",
        });
        const BindingBufferInfo info{
            .buffer = frameData_[frame].gpuBuffer,
            .range = sizeof(ShadowFrameData),
        };
        device.UpdateBindingSet({
            .dstSet = shadowSets_[frame], .binding = 0,
            .type = BindingType::UniformBuffer, .bufferInfo = &info,
        });
    }

	const BindingDesc cullBindings[]{
		{.binding = 0, .type = BindingType::UniformBuffer,
			.visibility = ShaderStage::Compute},
		{.binding = 1, .type = BindingType::StorageBuffer,
			.visibility = ShaderStage::Compute},
		{.binding = 2, .type = BindingType::StorageBuffer,
			.visibility = ShaderStage::Compute},
		{.binding = 3, .type = BindingType::StorageBuffer,
			.visibility = ShaderStage::Compute},
		{.binding = 4, .type = BindingType::StorageBuffer,
			.visibility = ShaderStage::Compute},
	};
	cullLayout_ = device.CreateBindingLayout({.bindings = cullBindings,
		.bindingCount = static_cast<std::uint32_t>(std::size(cullBindings)),
		.debugName = "Shadow object cull layout"});
	const BindingPoolSize cullPoolSizes[]{
		{BindingType::UniformBuffer, k_FramesInFlight},
		{BindingType::StorageBuffer, 4 * k_FramesInFlight},
	};
	cullPool_ = device.CreateBindingPool({.poolSizes = cullPoolSizes,
		.poolSizeCount = static_cast<std::uint32_t>(std::size(cullPoolSizes)),
		.maxSets = k_FramesInFlight, .debugName = "Shadow object cull pool"});
	for (std::uint32_t frame = 0; frame < k_FramesInFlight; ++frame) {
		cullSets_[frame] = device.AllocateBindingSet({.pool = cullPool_,
			.layout = cullLayout_, .debugName = "Shadow object cull set"});
		const BindingBufferInfo data{.buffer = cullData_[frame].gpuBuffer,
			.range = sizeof(ShadowCullData)};
		const BindingBufferInfo lights{.buffer = cullLights_[frame].gpuBuffer,
			.range = sizeof(ShadowCullLight) * kMaxShadowLights};
		const BindingBufferInfo objects{.buffer = cullObjects_[frame].gpuBuffer,
			.range = sizeof(ShadowCullObject) * kMaxShadowObjects};
		const BindingBufferInfo tiles{
			.buffer = renderer_.lightCullingPass_->TileBuffer(frame),
			.range = renderer_.lightCullingPass_->TileBufferSize()};
		const BindingBufferInfo commands{.buffer = indirectCommands_[frame],
			.range = sizeof(ShadowDrawCommand) * kMaxShadowLights * kMaxShadowObjects};
		const BindingBufferInfo infos[]{data, lights, objects, tiles, commands};
		for (std::uint32_t binding = 0; binding < std::size(infos); ++binding) {
			device.UpdateBindingSet({.dstSet = cullSets_[frame], .binding = binding,
				.type = binding == 0 ? BindingType::UniformBuffer : BindingType::StorageBuffer,
				.bufferInfo = &infos[binding]});
		}
	}

    const auto createShader = [&](const char* path, ShaderStage stage) {
        const auto code = Velos::ShaderCompiler::CompileFile({
            .path = path, .stage = stage,
            .language = Velos::ShaderSourceLanguage::SpirvBinary,
        });
        return device.CreateShader({
            .stage = stage, .bytecode = code.spirv.data(),
            .bytecodeSize = code.spirv.size() * sizeof(std::uint32_t),
            .reflection = code.reflection, .debugName = path,
        });
    };
    meshShader_ = createShader(
        "assets/shaders/internal/shadow.mesh.spv", ShaderStage::Mesh);
	instanceCullShader_ = createShader(
		"assets/shaders/internal/shadow_instance_cull.comp.spv", ShaderStage::Compute);
    fragmentShader_ = createShader(
        "assets/shaders/internal/shadow.frag.spv", ShaderStage::Fragment);
    const BindingLayoutHandle layouts[]{
        shadowLayout_, renderer_.meshletBindingLayout_,
    };
    shadowPipeline_ = device.CreateMeshPipeline({
        .meshShader = meshShader_,
        .fragmentShader = fragmentShader_,
        .layout = {.descriptorSetLayouts = layouts,
            .descriptorSetLayoutCount = static_cast<Velos::u32>(std::size(layouts))},
        .raster = {.cullBackFaces = false, .frontFaceCCW = true},
        .depth = {.depthTestEnable = true, .depthWriteEnable = true,
            .depthFormat = Format::D32_FLOAT},
        .colorFormat = Format::Undefined,
        .debugName = "Point shadow mesh pipeline",
    });
	instanceCullPipeline_ = device.CreateComputePipeline({
		.computeShader = instanceCullShader_,
		.layout = {.descriptorSetLayouts = &cullLayout_, .descriptorSetLayoutCount = 1},
		.debugName = "Shadow object cull pipeline",
	});
}

void Renderer::ShadowMappingPass::DestroyResources()
{
    auto& device = *renderer_.device_;
	if (cullPool_) device.DestroyBindingPool(cullPool_);
	cullPool_ = {};
	cullSets_ = {};
	indirectCommandsInitialized_ = {};
    if (pool_) device.DestroyBindingPool(pool_);
    pool_ = {};
	for (auto& buffer : indirectCommands_) {
		if (buffer) device.DestroyBuffer(buffer);
		buffer = {};
	}
    for (auto& buffer : frameData_) renderer_.DestroyUploadBackedBuffer(buffer);
	for (auto& buffer : cullData_) renderer_.DestroyUploadBackedBuffer(buffer);
	for (auto& buffer : cullLights_) renderer_.DestroyUploadBackedBuffer(buffer);
	for (auto& buffer : cullObjects_) renderer_.DestroyUploadBackedBuffer(buffer);
    if (shadowSampler_) device.DestroySampler(shadowSampler_);
	for (auto& view : shadowMipDepthViews_) {
		if (view) device.DestroyImageView(view);
		view = {};
	}
	for (auto& mipViews : shadowSlotDepthViews_) {
		for (auto& view : mipViews) {
			if (view) device.DestroyImageView(view);
			view = {};
		}
	}
    if (shadowCubeView_) device.DestroyImageView(shadowCubeView_);
    if (shadowImage_) device.DestroyImage(shadowImage_);
    shadowSampler_ = {};
    shadowCubeView_ = {};
    shadowImage_ = {};
}

void Renderer::ShadowMappingPass::Destroy()
{
    DestroyResources();
    auto& device = *renderer_.device_;
    if (shadowPipeline_) device.DestroyPipeline(shadowPipeline_);
    if (instanceCullPipeline_) device.DestroyPipeline(instanceCullPipeline_);
    if (meshShader_) device.DestroyShader(meshShader_);
    if (fragmentShader_) device.DestroyShader(fragmentShader_);
    if (instanceCullShader_) device.DestroyShader(instanceCullShader_);
    if (shadowLayout_) device.DestroyBindingLayout(shadowLayout_);
	if (cullLayout_) device.DestroyBindingLayout(cullLayout_);
    shadowPipeline_ = {};
    instanceCullPipeline_ = {};
    meshShader_ = {};
    fragmentShader_ = {};
    instanceCullShader_ = {};
    shadowLayout_ = {};
	cullLayout_ = {};
}

} // namespace Iryven
