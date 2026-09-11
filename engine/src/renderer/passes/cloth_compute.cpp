#include "cloth_compute.h"

namespace Iryven {

	void Renderer::ClothCompute::PreRender(
		Velos::RHI::ICommandList&, const RenderScene&)
	{
	}

	void Renderer::ClothCompute::Render(
		Velos::RHI::ICommandList& commands, const RenderScene& scene)
	{
		renderer_.SimulateCloths(commands, scene);
	}

	void Renderer::ClothCompute::OnResize(
		Velos::RHI::IDevice&, std::uint32_t, std::uint32_t)
	{
	}

} // namespace Iryven
