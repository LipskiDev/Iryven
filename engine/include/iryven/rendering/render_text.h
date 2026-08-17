#pragma once

#include <string>

#include <glm/ext/vector_float2.hpp>

#include <iryven/assets/font.h>
#include <iryven/math/color.h>

namespace Iryven {

struct RenderText {
    FontHandle font;
    std::string text;
    glm::vec2 position{0.0f};
    float fontSize = 16.0f;
    Color color = Color::White;
};

} // namespace Iryven
