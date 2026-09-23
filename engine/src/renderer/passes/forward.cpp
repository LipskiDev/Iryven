#include "forward.h"

namespace Iryven {

void Renderer::ForwardPass::Render(
	Velos::RHI::ICommandList& commands, const RenderScene& scene)
{
	// Composite transmission and text over the deferred lighting result.
	renderer_.DrawTransmissiveObjects(commands, scene);
	for (const RenderText& text : scene.texts) {
		renderer_.DrawText(commands, text);
	}
}

} // namespace Iryven
