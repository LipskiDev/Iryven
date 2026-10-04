#include "shading.h"
#include "shadow_mapping.h"
#include <shader/shader_compiler.h>

namespace Iryven {
Renderer::ShadingPass::ShadingPass(Renderer& renderer) : renderer_(renderer)
{
    auto& device = *renderer_.device_;
    try {
        const auto createShader = [&](const char* path, ShaderStage stage) {
            const auto code = Velos::ShaderCompiler::CompileFile({
                .path = path, .stage = stage,
                .language = Velos::ShaderSourceLanguage::SpirvBinary});
            return device.CreateShader({.stage = stage, .bytecode = code.spirv.data(),
                .bytecodeSize = code.spirv.size() * sizeof(std::uint32_t),
                .reflection = code.reflection, .debugName = path});
        };
        vertex_ = createShader("assets/shaders/internal/shading.vert.spv", ShaderStage::Vertex);
        fragment_ = createShader("assets/shaders/internal/shading.frag.spv", ShaderStage::Fragment);
        std::array<BindingDesc, 6> bindings{};
        for (std::uint32_t i = 0; i < bindings.size(); ++i)
            bindings[i] = {.binding = i, .type = BindingType::CombinedImageSampler,
                .visibility = ShaderStage::Fragment};
        textureLayout_ = device.CreateBindingLayout({.bindings = bindings.data(),
            .bindingCount = static_cast<std::uint32_t>(bindings.size()), .debugName = "G-buffer inputs"});
        const BindingLayoutHandle layouts[]{renderer_.lightsBindingLayout_, textureLayout_};
        pipeline_ = device.CreateGraphicsPipeline({.vertexShader = vertex_, .fragmentShader = fragment_,
            .layout = {.descriptorSetLayouts = layouts, .descriptorSetLayoutCount = 2},
            .raster = {.cullBackFaces = false}, .colorFormat = Format::RGBA16_FLOAT,
            .debugName = "Screen-space deferred shading"});
        const BindingPoolSize size{.type = BindingType::CombinedImageSampler,
            .count = static_cast<std::uint32_t>(bindings.size()) * k_FramesInFlight};
        pool_ = device.CreateBindingPool({.poolSizes = &size, .poolSizeCount = 1,
            .maxSets = k_FramesInFlight, .debugName = "Shading inputs"});
        sampler_ = device.CreateSampler({.minFilter = Filter::Nearest, .magFilter = Filter::Nearest});
        for (auto& set : sets_) set = device.AllocateBindingSet({.pool = pool_, .layout = textureLayout_});
    } catch (...) { Destroy(); throw; }
}

Renderer::ShadingPass::~ShadingPass() { Destroy(); }
void Renderer::ShadingPass::Destroy()
{
    auto& device = *renderer_.device_;
    if (pipeline_) device.DestroyPipeline(pipeline_);
    if (pool_) device.DestroyBindingPool(pool_);
    if (sampler_) device.DestroySampler(sampler_);
    if (textureLayout_) device.DestroyBindingLayout(textureLayout_);
    if (fragment_) device.DestroyShader(fragment_);
    if (vertex_) device.DestroyShader(vertex_);
}

void Renderer::ShadingPass::PreRender(ICommandList&, const RenderScene&)
{
    // Only update the descriptor set whose frame fence has completed. Fetching
    // graph views each frame also handles replacement after a resize.
    const auto set = sets_.at(renderer_.frame_.frameIndex);
    for (std::uint32_t i = 0; i < 5; ++i) {
        const auto& texture = std::get<FrameGraphTextureInfo>(
            renderer_.frameGraph_.GetResource(i < 4 ? GBufferNames[i] : "depth")->info);
        const BindingImageInfo image{.sampler = sampler_, .imageView = texture.view,
            .imageLayout = ImageLayout::ShaderReadOnly};
        renderer_.device_->UpdateBindingSet({.dstSet = set, .binding = i,
            .type = BindingType::CombinedImageSampler, .imageInfo = &image});
    }
	const BindingImageInfo shadowImage{
			.sampler = renderer_.shadowMappingPass_->ShadowSampler(),
			.imageView = renderer_.shadowMappingPass_->ShadowSamplingView(),
			.imageLayout = ImageLayout::ShaderReadOnly,
		};
		renderer_.device_->UpdateBindingSet({
			.dstSet = set, .binding = 5,
			.type = BindingType::CombinedImageSampler, .imageInfo = &shadowImage,
		});
}

void Renderer::ShadingPass::Render(ICommandList& commands, const RenderScene& scene)
{
    if (!scene.camera) return;
    const auto inverseViewProjection = glm::inverse(renderer_.BuildFrameData(*scene.camera).viewProjection);
    commands.BindPipeline(pipeline_);
    commands.SetBindings(pipeline_, 0, renderer_.lightingFrames_.at(renderer_.frame_.frameIndex).lightBindingSet);
    commands.SetBindings(pipeline_, 1, sets_.at(renderer_.frame_.frameIndex));
    struct alignas(16) Constants {
        glm::mat4 inverseViewProjection;
        std::uint32_t lightCountView = 0;
		std::uint32_t shadowTierView = 0;
    };
	static_assert(sizeof(Constants) == 80);
    Constants constants{
        .inverseViewProjection = inverseViewProjection,
        .lightCountView = renderer_.lightCountView_ ? 1u : 0u,
		.shadowTierView = renderer_.shadowTierView_ ? 1u : 0u,
    };
    commands.PushConstants(ShaderStage::Fragment, 0, 72, &constants);
    commands.Draw(3);
}
}
