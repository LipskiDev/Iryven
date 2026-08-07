#pragma once

#include <optional>
#include <vector>

#include <iryven/rendering/render_camera.h>
#include <iryven/rendering/render_object.h>

namespace Iryven {
	struct RenderScene {
		std::optional<RenderCamera> camera;
		std::vector<RenderObject> objects;
	};
}
