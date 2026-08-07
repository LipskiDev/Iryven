#include <iryven/world.h>

#include <string>

#include <glm/matrix.hpp>

#include <iryven/scene/components/components.h>


namespace Iryven {
	Entity World::CreateEntity(std::string_view name)
	{
		return Entity{ world_.entity(std::string{name}.c_str()) };
	}

	bool World::Progress(float deltaTime)
	{
		return world_.progress(deltaTime);
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
				RenderObject object{
					.transform = transform.ToMatrix(),
					.mesh = meshRenderer.mesh,
				};

				scene.objects.push_back(object);
			}
		);

		return scene;
	}
}
