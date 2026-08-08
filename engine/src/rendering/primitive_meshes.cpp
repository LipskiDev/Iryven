#include <iryven/rendering/primitive_meshes.h>

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace Iryven::PrimitiveMeshes {
namespace {

using MeshHandle = std::shared_ptr<const MeshData>;

void RequireSegments(std::uint32_t segments) {
    if (segments < 3) {
        throw std::invalid_argument("A primitive requires at least three segments");
    }
}

Vertex At(float x, float y, float z) {
    return Vertex{ .position = { x, y, z } };
}

} // namespace

MeshHandle Cube() {
    static const MeshHandle mesh = std::make_shared<const MeshData>(MeshData{
        .vertices = {
            At(-0.5f, -0.5f, -0.5f), At(0.5f, -0.5f, -0.5f),
            At(0.5f, 0.5f, -0.5f), At(-0.5f, 0.5f, -0.5f),
            At(-0.5f, -0.5f, 0.5f), At(0.5f, -0.5f, 0.5f),
            At(0.5f, 0.5f, 0.5f), At(-0.5f, 0.5f, 0.5f)
        },
        .indices = {
            4, 5, 6, 4, 6, 7, 1, 0, 3, 1, 3, 2,
            0, 4, 7, 0, 7, 3, 5, 1, 2, 5, 2, 6,
            3, 7, 6, 3, 6, 2, 0, 1, 5, 0, 5, 4
        }
    });
    return mesh;
}

MeshHandle Sphere(std::uint32_t segments, std::uint32_t rings) {
    RequireSegments(segments);
    if (rings < 2) throw std::invalid_argument("A sphere requires at least two rings");

    MeshData mesh;
    mesh.vertices.reserve((rings + 1) * (segments + 1));
    mesh.indices.reserve(rings * segments * 6);
    constexpr float tau = 2.0f * std::numbers::pi_v<float>;
    for (std::uint32_t ring = 0; ring <= rings; ++ring) {
        const float phi = std::numbers::pi_v<float> * static_cast<float>(ring) / static_cast<float>(rings);
        const float y = 0.5f * std::cos(phi);
        const float radius = 0.5f * std::sin(phi);
        for (std::uint32_t segment = 0; segment <= segments; ++segment) {
            const float theta = tau * static_cast<float>(segment) / static_cast<float>(segments);
            mesh.vertices.push_back(At(radius * std::cos(theta), y, radius * std::sin(theta)));
        }
    }
    const std::uint32_t stride = segments + 1;
    for (std::uint32_t ring = 0; ring < rings; ++ring) {
        for (std::uint32_t segment = 0; segment < segments; ++segment) {
            const std::uint32_t a = ring * stride + segment;
            const std::uint32_t b = a + stride;
            mesh.indices.insert(mesh.indices.end(), { a, a + 1, b + 1, a, b + 1, b });
        }
    }
    return std::make_shared<const MeshData>(std::move(mesh));
}

MeshHandle Cylinder(std::uint32_t segments) {
    RequireSegments(segments);
    MeshData mesh;
    constexpr float tau = 2.0f * std::numbers::pi_v<float>;
    for (std::uint32_t i = 0; i < segments; ++i) {
        const float angle = tau * static_cast<float>(i) / static_cast<float>(segments);
        const float x = 0.5f * std::cos(angle);
        const float z = 0.5f * std::sin(angle);
        mesh.vertices.push_back(At(x, -0.5f, z));
        mesh.vertices.push_back(At(x, 0.5f, z));
    }
    const std::uint32_t bottomCenter = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(At(0.0f, -0.5f, 0.0f));
    const std::uint32_t topCenter = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(At(0.0f, 0.5f, 0.0f));
    for (std::uint32_t i = 0; i < segments; ++i) {
        const std::uint32_t next = (i + 1) % segments;
        const std::uint32_t bottom = i * 2;
        const std::uint32_t top = bottom + 1;
        const std::uint32_t nextBottom = next * 2;
        const std::uint32_t nextTop = nextBottom + 1;
        mesh.indices.insert(mesh.indices.end(), {
            bottom, nextBottom, nextTop, bottom, nextTop, top,
            bottomCenter, nextBottom, bottom,
            topCenter, top, nextTop
        });
    }
    return std::make_shared<const MeshData>(std::move(mesh));
}

MeshHandle Cone(std::uint32_t segments) {
    RequireSegments(segments);
    MeshData mesh;
    constexpr float tau = 2.0f * std::numbers::pi_v<float>;
    for (std::uint32_t i = 0; i < segments; ++i) {
        const float angle = tau * static_cast<float>(i) / static_cast<float>(segments);
        mesh.vertices.push_back(At(0.5f * std::cos(angle), -0.5f, 0.5f * std::sin(angle)));
    }
    const std::uint32_t apex = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(At(0.0f, 0.5f, 0.0f));
    const std::uint32_t center = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(At(0.0f, -0.5f, 0.0f));
    for (std::uint32_t i = 0; i < segments; ++i) {
        const std::uint32_t next = (i + 1) % segments;
        mesh.indices.insert(mesh.indices.end(), { i, next, apex, center, next, i });
    }
    return std::make_shared<const MeshData>(std::move(mesh));
}

MeshHandle Plane() {
    static const MeshHandle mesh = std::make_shared<const MeshData>(MeshData{
        .vertices = {
            At(-0.5f, 0.0f, -0.5f), At(0.5f, 0.0f, -0.5f),
            At(0.5f, 0.0f, 0.5f), At(-0.5f, 0.0f, 0.5f)
        },
        .indices = { 0, 2, 1, 0, 3, 2 }
    });
    return mesh;
}

} // namespace Iryven::PrimitiveMeshes
