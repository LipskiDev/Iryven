#pragma once

#include <cstdint>
#include <vector>

#include <glm/ext/vector_float3.hpp>

namespace Iryven {

struct Vertex {
    glm::vec3 position{ 0.0f };
    glm::vec3 normal{ 0.0f, 0.0f, 0.0f };
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

} // namespace Iryven
