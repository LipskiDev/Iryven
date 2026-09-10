#include <iryven/iryven.h>

#include "development/development_session.h"

#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

#include <glm/ext/matrix_transform.hpp>

namespace {

	struct CameraMotion {
		glm::vec3 velocity{ 0.0f };
		glm::vec2 lookVelocity{ 0.0f }; // Pitch and yaw in radians per second.
	};

	struct PlayerTag {
		bool enabled = true;
	};

	struct Collectible {
		bool collected = false;
	};

	Iryven::MaterialHandle MakeMaterial(std::string name, Iryven::Color color, float roughness = 0.0f, float metallic = 0.0f)
	{
		auto material = std::make_shared<Iryven::Material>();
		material->name = std::move(name);
		material->baseColor = color;
		material->roughness = roughness;
		material->metallic = metallic;
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
		.width = 1920,
		.height = 1080,
		});

	auto sponzaAsset = engine.GetAsyncLoader().RequestModel("assets/models/sponza/Sponza.gltf");

	auto sponzaEntity = engine.GetWorld().CreateEntity("Sponza");
	sponzaEntity.Add<Iryven::Transform>();
	sponzaEntity.Add<Iryven::MeshRenderer>(sponzaAsset);

	Iryven::World& world = engine.GetWorld();
	Iryven::InputHandler& input = engine.GetInput();

	auto camera = world.CreateEntity("Camera");
	camera.Add<Iryven::Transform>();
	camera.Add<CameraMotion>();
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
		.type = Iryven::LightType::Point,
		.color = Iryven::Color::Yellow,
		.intensity = 1.0f,
		.range = 10000.0f,
		});

	world.AddSystem<Iryven::Camera, Iryven::Transform, CameraMotion>("Camera Control", [&](float deltaTime, Iryven::Camera&, Iryven::Transform& transform, CameraMotion& motion) {
		if (deltaTime <= 0.0f) return;
		const float moveSpeed = 5.0f;
		const float lookSpeed = 60.0f; // Degrees per second.
		const float moveResponse = 12.0f; // Higher values respond faster.
		const float lookResponse = 16.0f;
		const float moveBlend = -std::expm1(-moveResponse * deltaTime);
		const float lookBlend = -std::expm1(-lookResponse * deltaTime);
		glm::vec3 moveDirection(0.0f);
		if (input.IsKeyDown(Iryven::Key::W)) moveDirection.z -= 1.0f;
		if (input.IsKeyDown(Iryven::Key::S)) moveDirection.z += 1.0f;
		if (input.IsKeyDown(Iryven::Key::A)) moveDirection.x -= 1.0f;
		if (input.IsKeyDown(Iryven::Key::D)) moveDirection.x += 1.0f;
		if (input.IsKeyDown(Iryven::Key::Space)) moveDirection.y += 1.0f;
		if (input.IsKeyDown(Iryven::Key::LeftShift)) moveDirection.y -= 1.0f;

		const glm::vec3 forward = glm::normalize(transform.rotation) * glm::vec3(0.0f, 0.0f, -1.0f);
		float pitch = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));
		float yaw = std::atan2(-forward.x, -forward.z);
		const float pitchInput = float(input.IsKeyDown(Iryven::Key::Up)) - float(input.IsKeyDown(Iryven::Key::Down));
		const float yawInput = float(input.IsKeyDown(Iryven::Key::Left)) - float(input.IsKeyDown(Iryven::Key::Right));
		const glm::vec2 targetLookVelocity = glm::vec2(pitchInput, yawInput) * glm::radians(lookSpeed);

		const glm::vec2 lookDelta = targetLookVelocity * deltaTime
			+ (motion.lookVelocity - targetLookVelocity) * (lookBlend / lookResponse);
		motion.lookVelocity += (targetLookVelocity - motion.lookVelocity) * lookBlend;
		const float nextPitch = pitch + lookDelta.x;
		pitch = glm::clamp(nextPitch, glm::radians(-89.0f), glm::radians(89.0f));
		if (pitch != nextPitch) motion.lookVelocity.x = 0.0f;
		yaw += lookDelta.y;

		transform.rotation = glm::normalize(
			glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
			glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f)));

		if (glm::length(moveDirection) > 0.0f) {
			moveDirection = glm::normalize(moveDirection);
		}
		const glm::vec3 targetVelocity = transform.rotation * moveDirection * moveSpeed;
		transform.position += targetVelocity * deltaTime
			+ (motion.velocity - targetVelocity) * (moveBlend / moveResponse);
		motion.velocity += (targetVelocity - motion.velocity) * moveBlend;
	});

	std::cout << "Collect all 5 ducks. Move with WASD or arrow keys.\n";
	engine.Run();
}
