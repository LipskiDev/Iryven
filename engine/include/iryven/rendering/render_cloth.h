#pragma once

#include <cstdint>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <iryven/material.h>

namespace Iryven {
struct RenderCloth {
	std::uint64_t id = 0;
	glm::mat4 transform{1.0f};
	glm::uvec2 resolution{32u, 32u};
	glm::vec2 size{2.0f, 2.0f};

	float mass = 1.0f;
	float stiffness = 0.9f;
	float damping = 0.99f;
	float gravityScale = 1.0f;
	std::uint32_t solverIterations = 8;
	bool pinTopLeft = true;
	bool pinTopRight = true;

	MaterialHandle material;
};

} // namespace Iryven
