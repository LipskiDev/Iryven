#include <iryven/iryven.h>

#include "development/development_session.h"

#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

#include <glm/ext/matrix_transform.hpp>

namespace {

struct PlayerTag {
    bool enabled = true;
};

struct Collectible {
    bool collected = false;
};

Iryven::MaterialHandle MakeMaterial(std::string name, Iryven::Color color)
{
    auto material = std::make_shared<Iryven::Material>();
    material->name = std::move(name);
    material->baseColor = color;
    return material;
}

void AddBox(
    Iryven::World& world,
    const std::shared_ptr<const Iryven::MeshData>& mesh,
    const Iryven::MaterialHandle& material,
    std::string name,
    glm::vec3 position,
    glm::vec3 size)
{
    auto entity = world.CreateEntity(name);
    entity.Add<Iryven::Transform>(Iryven::Transform{
        .position = position,
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
        .title = "Iryven - Collect 5 | WASD to move",
        .width = 1280,
        .height = 720,
    });

    Iryven::World& world = engine.GetWorld();
    Iryven::InputHandler& input = engine.GetInput();

    auto font = std::make_shared<Iryven::Font>("assets/fonts/AppleGaramond.ttf");
    uint32_t collectedCount = 0;
    auto collectedCountUI = world.CreateEntity("FPS Counter");
    collectedCountUI.Add<Iryven::UIText>(
        Iryven::UIText{
            .font = font,
            .text = "Collected: 0",
            .position = glm::vec2{100.0, 100.0},
            .fontSize = 64.0f,
            .color = Iryven::Color::Black
        });

    const auto cubeMesh = Iryven::PrimitiveMeshes::Cube();
    const auto sphereMesh = Iryven::PrimitiveMeshes::Sphere();
    const auto groundMaterial = MakeMaterial("Ground", { 0.08f, 0.12f, 0.18f });
    const auto playerMaterial = MakeMaterial("Player", Iryven::Color::CornflowerBlue);
    const auto collectibleMaterial = MakeMaterial("Collectible", Iryven::Color::Orange);
    const auto winMaterial = MakeMaterial("Win", Iryven::Color::Green);

    AddBox(
        world,
        cubeMesh,
        groundMaterial,
        "Ground",
        { 0.0f, -0.3f, 0.0f },
        { 18.0f, 0.5f, 18.0f });

    auto player = world.CreateEntity("Player");
    player.Add<PlayerTag>();
    player.Add<Iryven::Transform>(Iryven::Transform{
        .position = { 0.0f, 0.5f, 0.0f },
        .scale = { 0.8f, 1.0f, 0.8f },
    });
    player.Add<Iryven::MeshRenderer>(cubeMesh, playerMaterial);

    constexpr std::array<glm::vec3, 5> collectiblePositions{
        glm::vec3{ -5.5f, 0.65f, -4.0f },
        glm::vec3{  5.0f, 0.65f, -5.0f },
        glm::vec3{  0.0f, 0.65f,  5.5f },
        glm::vec3{ -6.0f, 0.65f,  4.5f },
        glm::vec3{  6.0f, 0.65f,  3.5f },
    };

    std::array<Iryven::Entity, 5> collectibles;
    for (std::size_t index = 0; index < collectibles.size(); ++index) {
        collectibles[index] = world.CreateEntity(
            "Collectible " + std::to_string(index + 1));
        collectibles[index].Add<Collectible>();
        collectibles[index].Add<Iryven::Transform>(Iryven::Transform{
            .position = collectiblePositions[index],
            .scale = glm::vec3{ 0.55f },
        });
        collectibles[index].Add<Iryven::MeshRenderer>(
            sphereMesh, collectibleMaterial);
        collectibles[index].Add<Iryven::RigidBody>(Iryven::RigidBody{
            .type = Iryven::BodyType::Dynamic,
            .gravityScale = 0.0f,
            .fixedRotation = true,
        });
    }

    auto winBeacon = world.CreateEntity("Win Beacon");
    winBeacon.Add<Iryven::Transform>(Iryven::Transform{
        .position = { 0.0f, 2.0f, 0.0f },
        .scale = glm::vec3{ 0.0f },
    });
    winBeacon.Add<Iryven::MeshRenderer>(sphereMesh, winMaterial);

	player.Add<Iryven::RigidBody>(Iryven::RigidBody{
		.type = Iryven::BodyType::Kinematic,
	});
	player.Add<Iryven::Collider>(
		Iryven::Collider::Box({ 0.4f, 0.5f, 0.4f }));

	bool won = false;
	for (auto& collectibleEntity : collectibles) {
		auto collider = Iryven::Collider::Sphere(0.55f);
		collider.OnTriggerEnter(
			[&collectedCount, &won, winBeacon, collectedCountUI](
				Iryven::Entity self,
				Iryven::Entity other) mutable {
				if (won || !other.Has<PlayerTag>()) {
					return;
				}

				auto& collectible = self.Get<Collectible>();
				if (collectible.collected) {
					return;
				}

				collectible.collected = true;
				self.Get<Iryven::Transform>().scale = glm::vec3{ 0.0f };
				++collectedCount;
                auto& text = collectedCountUI.Get<Iryven::UIText>();
                text.text = std::string("Collected: ");
                text.text.append(std::to_string(collectedCount));

				if (collectedCount == 5) {
					won = true;
					winBeacon.Get<Iryven::Transform>().scale = glm::vec3{ 2.0f };
                    text.text = std::string("YOU WIN! All 5 collected.");
                    text.position = glm::vec2{ 370.0, 300.0 };
                    text.color = Iryven::Color::Red;
				}
			});
		collider.OnTriggerExit(
			[](Iryven::Entity, Iryven::Entity) {
				// Ready for per-collectible exit behavior when the game needs it.
			});
		collectibleEntity.Add<Iryven::Collider>(std::move(collider));
	}

    auto camera = world.CreateEntity("Camera");
    camera.Add<Iryven::Transform>();
    camera.Add<Iryven::Camera>(Iryven::Camera{
        .verticalFov = 52.0f,
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

    world.AddSystem<PlayerTag, Iryven::Transform>(
        "Player Movement",
        [&input](float deltaTime, PlayerTag&, Iryven::Transform& transform) {
            glm::vec3 direction{};
            if (input.IsKeyDown(Iryven::Key::W) || input.IsKeyDown(Iryven::Key::Up)) {
                direction.z -= 1.0f;
            }
            if (input.IsKeyDown(Iryven::Key::S) || input.IsKeyDown(Iryven::Key::Down)) {
                direction.z += 1.0f;
            }
            if (input.IsKeyDown(Iryven::Key::A) || input.IsKeyDown(Iryven::Key::Left)) {
                direction.x -= 1.0f;
            }
            if (input.IsKeyDown(Iryven::Key::D) || input.IsKeyDown(Iryven::Key::Right)) {
                direction.x += 1.0f;
            }

            if (glm::dot(direction, direction) > 0.0f) {
                direction = glm::normalize(direction);
                transform.position += direction * 5.5f * deltaTime;
                transform.rotation = glm::angleAxis(
                    std::atan2(direction.x, direction.z),
                    glm::vec3{ 0.0f, 1.0f, 0.0f });
            }

            transform.position.x = glm::clamp(transform.position.x, -8.2f, 8.2f);
            transform.position.z = glm::clamp(transform.position.z, -8.2f, 8.2f);
        });

    world.AddSystem<PlayerTag, Iryven::Transform>(
        "Follow Camera",
        [camera](float, PlayerTag&, Iryven::Transform& playerTransform) mutable {
            const glm::vec3 cameraPosition =
                playerTransform.position + glm::vec3{ 0.0f, 10.0f, 10.0f };
            const glm::mat4 cameraWorld = glm::inverse(glm::lookAtRH(
                cameraPosition,
                playerTransform.position,
                glm::vec3{ 0.0f, 1.0f, 0.0f }));
            camera.Get<Iryven::Transform>() =
                Iryven::Transform::FromMatrix(cameraWorld);
        });

    std::cout << "Collect all 5 orange spheres. Move with WASD or arrow keys.\n";
    engine.Run();
}
