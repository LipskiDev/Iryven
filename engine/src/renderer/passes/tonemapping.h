#pragma once

#include "../renderer.h"

namespace Iryven {

class Renderer::TonemappingPass final : public FrameGraphRenderPass {
public:
	explicit TonemappingPass(Renderer& renderer);
	~TonemappingPass() override;

	void AddUI() override {}
	void PreRender(ICommandList&, const RenderScene&) override;
	void Render(ICommandList&, const RenderScene&) override;
	void OnResize(IDevice&, std::uint32_t, std::uint32_t) override {}

private:
	void Destroy();

	Renderer& renderer_;
	ShaderHandle vertex_{};
	ShaderHandle fragment_{};
	PipelineHandle pipeline_{};
	BindingLayoutHandle textureLayout_{};
	BindingPoolHandle pool_{};
	SamplerHandle sampler_{};
	std::array<BindingSetHandle, k_FramesInFlight> sets_{};
};

} // namespace Iryven
