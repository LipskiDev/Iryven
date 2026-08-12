#pragma once

#include <glm/ext/vector_float3.hpp>

#include <iryven/math/color.h>
#include <iryven/rendering/light_type.h>

namespace Iryven {

struct RenderLight {
    LightType type = LightType::Directional;
    glm::vec3 position{ 0.0f };
    glm::vec3 direction{ 0.0f, 0.0f, -1.0f };
    Color color = Color::White;
    float intensity = 1.0f;
    float range = 10.0f;
    float innerConeAngle = 20.0f;
    float outerConeAngle = 30.0f;
};

} // namespace Iryven
