#include <iryven/iryven.h>

#include "development/development_session.h"

#include <cmath>
#include <memory>
#include <string>

#include <glm/ext/matrix_transform.hpp>

namespace {

constexpr float Pi = 3.14159265358979323846f;

Iryven::MaterialHandle MakeMaterial(
    std::string name,
    Iryven::Color color)
{
    auto material = std::make_shared<Iryven::Material>();
    material->name = std::move(name);
    material->baseColor = color;
    return material;
}

void AddStaticBox(
    Iryven::World& world,
    std::string name,
    const std::shared_ptr<const Iryven::MeshData>& mesh,
    const Iryven::MaterialHandle& material,
    const glm::vec3& position,
    const glm::vec3& size,
    float rotationZ = 0.0f)
{
    auto entity = world.CreateEntity(name);
    entity.Add<Iryven::Transform>(Iryven::Transform{
        .position = position,
        .rotation = glm::angleAxis(rotationZ, glm::vec3{ 0.0f, 0.0f, 1.0f }),
        .scale = size,
    });
    entity.Add<Iryven::MeshRenderer>(mesh, material);
    entity.Add<Iryven::RigidBody>(Iryven::RigidBody{
        .type = Iryven::BodyType::Static,
    });
    entity.Add<Iryven::Collider>(Iryven::Collider::Box(size * 0.5f));
}

} // namespace

int main()
{
    Iryven::Development::DevelopmentSession development;

    Iryven::Engine engine({
        .title = "Iryven - Washer Physics Sample",
        .width = 1280,
        .height = 720,
    });

    Iryven::World& world = engine.GetWorld();
    const auto cubeMesh = Iryven::PrimitiveMeshes::Cube();
    const auto sphereMesh = Iryven::PrimitiveMeshes::Sphere();
    const auto drumMaterial = MakeMaterial(
        "Washer Blue", Iryven::Color{ 0.04f, 0.22f, 0.48f });
    const auto particleMaterial = MakeMaterial(
        "Particle Orange", Iryven::Color{ 1.0f, 0.42f, 0.03f });
    const auto groundMaterial = MakeMaterial(
        "Ground", Iryven::Color{ 0.35f, 0.37f, 0.42f });

    auto camera = world.CreateEntity("Camera");
    const glm::vec3 cameraPosition{ 11.0f, 8.5f, 15.0f };
    const glm::mat4 cameraWorld = glm::inverse(glm::lookAtRH(
        cameraPosition,
        glm::vec3{ 0.0f, 5.0f, 0.0f },
        glm::vec3{ 0.0f, 1.0f, 0.0f }));
    camera.Add<Iryven::Transform>(
        Iryven::Transform::FromMatrix(cameraWorld));
    camera.Add<Iryven::Camera>(Iryven::Camera{
        .verticalFov = 50.0f,
    });

    auto light = world.CreateEntity("Directional Light");
    light.Add<Iryven::Transform>(Iryven::Transform{
        .rotation = glm::quat{
            glm::vec3{ glm::radians(-55.0f), glm::radians(-35.0f), 0.0f }
        },
    });
    light.Add<Iryven::Light>(Iryven::Light{
        .type = Iryven::LightType::Directional,
        .intensity = 2.5f,
    });

    AddStaticBox(
        world,
        "Ground",
        cubeMesh,
        groundMaterial,
        { 0.0f, -0.25f, 0.0f },
        { 24.0f, 0.5f, 18.0f });

    constexpr glm::vec3 drumCenter{ 0.0f, 5.0f, 0.0f };
    constexpr float drumRadius = 4.5f;
    constexpr float wallThickness = 0.45f;
    constexpr float drumDepth = 2.4f;
    constexpr int segmentCount = 48;
    const float segmentLength =
        2.0f * drumRadius * std::tan(Pi / segmentCount) + 0.08f;

    for (int index = 0; index < segmentCount; ++index) {
        const float angle = 2.0f * Pi * static_cast<float>(index) /
            static_cast<float>(segmentCount);
        const glm::vec3 position = drumCenter + glm::vec3{
            std::cos(angle) * drumRadius,
            std::sin(angle) * drumRadius,
            0.0f
        };

        AddStaticBox(
            world,
            "Drum Segment " + std::to_string(index),
            cubeMesh,
            drumMaterial,
            position,
            { segmentLength, wallThickness, drumDepth },
            angle + Pi * 0.5f);
    }

    // Three inward-facing paddles approximate the washer benchmark's baffles.
    constexpr float paddleLength = 2.15f;
    constexpr float paddleThickness = 0.28f;
    for (int index = 0; index < 3; ++index) {
        const float angle = Pi * 0.5f +
            2.0f * Pi * static_cast<float>(index) / 3.0f;
        const float paddleRadius = drumRadius - paddleLength * 0.5f;
        const glm::vec3 position = drumCenter + glm::vec3{
            std::cos(angle) * paddleRadius,
            std::sin(angle) * paddleRadius,
            0.0f
        };

        AddStaticBox(
            world,
            "Paddle " + std::to_string(index),
            cubeMesh,
            drumMaterial,
            position,
            { paddleLength, paddleThickness, drumDepth * 0.92f },
            angle);
    }

    constexpr float particleSize = 0.28f;
    constexpr int columns = 15;
    constexpr int rows = 7;
    constexpr int depthLayers = 4;
    int particleIndex = 0;

    for (int layer = 0; layer < depthLayers; ++layer) {
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                const float x =
                    (static_cast<float>(column) - (columns - 1) * 0.5f) *
                    particleSize * 1.08f +
                    (row % 2 == 0 ? 0.0f : particleSize * 0.5f);
                const float y = 1.35f +
                    static_cast<float>(row) * particleSize * 1.08f;
                const float z =
                    (static_cast<float>(layer) - (depthLayers - 1) * 0.5f) *
                    particleSize * 1.35f;

                // Keep the initial pile inside the circular drum.
                const glm::vec2 offset{ x, y - drumCenter.y };
                if (glm::dot(offset, offset) > 3.75f * 3.75f) {
                    continue;
                }

                auto particle = world.CreateEntity(
                    "Particle " + std::to_string(particleIndex++));
                particle.Add<Iryven::Transform>(Iryven::Transform{
                    .position = { x, y, z },
                    .rotation = glm::angleAxis(
                        0.13f * static_cast<float>((column + row) % 5),
                        glm::normalize(glm::vec3{ 1.0f, 0.7f, 0.3f })),
                    .scale = glm::vec3{ particleSize },
                });
                particle.Add<Iryven::MeshRenderer>(
                    sphereMesh, particleMaterial);
                particle.Add<Iryven::RigidBody>(Iryven::RigidBody{
                    .type = Iryven::BodyType::Dynamic,
                });
                auto collider = Iryven::Collider::Sphere(
                    particleSize * 0.5f);
                collider.density = 0.8f;
                collider.friction = 0.45f;
                particle.Add<Iryven::Collider>(collider);
            }
        }
    }

    engine.Run();
}
