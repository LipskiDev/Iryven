#include <iryven/world.h>

#include <string>
#include <functional>

#include <glm/matrix.hpp>
#include <glm/geometric.hpp>

#include <iryven/scene/components/components.h>
#include "../physics/physics_world.h"


namespace Iryven {

	World::World() : physics_(std::make_unique<PhysicsWorld>(world_)) {}
	World::~World() = default;

	Entity World::CreateEntity(std::string_view name)
	{
		return Entity{ world_.entity(std::string{name}.c_str()) };
	}

	bool World::Progress(float deltaTime)
	{
		const bool result = world_.progress(deltaTime);
		physics_->Update(deltaTime);
		return result;
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
}
