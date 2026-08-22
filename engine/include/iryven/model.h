#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <iryven/material.h>
#include <iryven/rendering/mesh_data.h>
#include <iryven/texture.h>

namespace Iryven {

inline constexpr std::uint32_t InvalidModelIndex =
    std::numeric_limits<std::uint32_t>::max();

struct BoundingBox {
    glm::vec3 minimum{0.0f};
    glm::vec3 maximum{0.0f};
};

struct BoundingSphere {
    glm::vec3 center{0.0f};
    float radius = 0.0f;
};

struct MeshPrimitive {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::int32_t vertexOffset = 0;
    std::uint32_t materialIndex = InvalidModelIndex;
    BoundingBox bounds;
    BoundingSphere boundingSphere;
};

struct Mesh {
    std::string name;
    std::vector<MeshPrimitive> primitives;
    BoundingBox bounds;
};

struct ModelNode {
    std::string name;
    glm::mat4 localTransform{1.0f};
    std::uint32_t meshIndex = InvalidModelIndex;
    std::vector<std::uint32_t> children;
};

struct Model {
    std::filesystem::path source;

    // All primitives address these two contiguous arrays.
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Mesh> meshes;
    std::vector<MaterialHandle> materials;
    TextureRegistry textureRegistry;
    std::vector<ModelNode> nodes;
    std::vector<std::uint32_t> sceneRoots;

    [[nodiscard]] bool IsValid() const noexcept {
        if (vertices.empty() || indices.empty() || meshes.empty()) return false;
        for (const Mesh& mesh : meshes) {
            for (const MeshPrimitive& primitive : mesh.primitives) {
                if (primitive.indexCount == 0 || primitive.firstIndex > indices.size() ||
                    primitive.indexCount > indices.size() - primitive.firstIndex ||
                    (primitive.materialIndex != InvalidModelIndex &&
                     primitive.materialIndex >= materials.size())) return false;
                for (std::uint32_t i = 0; i < primitive.indexCount; ++i) {
                    const std::int64_t vertexIndex =
                        static_cast<std::int64_t>(indices[primitive.firstIndex + i]) +
                        primitive.vertexOffset;
                    if (vertexIndex < 0 || vertexIndex >= static_cast<std::int64_t>(vertices.size()))
                        return false;
                }
            }
        }
        for (const ModelNode& node : nodes) {
            if (node.meshIndex != InvalidModelIndex && node.meshIndex >= meshes.size()) return false;
            for (std::uint32_t child : node.children) if (child >= nodes.size()) return false;
        }
        for (std::uint32_t root : sceneRoots) if (root >= nodes.size()) return false;
        if (!textureRegistry.IsValid()) return false;
        for (const MaterialHandle& material : materials) {
            if (!material) return false;
            if (material->baseColorTexture != InvalidTextureIndex &&
                material->baseColorTexture >= textureRegistry.textures.size()) return false;
            if (material->metallicRoughnessTexture != InvalidTextureIndex &&
                material->metallicRoughnessTexture >= textureRegistry.textures.size()) return false;
            if (material->normalTexture != InvalidTextureIndex &&
                material->normalTexture >= textureRegistry.textures.size()) return false;
            if (material->occlusionTexture != InvalidTextureIndex &&
                material->occlusionTexture >= textureRegistry.textures.size()) return false;
            if (material->emissiveTexture != InvalidTextureIndex &&
                material->emissiveTexture >= textureRegistry.textures.size()) return false;
        }
        return !sceneRoots.empty();
    }
};

using ModelHandle = std::shared_ptr<const Model>;

} // namespace Iryven
