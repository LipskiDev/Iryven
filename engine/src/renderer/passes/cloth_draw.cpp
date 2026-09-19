#include "cloth_draw.h"

namespace Iryven {

void Renderer::ClothDraw::Render(
	Velos::RHI::ICommandList& commands, const RenderScene& scene)
{
	renderer_.DrawCloths(commands, scene);
	// All opaque geometry, including cloth, must precede transmission.
	renderer_.DrawTransmissiveObjects(commands, scene);
	for (const RenderText& text : scene.texts) {
		renderer_.DrawText(commands, text);
	}
}

} // namespace Iryven
