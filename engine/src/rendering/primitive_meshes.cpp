#include <iryven/rendering/primitive_meshes.h>

#include <cmath>
#include <numbers>
#include <stdexcept>

#include <glm/geometric.hpp>

namespace Iryven::PrimitiveMeshes {
namespace {

using MeshHandle = std::shared_ptr<const MeshData>;

void RequireSegments(std::uint32_t segments) {
    if (segments < 3) throw std::invalid_argument("A primitive requires at least three segments");
}

Vertex At(const glm::vec3& position, const glm::vec3& normal) {
    return Vertex{ .position = position, .normal = normal };
}

} // namespace

MeshHandle Cube() {
    static const MeshHandle mesh = [] {
        MeshData result;
        const auto face = [&result](glm::vec3 normal, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
            const auto start = static_cast<std::uint32_t>(result.vertices.size());
            result.vertices.insert(result.vertices.end(), {
                At(a, normal), At(b, normal), At(c, normal), At(d, normal)
            });
            result.indices.insert(result.indices.end(), {
                start, start + 1, start + 2, start, start + 2, start + 3
            });
        };
        face({ 0, 0, 1 }, {-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f});
        face({ 0, 0,-1 }, { 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f});
        face({-1, 0, 0 }, {-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f,-0.5f});
        face({ 1, 0, 0 }, { 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f});
        face({ 0, 1, 0 }, {-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f});
        face({ 0,-1, 0 }, {-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f});
        return std::make_shared<const MeshData>(std::move(result));
    }();
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
        for (std::uint32_t segment = 0; segment <= segments; ++segment) {
            const float theta = tau * static_cast<float>(segment) / static_cast<float>(segments);
            const glm::vec3 normal{ std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta) };
            mesh.vertices.push_back(At(normal * 0.5f, normal));
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
        const glm::vec3 normal{ std::cos(angle), 0.0f, std::sin(angle) };
        mesh.vertices.push_back(At({ normal.x * 0.5f, -0.5f, normal.z * 0.5f }, normal));
        mesh.vertices.push_back(At({ normal.x * 0.5f,  0.5f, normal.z * 0.5f }, normal));
    }
    const std::uint32_t bottomRing = static_cast<std::uint32_t>(mesh.vertices.size());
    for (std::uint32_t i = 0; i < segments; ++i) {
        const float angle = tau * static_cast<float>(i) / static_cast<float>(segments);
        mesh.vertices.push_back(At({ 0.5f * std::cos(angle), -0.5f, 0.5f * std::sin(angle) }, { 0,-1,0 }));
    }
    const std::uint32_t topRing = static_cast<std::uint32_t>(mesh.vertices.size());
    for (std::uint32_t i = 0; i < segments; ++i) {
        const float angle = tau * static_cast<float>(i) / static_cast<float>(segments);
        mesh.vertices.push_back(At({ 0.5f * std::cos(angle), 0.5f, 0.5f * std::sin(angle) }, { 0,1,0 }));
    }
    const std::uint32_t bottomCenter = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(At({ 0,-0.5f,0 }, { 0,-1,0 }));
    const std::uint32_t topCenter = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(At({ 0,0.5f,0 }, { 0,1,0 }));
    for (std::uint32_t i = 0; i < segments; ++i) {
        const std::uint32_t next = (i + 1) % segments;
        const std::uint32_t bottom = i * 2, top = bottom + 1;
        const std::uint32_t nextBottom = next * 2, nextTop = nextBottom + 1;
        mesh.indices.insert(mesh.indices.end(), {
            bottom, nextTop, nextBottom, bottom, top, nextTop,
            bottomCenter, bottomRing + i, bottomRing + next,
            topCenter, topRing + next, topRing + i
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
        const glm::vec3 normal = glm::normalize(glm::vec3{ std::cos(angle), 0.5f, std::sin(angle) });
        mesh.vertices.push_back(At({ 0.5f * std::cos(angle), -0.5f, 0.5f * std::sin(angle) }, normal));
        mesh.vertices.push_back(At({ 0,0.5f,0 }, normal));
    }
    const std::uint32_t baseRing = static_cast<std::uint32_t>(mesh.vertices.size());
    for (std::uint32_t i = 0; i < segments; ++i) {
        const float angle = tau * static_cast<float>(i) / static_cast<float>(segments);
        mesh.vertices.push_back(At({ 0.5f * std::cos(angle), -0.5f, 0.5f * std::sin(angle) }, { 0,-1,0 }));
    }
    const std::uint32_t center = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(At({ 0,-0.5f,0 }, { 0,-1,0 }));
    for (std::uint32_t i = 0; i < segments; ++i) {
        const std::uint32_t next = (i + 1) % segments;
        mesh.indices.insert(mesh.indices.end(), {
            i * 2, i * 2 + 1, next * 2,
            center, baseRing + i, baseRing + next
        });
    }
    return std::make_shared<const MeshData>(std::move(mesh));
}

MeshHandle Plane() {
    static const MeshHandle mesh = std::make_shared<const MeshData>(MeshData{
        .vertices = {
            At({-0.5f,0,-0.5f}, {0,1,0}), At({0.5f,0,-0.5f}, {0,1,0}),
            At({0.5f,0,0.5f}, {0,1,0}), At({-0.5f,0,0.5f}, {0,1,0})
        },
        .indices = { 0, 2, 1, 0, 3, 2 }
    });
    return mesh;
}

} // namespace Iryven::PrimitiveMeshes
