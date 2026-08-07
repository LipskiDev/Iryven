#pragma once

#include <memory>

#include <glm/mat4x4.hpp>
#include <iryven/rendering/mesh_data.h>

namespace Iryven {
	struct RenderObject {
		glm::mat4 transform{ 1.0f };
		std::shared_ptr<const MeshData> mesh;
	};
}
