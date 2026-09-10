#include <iryven/world.h>

#include <string>
#include <functional>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include <glm/matrix.hpp>
#include <glm/geometric.hpp>

#include <iryven/scene/components/components.h>
#include <iryven/assets/asynchronous_loader.h>
#include "../physics/physics_world.h"


namespace Iryven {

	World::World() : physics_(std::make_unique<PhysicsWorld>(world_))
	{
		world_.component<SceneEntityTag>();
		world_.component<MeshRenderer>();
		world_.component<Collider>();
		world_.component<UIText>();

		world_.component<glm::vec3>()
			.member<float>("x", 0, offsetof(glm::vec3, x))
			.member<float>("y", 0, offsetof(glm::vec3, y))
			.member<float>("z", 0, offsetof(glm::vec3, z));

		world_.component<glm::vec4>()
			.member<float>("x", 0, offsetof(glm::vec4, x))
			.member<float>("y", 0, offsetof(glm::vec4, y))
			.member<float>("z", 0, offsetof(glm::vec4, z))
			.member<float>("w", 0, offsetof(glm::vec4, w));

		world_.component<glm::quat>()
			.member<float>("x", 0, offsetof(glm::quat, x))
			.member<float>("y", 0, offsetof(glm::quat, y))
			.member<float>("z", 0, offsetof(glm::quat, z))
			.member<float>("w", 0, offsetof(glm::quat, w));

		world_.component<Color>()
			.member<glm::vec4>("value", 0, offsetof(Color, value));

		world_.component<Transform>()
			.member<glm::vec3>("position", 0, offsetof(Transform, position))
			.member<glm::quat>("rotation", 0, offsetof(Transform, rotation))
			.member<glm::vec3>("scale", 0, offsetof(Transform, scale));

		world_.component<Camera>()
			.member<float>("verticalFov", 0, offsetof(Camera, verticalFov))
			.member<float>("nearPlane", 0, offsetof(Camera, nearPlane))
			.member<float>("farPlane", 0, offsetof(Camera, farPlane))
			.member<bool>("primary", 0, offsetof(Camera, primary));

		world_.component<Light>()
			.member<LightType>("type", 0, offsetof(Light, type))
			.member<Color>("color", 0, offsetof(Light, color))
			.member<float>("intensity", 0, offsetof(Light, intensity))
			.member<float>("range", 0, offsetof(Light, range))
			.member<float>("innerConeAngle", 0, offsetof(Light, innerConeAngle))
			.member<float>("outerConeAngle", 0, offsetof(Light, outerConeAngle))
			.member<bool>("enabled", 0, offsetof(Light, enabled));

		world_.component<RigidBody>()
			.member(&RigidBody::type, "type")
			.member(&RigidBody::gravityScale, "gravityScale")
			.member(&RigidBody::fixedRotation, "fixedRotation");
	}

	World::~World() = default;

	Entity World::CreateEntity(std::string_view name)
	{
		return Entity{ world_.entity(std::string{name}.c_str()).add<SceneEntityTag>() };
	}

	bool World::Progress(float deltaTime)
	{
		const bool result = world_.progress(deltaTime);
		physics_->Update(deltaTime);
		return result;
	}

	void World::ResetPhysics()
	{
		physics_ = std::make_unique<PhysicsWorld>(world_);
	}

	void World::ForEachEntity(const std::function<void(Entity)>& visitor)
	{
		world_.query<SceneEntityTag>().each([&visitor](flecs::entity entity, SceneEntityTag) {
			visitor(Entity{ entity });
		});
	}

	void World::ResolveAssetReferences(const AsynchronousLoader& loader)
	{
		auto renderableQuery = world_.query<MeshRenderer>();
		renderableQuery.each([&loader](MeshRenderer& meshRenderer) {
			if (meshRenderer.model || !meshRenderer.modelAsset) return;
			meshRenderer.model = loader.GetModel(meshRenderer.modelAsset);
		});
	}

	RenderScene World::ExtractRenderScene() const
	{
		RenderScene scene;

		auto cameraQuery = world_.query<const Transform, const Camera>();
		cameraQuery.each(
			[&scene](const Transform& transform, const Camera& camera)
			{
				if (scene.camera || !camera.primary) {
					return;
				}

				scene.camera = RenderCamera{
					.view = glm::inverse(transform.ToMatrix()),
					.verticalFov = camera.verticalFov,
					.nearPlane = camera.nearPlane,
					.farPlane = camera.farPlane
				};
			}
		);

		auto renderableQuery = world_.query<const Transform, const MeshRenderer>();

		renderableQuery.each(
			[&scene](flecs::entity entity,
				const Transform& transform,
				const MeshRenderer& meshRenderer)
			{
				if (meshRenderer.model) {
					const ModelHandle& model = meshRenderer.model;
					std::function<void(std::uint32_t, const glm::mat4&)> visitNode;
					visitNode = [&](std::uint32_t nodeIndex, const glm::mat4& parentTransform) {
						const ModelNode& node = model->nodes[nodeIndex];
						const glm::mat4 objectTransform = parentTransform * node.localTransform;
						if (node.meshIndex != InvalidModelIndex) {
							for (const MeshPrimitive& primitive : model->meshes[node.meshIndex].primitives) {
								MaterialHandle material = meshRenderer.material;
								if (!material && primitive.materialIndex != InvalidModelIndex)
									material = model->materials[primitive.materialIndex];
								scene.objects.push_back(RenderObject{
									.transform = objectTransform,
									.model = model,
									.firstIndex = primitive.firstIndex,
									.indexCount = primitive.indexCount,
									.vertexOffset = primitive.vertexOffset,
									.material = std::move(material),
								});
							}
						}
						for (std::uint32_t child : node.children) visitNode(child, objectTransform);
					};
					for (std::uint32_t root : model->sceneRoots)
						visitNode(root, transform.ToMatrix());
					return;
				}
				RenderObject object{
					.transform = transform.ToMatrix(),
					.mesh = meshRenderer.mesh,
					.material = meshRenderer.material,
				};

				scene.objects.push_back(object);
			}
		);

		auto lightQuery = world_.query<const Transform, const Light>();
		lightQuery.each(
			[&scene](const Transform& transform, const Light& light)
			{
				if (!light.enabled) return;
				scene.lights.push_back(RenderLight{
					.type = light.type,
					.position = transform.position,
					.direction = glm::normalize(transform.rotation * glm::vec3{ 0.0f, 0.0f, -1.0f }),
					.color = light.color,
					.intensity = light.intensity,
					.range = light.range,
					.innerConeAngle = light.innerConeAngle,
					.outerConeAngle = light.outerConeAngle
				});
			}
		);

		auto textQuery = world_.query<const UIText>();
		textQuery.each(
			[&scene](const UIText& text)
			{
				if (!text.font || !text.font->IsValid() || text.text.empty()) {
					return;
				}

				scene.texts.push_back(RenderText{
					.font = text.font,
					.text = text.text,
					.position = text.position,
					.fontSize = text.fontSize,
					.color = text.color
				});
			}
		);

		return scene;
	}

	void World::SerializeScene(const std::filesystem::path& path) const
	{
		const auto json = world_.to_json();
		if (!json.c_str()) {
			throw std::runtime_error("Failed to serialize scene: " + path.string());
		}
		std::ofstream file{path, std::ios::binary};
		if (!file) {
			throw std::runtime_error("Failed to open file for writing: " + path.string());
		}
		file << json.c_str();
		file.close();
		if (!file) {
			throw std::runtime_error("Failed to write scene: " + path.string());
		}
	}

	void World::DeserializeScene(const std::filesystem::path& path)
	{
		std::ifstream file{path, std::ios::binary};
		if (!file) {
			throw std::runtime_error("Failed to open scene for reading: " + path.string());
		}
		const std::string json{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
		if (file.bad()) {
			throw std::runtime_error("Failed to read scene: " + path.string());
		}
		if (json.find_first_not_of(" \t\r\n") == std::string::npos ||
			json.find('\0') != std::string::npos) {
			throw std::runtime_error("Invalid scene JSON: " + path.string());
		}
		const char* remaining = world_.from_json(json.c_str());
		if (!remaining || std::string_view{remaining}.find_first_not_of(" \t\r\n") != std::string_view::npos) {
			throw std::runtime_error("Failed to deserialize scene: " + path.string());
		}
	}
}
