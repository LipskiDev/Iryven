#pragma once

#include <iryven/math/color.h>
#include <iryven/rendering/light_type.h>

namespace Iryven {

struct Light {
    LightType type = LightType::Directional;
    Color color = Color::White;
    float intensity = 1.0f;
    float range = 10.0f;
    float innerConeAngle = 20.0f;
    float outerConeAngle = 30.0f;
    bool enabled = true;
};

} // namespace Iryven
