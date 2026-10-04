#include "tonemapping.h"

#include <shader/shader_compiler.h>

namespace Iryven {

Renderer::TonemappingPass::TonemappingPass(Renderer& renderer)
	: renderer_(renderer)
{
	auto& device = *renderer_.device_;
	try {
		const auto createShader = [&](const char* path, ShaderStage stage) {
			const auto code = Velos::ShaderCompiler::CompileFile({
				.path = path,
				.stage = stage,
				.language = Velos::ShaderSourceLanguage::SpirvBinary,
			});
			return device.CreateShader({
				.stage = stage,
				.bytecode = code.spirv.data(),
				.bytecodeSize = code.spirv.size() * sizeof(std::uint32_t),
				.reflection = code.reflection,
				.debugName = path,
			});
		};

		vertex_ = createShader(
			"assets/shaders/internal/tonemapping.vert.spv", ShaderStage::Vertex);
		fragment_ = createShader(
			"assets/shaders/internal/tonemapping.frag.spv", ShaderStage::Fragment);

		const BindingDesc binding{
			.binding = 0,
			.type = BindingType::CombinedImageSampler,
			.visibility = ShaderStage::Fragment,
		};
		textureLayout_ = device.CreateBindingLayout({
			.bindings = &binding,
			.bindingCount = 1,
			.debugName = "Tonemapping input",
		});
		pipeline_ = device.CreateGraphicsPipeline({
			.vertexShader = vertex_,
			.fragmentShader = fragment_,
			.layout = {
				.descriptorSetLayouts = &textureLayout_,
				.descriptorSetLayoutCount = 1,
			},
			.raster = {.cullBackFaces = false},
			.colorFormat = Format::BGRA8_UNORM,
			.debugName = "Reinhard tonemapping",
		});

		const BindingPoolSize poolSize{
			.type = BindingType::CombinedImageSampler,
			.count = k_FramesInFlight,
		};
		pool_ = device.CreateBindingPool({
			.poolSizes = &poolSize,
			.poolSizeCount = 1,
			.maxSets = k_FramesInFlight,
			.debugName = "Tonemapping bindings",
		});
		sampler_ = device.CreateSampler({
			.minFilter = Filter::Nearest,
			.magFilter = Filter::Nearest,
		});
		for (auto& set : sets_) {
			set = device.AllocateBindingSet({.pool = pool_, .layout = textureLayout_});
		}
	} catch (...) {
		Destroy();
		throw;
	}
}

Renderer::TonemappingPass::~TonemappingPass()
{
	Destroy();
}

void Renderer::TonemappingPass::Destroy()
{
	auto& device = *renderer_.device_;
	if (pipeline_) device.DestroyPipeline(pipeline_);
	if (pool_) device.DestroyBindingPool(pool_);
	if (sampler_) device.DestroySampler(sampler_);
	if (textureLayout_) device.DestroyBindingLayout(textureLayout_);
	if (fragment_) device.DestroyShader(fragment_);
	if (vertex_) device.DestroyShader(vertex_);
}

void Renderer::TonemappingPass::PreRender(ICommandList&, const RenderScene&)
{
	const auto& sceneColor = std::get<FrameGraphTextureInfo>(
		renderer_.frameGraph_.GetResource("sceneColor")->info);
	const BindingImageInfo image{
		.sampler = sampler_,
		.imageView = sceneColor.view,
		.imageLayout = ImageLayout::ShaderReadOnly,
	};
	renderer_.device_->UpdateBindingSet({
		.dstSet = sets_.at(renderer_.frame_.frameIndex),
		.binding = 0,
		.type = BindingType::CombinedImageSampler,
		.imageInfo = &image,
	});
}

void Renderer::TonemappingPass::Render(ICommandList& commands, const RenderScene&)
{
	commands.BindPipeline(pipeline_);
	commands.SetBindings(
		pipeline_, 0, sets_.at(renderer_.frame_.frameIndex));
	commands.Draw(3);
}

} // namespace Iryven
