#pragma once

#include <cstdint>

#include <glm/vec3.hpp>

namespace Iryven {

enum class ColliderType {
    Box,
    Sphere,
    Capsule
};

struct Collider {
    [[nodiscard]] static Collider Box(glm::vec3 halfExtents)
    {
        Collider collider;
        collider.type = ColliderType::Box;
        collider.halfExtents = halfExtents;
        return collider;
    }

    [[nodiscard]] static Collider Sphere(float radius)
    {
        Collider collider;
        collider.type = ColliderType::Sphere;
        collider.radius = radius;
        return collider;
    }

    [[nodiscard]] static Collider Capsule(float radius, float halfHeight)
    {
        Collider collider;
        collider.type = ColliderType::Capsule;
        collider.radius = radius;
        collider.halfHeight = halfHeight;
        return collider;
    }

    ColliderType type = ColliderType::Box;

    glm::vec3 halfExtents{ 0.5f };
    float radius = 0.5f;
    float halfHeight = 0.5f;

    float density = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;

    bool sensor = false;
    std::uint64_t categoryBits = ~std::uint64_t{ 0 };
    std::uint64_t maskBits = ~std::uint64_t{ 0 };
};

} // namespace Iryven
