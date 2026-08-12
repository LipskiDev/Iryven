#pragma once

#include <glm/ext/vector_float3.hpp>
#include <glm/mat4x4.hpp>

namespace Iryven {

struct FrameData {
    glm::mat4 view{ 1.0f };
    glm::mat4 projection{ 1.0f };
    glm::mat4 viewProjection{ 1.0f };
    glm::vec3 cameraPosition{ 0.0f };
};

} // namespace Iryven
