#pragma once

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <iryven/rendering/mesh_data.h>
#include <iryven/material.h>
#include <iryven/model.h>

namespace Iryven {

struct MeshRenderer {
    explicit MeshRenderer(std::shared_ptr<const MeshData> mesh, MaterialHandle material = {})
        : mesh(std::move(mesh)), material(std::move(material)) {
        if (!this->mesh) throw std::invalid_argument("MeshRenderer requires mesh data");
    }

    explicit MeshRenderer(ModelHandle model, MaterialHandle material = {})
        : mesh(model ? model->mesh : nullptr), model(std::move(model)), material(std::move(material)) {
        if (!mesh) throw std::invalid_argument("MeshRenderer requires a model with mesh data");
    }

    MeshRenderer(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices)
        : mesh(std::make_shared<const MeshData>(MeshData{
            .vertices = std::move(vertices),
            .indices = std::move(indices)
        })) {}

    std::shared_ptr<const MeshData> mesh;
    ModelHandle model;
    MaterialHandle material;
};

}
