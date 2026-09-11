#include <iryven/iryven.h>

#include "development/development_session.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

#include <glm/ext/matrix_transform.hpp>
#include <imgui.h>

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

	class FpsOverlay final : public Iryven::Layer {
	public:
		explicit FpsOverlay(const Iryven::Engine& engine)
			: Layer("CPU Timing Overlay"), engine_(engine) {}

		void OnImGuiRender() override
		{
			const ImGuiIO& io = ImGui::GetIO();
			const Iryven::CpuFrameTimings& sample = engine_.GetCpuFrameTimings();
			if (!initialized_ && sample.frameMs > 0.0f) {
				timings_ = sample;
				initialized_ = true;
			} else if (initialized_) {
				constexpr float smoothing = 0.12f;
				const auto smooth = [](float& value, float next) {
					value += (next - value) * smoothing;
				};
				smooth(timings_.frameMs, sample.frameMs);
				smooth(timings_.eventsMs, sample.eventsMs);
				smooth(timings_.updateMs, sample.updateMs);
				smooth(timings_.beginFrameMs, sample.beginFrameMs);
				smooth(timings_.frameFenceWaitMs, sample.frameFenceWaitMs);
				smooth(timings_.acquireImageMs, sample.acquireImageMs);
				smooth(timings_.uiBuildMs, sample.uiBuildMs);
				smooth(timings_.assetResolveMs, sample.assetResolveMs);
				smooth(timings_.sceneRenderMs, sample.sceneRenderMs);
				smooth(timings_.sceneExtractionMs, sample.sceneExtractionMs);
				smooth(timings_.frameGraphMs, sample.frameGraphMs);
				smooth(timings_.frameGraphSchedulingMs, sample.frameGraphSchedulingMs);
				smooth(timings_.commandAcquireMs, sample.commandAcquireMs);
				smooth(timings_.commandBeginMs, sample.commandBeginMs);
				smooth(timings_.resourceSetupMs, sample.resourceSetupMs);
				smooth(timings_.preRenderMs, sample.preRenderMs);
				smooth(timings_.uploadLightsMs, sample.uploadLightsMs);
				smooth(timings_.uploadMaterialsMs, sample.uploadMaterialsMs);
				smooth(timings_.uploadFrameDataMs, sample.uploadFrameDataMs);
				smooth(timings_.renderingSetupMs, sample.renderingSetupMs);
				smooth(timings_.drawRecordMs, sample.drawRecordMs);
				smooth(timings_.commandEndMs, sample.commandEndMs);
				smooth(timings_.queueSubmitMs, sample.queueSubmitMs);
				smooth(timings_.uiDrawMs, sample.uiDrawMs);
				smooth(timings_.presentMs, sample.presentMs);
			}

			std::array<std::array<char, 128>, 9> lines{};
			std::snprintf(lines[0].data(), lines[0].size(), "%.0f FPS | CPU %.2f ms",
				io.Framerate, timings_.frameMs);
			std::snprintf(lines[1].data(), lines[1].size(), "Events %.2f | Update %.2f ms",
				timings_.eventsMs, timings_.updateMs);
			std::snprintf(lines[2].data(), lines[2].size(),
				"Begin %.2f | Fence %.2f | Acquire %.2f ms",
				timings_.beginFrameMs, timings_.frameFenceWaitMs,
				timings_.acquireImageMs);
			std::snprintf(lines[3].data(), lines[3].size(),
				"Scene %.2f | Extract %.2f | FrameGraph %.2f ms",
				timings_.sceneRenderMs, timings_.sceneExtractionMs,
				timings_.frameGraphMs);
			std::snprintf(lines[4].data(), lines[4].size(),
				"FG schedule %.2f | AcquireCmd %.2f | BeginCmd %.2f ms",
				timings_.frameGraphSchedulingMs, timings_.commandAcquireMs,
				timings_.commandBeginMs);
			std::snprintf(lines[5].data(), lines[5].size(),
				"Resources %.2f | PreRender %.2f ms",
				timings_.resourceSetupMs, timings_.preRenderMs);
			std::snprintf(lines[6].data(), lines[6].size(),
				"Uploads: lights %.2f | materials %.2f | frame %.2f ms",
				timings_.uploadLightsMs, timings_.uploadMaterialsMs,
				timings_.uploadFrameDataMs);
			std::snprintf(lines[7].data(), lines[7].size(),
				"RenderSetup %.2f | Draws %.2f | EndCmd %.2f | Submit %.2f ms",
				timings_.renderingSetupMs, timings_.drawRecordMs,
				timings_.commandEndMs, timings_.queueSubmitMs);
			std::snprintf(lines[8].data(), lines[8].size(),
				"UI %.2f | Assets %.2f | Present %.2f ms",
				timings_.uiBuildMs + timings_.uiDrawMs,
				timings_.assetResolveMs, timings_.presentMs);

			constexpr float margin = 8.0f;
			constexpr float padding = 5.0f;
			constexpr float lineSpacing = 2.0f;
			float width = 0.0f;
			for (const auto& line : lines) {
				width = std::max(width, ImGui::CalcTextSize(line.data()).x);
			}
			const float lineHeight = ImGui::GetTextLineHeight();
			const float height = lineHeight * static_cast<float>(lines.size())
				+ lineSpacing * static_cast<float>(lines.size() - 1);
			const ImVec2 textPosition{
				io.DisplaySize.x - margin - width,
				margin
			};
			ImDrawList* drawList = ImGui::GetForegroundDrawList();
			drawList->AddRectFilled(
				{textPosition.x - padding, textPosition.y - padding},
				{textPosition.x + width + padding, textPosition.y + height + padding},
				IM_COL32(0, 0, 0, 150), 3.0f);
			for (std::size_t index = 0; index < lines.size(); ++index) {
				drawList->AddText(
					{textPosition.x,
					 textPosition.y + static_cast<float>(index) * (lineHeight + lineSpacing)},
					IM_COL32(235, 235, 235, 255), lines[index].data());
			}
		}

	private:
		const Iryven::Engine& engine_;
		Iryven::CpuFrameTimings timings_{};
		bool initialized_ = false;
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
		.title = "Iryven - Sandbox",
		.width = 1920,
		.height = 1080,
		.enableImGui = true,
		});
	engine.PushOverlay(std::make_unique<FpsOverlay>(engine));

	auto sponzaAsset = engine.GetAsyncLoader().RequestModel("assets/models/sponza/Sponza.gltf");

	auto sponzaEntity = engine.GetWorld().CreateEntity("Sponza");
	sponzaEntity.Add<Iryven::Transform>(Iryven::Transform{
	});
	//sponzaEntity.Add<Iryven::MeshRenderer>(sponzaAsset);

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
		.position = glm::vec3{ 0.0f, 1.0f, 0.0f },
		.rotation = glm::quat{
			glm::vec3{ glm::radians(-55.0f), glm::radians(-35.0f), 0.0f }
		},
	});

	light.Add<Iryven::Light>(Iryven::Light{
		.type = Iryven::LightType::Directional,
		.color = Iryven::Color::White,
		.intensity = 10.5f,
		.range = 10.0,
	});

	auto cloth = world.CreateEntity("Cloth");
	cloth.Add<Iryven::Transform>(Iryven::Transform{
		.position = glm::vec3{0.0f, 2.0f, -3.0f},
	});
	cloth.Add<Iryven::Cloth>(Iryven::Cloth{
		.resolution = {32u, 32u},
		.size = {2.0f, 2.0f},
		.stiffness = 0.1f,
		.damping = 0.01f,
		.material = MakeMaterial(
			"Cloth", Iryven::Color{0.65f, 0.08f, 0.06f, 1.0f}, 0.75f),
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
