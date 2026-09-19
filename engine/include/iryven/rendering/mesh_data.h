#pragma once
#include <cstdint>
#include <vector>

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float4.hpp>

namespace Iryven {

struct Vertex {
    glm::vec3 position{ 0.0f };
    glm::vec3 normal{ 0.0f, 0.0f, 0.0f };
    glm::vec2 texCoord{ 0.0, 0.0f };
    glm::vec4 tangent{ 0.0f };
    glm::vec4 color{ 1.0f };
};

struct Meshlet {
    std::vector<std::uint32_t> vertexIndices;
    std::vector<std::uint32_t> triangleIndices;

    // Bounding sphere
	glm::vec3 center{ 0.0f };
	float radius{ 0.0f };

	// Cone culling
	glm::vec3 coneApex{ 0.0f };
	glm::vec3 coneAxis{ 0.0f, 0.0f, 1.0f };
	float  coneCutoff{ 1.0f }; // Cosine of the cone angle
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
	std::vector<Meshlet> meshlets;
};

} // namespace Iryven
