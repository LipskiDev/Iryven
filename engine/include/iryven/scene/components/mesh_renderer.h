#pragma once

#include <memory>
#include <utility>
#include <vector>

#include <iryven/rendering/mesh_data.h>

namespace Iryven {

struct MeshRenderer {
    MeshRenderer(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices)
        : mesh(std::make_shared<const MeshData>(MeshData{
            .vertices = std::move(vertices),
            .indices = std::move(indices)
        })) {}

    std::shared_ptr<const MeshData> mesh;
};

}
