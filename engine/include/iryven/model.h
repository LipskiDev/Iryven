#pragma once

#include <filesystem>
#include <memory>

#include <iryven/rendering/mesh_data.h>

namespace Iryven {

struct Model {
    std::filesystem::path source;
    std::shared_ptr<const MeshData> mesh;
};

using ModelHandle = std::shared_ptr<const Model>;

} // namespace Iryven
