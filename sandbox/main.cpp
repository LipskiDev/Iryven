#include <iryven/iryven.h>

#include "development/development_session.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include <glm/ext/matrix_transform.hpp>
#include <imgui.h>

struct Block {
	float min;
	float max;
	float delay;
	float elapsedTime = 0.0f;
	bool hidden = true;
};

struct Tree {
	float min;
	float max;
	float delay;
	float elapsedTime = 0.0f;
	bool hidden = true;
};

class FpsOverlay final : public Iryven::Layer {
public:
	explicit FpsOverlay(Iryven::Engine& engine)
		: Layer("CPU Timing Overlay"), engine_(engine) {}

	void OnImGuiRender() override
	{
		ImGui::Begin("Lighting debug");
		if (ImGui::Checkbox("Lights affecting pixel (opaque)", &lightCountView_))
			engine_.SetLightCountView(lightCountView_);
		if (lightCountView_) {
			ImGui::TextUnformatted("Positive light energy, in range/cone, facing surface.");
			const ImVec4 colors[]{ {0,0,0,1},{0,0,1,1},{0,1,1,1},{0,1,0,1},{1,1,0,1},{1,0,0,1},{1,0,1,1} };
			const char* labels[]{ "0", "1-4", "5-8", "9-16", "17-32", "33-64", "65+" };
			for (int i = 0; i < 7; ++i) {
				ImGui::PushID(i);
				ImGui::ColorButton("##band", colors[i], ImGuiColorEditFlags_NoTooltip, ImVec2(18, 18));
				ImGui::SameLine(); ImGui::TextUnformatted(labels[i]); ImGui::PopID();
			}
			ImGui::TextUnformatted("Diagnostic mode skips BRDF work; timings are not representative.");
		}
		ImGui::End();
		const ImGuiIO& io = ImGui::GetIO();
		const Iryven::CpuFrameTimings& sample = engine_.GetCpuFrameTimings();
		if (!initialized_ && sample.frameMs > 0.0f) {
			timings_ = sample;
			initialized_ = true;
		}
		else if (initialized_) {
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
			{ textPosition.x - padding, textPosition.y - padding },
			{ textPosition.x + width + padding, textPosition.y + height + padding },
			IM_COL32(0, 0, 0, 150), 3.0f);
		for (std::size_t index = 0; index < lines.size(); ++index) {
			drawList->AddText(
				{ textPosition.x,
				 textPosition.y + static_cast<float>(index) * (lineHeight + lineSpacing) },
				IM_COL32(235, 235, 235, 255), lines[index].data());
		}
	}

private:
	Iryven::Engine& engine_;
	bool lightCountView_ = false;
	Iryven::CpuFrameTimings timings_{};
	bool initialized_ = false;
};

int main()
{
	Iryven::Development::DevelopmentSession development;

	Iryven::Engine engine({
		.title = "Iryven - Sandbox",
		.width = 1920,
		.height = 1080,
		.enableImGui = true,
		.vsync = true,
		});

	engine.PushOverlay(std::make_unique<FpsOverlay>(engine));

	auto blockAsset = engine.GetAsyncLoader().RequestModel("assets/models/block.glb");
	auto treeAsset = engine.GetAsyncLoader().RequestModel("assets/models/tree.glb");

	int gridSize = 20;
	for (int x = -gridSize; x <= gridSize; ++x) {
		for (int z = -gridSize; z <= gridSize; ++z) {

			if (x * x + z * z > (gridSize * gridSize - 1)) {
				continue; // Skip blocks outside the circle.
			}

			const std::string blockName = "Block " + std::to_string(x) + "," + std::to_string(z);
			auto blockEntity = engine.GetWorld().CreateEntity(blockName);
			blockEntity.Add<Iryven::Transform>(Iryven::Transform{
				.position = glm::vec3{float(x) * 2.0f, -100.0f, float(z) * 2.0f},
				.scale = glm::vec3{1.0f, 1.0f, 1.0f},
				});
			blockEntity.Add<Iryven::MeshRenderer>(blockAsset);
			blockEntity.Add<Block>(Block{
				.min = -100.0f,
				.max = 0.0f,
				.delay = glm::length(glm::vec2(x, z)) * 0.2f,
				});

			float treeChance = 0.1f; // 10% chance to spawn a tree on each block.
			if (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) < treeChance) {
				const std::string treeName = "Tree " + std::to_string(x) + "," + std::to_string(z);
				auto treeEntity = engine.GetWorld().CreateEntity(treeName);
				treeEntity.Add<Iryven::Transform>(Iryven::Transform{
					.position = glm::vec3{float(x) * 2.0f, 0.0f, float(z) * 2.0f},
					.scale = glm::vec3{1.0f, 1.0f, 1.0f},
					});
				treeEntity.Add<Iryven::MeshRenderer>(treeAsset);
				treeEntity.Add<Tree>(Tree{
					.min = 0.0f,
					.max = 1.0f,
					.delay = glm::length(glm::vec2(x, z)) * 0.2f + 1.0f, // Trees appear slightly after blocks.
					});
			}
		}
	}


	Iryven::World& world = engine.GetWorld();
	Iryven::InputHandler& input = engine.GetInput();

	const glm::vec3 cameraPosition(50.0f, 40.0f, 50.0f);
	const glm::vec3 initialForward = glm::normalize(
		glm::vec3(0.0, 0.0, 0.0) - cameraPosition);
	auto camera = world.CreateEntity("Camera");
	camera.Add<Iryven::Transform>(Iryven::Transform{
		.position = cameraPosition,
		.rotation = glm::quatLookAt(initialForward, glm::vec3{0.0f, 1.0f, 0.0f}),
		.scale = glm::vec3{1.0f, 1.0f, 1.0f},
		});
	camera.Add<Iryven::Camera>(Iryven::Camera{
		.verticalFov = 52.0f,
	});

	//auto light = world.CreateEntity("Point Light");
	//light.Add<Iryven::Transform>(Iryven::Transform{
	//	.position = glm::vec3{0.0f, 10.0f, 0.0f},
	//	.scale = glm::vec3{1.0f, 1.0f, 1.0f},
	//	});
	//light.Add<Iryven::Light>(Iryven::Light{
	//	.type = Iryven::LightType::Point,
	//	.color = Iryven::Color{1.0f, 1.0f, 1.0f, 1.0f},
	//	.intensity = 30.0f,
	//	.range = 100.0f,
	//});

	// Fly camera: arrows look, WASD moves, and Space/Shift changes height.
	world.AddSystem<Iryven::Transform, Iryven::Camera>("Camera Control",
		[&input,
		 yaw = std::atan2(-initialForward.x, -initialForward.z),
		 pitch = std::asin(glm::clamp(initialForward.y, -1.0f, 1.0f))]
		(float deltaTime, Iryven::Transform& transform, Iryven::Camera&) mutable {
			if (deltaTime <= 0.0f) return;

			constexpr float lookSpeed = 90.0f;
			const float yawInput = float(input.IsKeyDown(Iryven::Key::Left))
				- float(input.IsKeyDown(Iryven::Key::Right));
			const float pitchInput = float(input.IsKeyDown(Iryven::Key::Up))
				- float(input.IsKeyDown(Iryven::Key::Down));
			yaw += glm::radians(lookSpeed) * yawInput * deltaTime;
			pitch = glm::clamp(
				pitch + glm::radians(lookSpeed) * pitchInput * deltaTime,
				glm::radians(-89.0f), glm::radians(89.0f));
			transform.rotation = glm::normalize(
				glm::angleAxis(yaw, glm::vec3{0.0f, 1.0f, 0.0f})
				* glm::angleAxis(pitch, glm::vec3{1.0f, 0.0f, 0.0f}));

			glm::vec3 moveInput{
				float(input.IsKeyDown(Iryven::Key::D)) - float(input.IsKeyDown(Iryven::Key::A)),
				float(input.IsKeyDown(Iryven::Key::Space)) - float(input.IsKeyDown(Iryven::Key::LeftShift)),
				float(input.IsKeyDown(Iryven::Key::S)) - float(input.IsKeyDown(Iryven::Key::W)),
			};
			if (glm::dot(moveInput, moveInput) > 0.0f) {
				moveInput = glm::normalize(moveInput);
				constexpr float movementSpeed = 50.0f;
				const glm::vec3 localMovement = transform.rotation
					* glm::vec3{moveInput.x, 0.0f, moveInput.z};
				transform.position += (localMovement
					+ glm::vec3{0.0f, moveInput.y, 0.0f}) * movementSpeed * deltaTime;
			}
		});

	auto light = world.CreateEntity("Directional Light");
	light.Add<Iryven::Transform>(Iryven::Transform{
		.position = glm::vec3{0.0f, 10.0f, 0.0f},
		.rotation = glm::normalize(glm::angleAxis(glm::radians(-45.0f), glm::vec3{1.0f, 1.0f, 0.0f})),
	});
	light.Add<Iryven::Light>(Iryven::Light{
		.type = Iryven::LightType::Directional,
		.color = Iryven::Color{1.0f, 1.0f, 1.0f, 1.0f},
		.intensity = 10.0f,
	});

	//auto cloth = world.CreateEntity("Cloth");
	//cloth.Add<Iryven::Transform>(Iryven::Transform{
	//	.position = glm::vec3{0.0f, 2.0f, -3.0f},
	//});
	//cloth.Add<Iryven::Cloth>(Iryven::Cloth{
	//	.resolution = {32u, 32u},
	//	.size = {2.0f, 2.0f},
	//	.stiffness = 0.1f,
	//	.damping = 0.01f,
	//	.material = MakeMaterial(
	//		"Cloth", Iryven::Color{0.65f, 0.08f, 0.06f, 1.0f}, 0.75f),
	//});


	// Move block from -100 to 0 on Y axis over 1 second, using a nice tweening function.
	world.AddSystem<Iryven::Transform, Block, Iryven::MeshRenderer>("Block Animation",
		[&input](float deltaTime, Iryven::Transform& transform, Block& block, Iryven::MeshRenderer& meshRenderer) {
			block.elapsedTime += deltaTime;
			meshRenderer.hidden = block.hidden && (block.elapsedTime < block.delay);

			if (block.elapsedTime < block.delay) {
				// Wait for the delay before starting the animation.
				return;
			}
			block.hidden = false;
			meshRenderer.hidden = false;

			constexpr float duration = 1.0f;
			const float t = glm::clamp(
				(block.elapsedTime - block.delay) / duration,
				0.0f,
				1.0f
			);

			transform.position.y = Iryven::Tween::EaseInOutBack(block.min, block.max, t, 1.01f);

			if (input.IsKeyDown(Iryven::Key::R)) {
				// Reset the block animation when 'R' is pressed.
				block.elapsedTime = 0.0f;
				transform.position.y = block.min;
				block.hidden = true;
			}
		});

	world.AddSystem<Iryven::Transform, Tree, Iryven::MeshRenderer>("Tree Animation",
		[&input](float deltaTime, Iryven::Transform& transform, Tree& tree, Iryven::MeshRenderer& meshRenderer) {
			tree.elapsedTime += deltaTime;
			meshRenderer.hidden = tree.hidden && (tree.elapsedTime < tree.delay);
			if (tree.elapsedTime < tree.delay) {
				// Wait for the delay before starting the animation.
				return;
			}
			tree.hidden = false;
			meshRenderer.hidden = false;
			constexpr float duration = 0.4f;
			const float t = glm::clamp(
				(tree.elapsedTime - tree.delay) / duration,
				0.0f,
				1.0f
			);

			transform.scale = glm::vec3(Iryven::Tween::EaseInOutSine(0.0f, 1.0f, t));

			if (input.IsKeyDown(Iryven::Key::R)) {
				// Reset the tree animation when 'R' is pressed.
				tree.elapsedTime = 0.0f;
				transform.scale = glm::vec3(tree.min);
				tree.hidden = true;
			}
		});

	engine.Run();
}
