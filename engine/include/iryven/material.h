#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <iryven/math/color.h>

namespace Iryven {

struct Material {
    std::string name;
    std::filesystem::path source;
    Color baseColor = Color::White;
};

using MaterialHandle = std::shared_ptr<const Material>;

} // namespace Iryven
