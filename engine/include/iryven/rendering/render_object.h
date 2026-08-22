#pragma once

#include <memory>

#include <glm/mat4x4.hpp>
#include <iryven/rendering/mesh_data.h>
#include <iryven/material.h>
#include <iryven/model.h>

namespace Iryven {
	struct RenderObject {
		glm::mat4 transform{ 1.0f };
		std::shared_ptr<const MeshData> mesh;
		ModelHandle model;
		std::uint32_t firstIndex = 0;
		std::uint32_t indexCount = 0;
		std::int32_t vertexOffset = 0;
		MaterialHandle material;
	};
}
