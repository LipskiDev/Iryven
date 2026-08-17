#pragma once

#include <optional>
#include <vector>

#include <iryven/rendering/render_camera.h>
#include <iryven/rendering/render_object.h>
#include <iryven/rendering/render_light.h>
#include <iryven/rendering/render_text.h>

namespace Iryven {
	struct RenderScene {
		std::optional<RenderCamera> camera;
		std::vector<RenderObject> objects;
		std::vector<RenderLight> lights;
		std::vector<RenderText> texts;
	};
}
