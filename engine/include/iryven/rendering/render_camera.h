#pragma once

#include <glm/mat4x4.hpp>

namespace Iryven {

struct RenderCamera {
    glm::mat4 view{ 1.0f };
    float verticalFov = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
};

} // namespace Iryven
