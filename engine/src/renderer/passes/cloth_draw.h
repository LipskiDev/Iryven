#pragma once

#include "../renderer.h"

namespace Iryven {

class Renderer::ClothDraw final : public FrameGraphRenderPass {
public:
	explicit ClothDraw(Renderer& renderer) : renderer_(renderer) {}

	void AddUI() override {}
	void PreRender(Velos::RHI::ICommandList&, const RenderScene&) override {}
	void Render(Velos::RHI::ICommandList& commands,
		const RenderScene& scene) override;
	void OnResize(Velos::RHI::IDevice&, std::uint32_t, std::uint32_t) override {}

private:
	Renderer& renderer_;
};

} // namespace Iryven
