#include "cloth_draw.h"

namespace Iryven {

void Renderer::ClothDraw::Render(
	Velos::RHI::ICommandList& commands, const RenderScene& scene)
{
	renderer_.DrawCloths(commands, scene);
}

} // namespace Iryven
