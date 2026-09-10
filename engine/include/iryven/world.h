#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <flecs.h>
#include <iryven/entity.h>
#include <iryven/rendering/render_scene.h>

namespace Iryven {

class PhysicsWorld;
class AsynchronousLoader;

class World {
public:
    World();
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    Entity CreateEntity(std::string_view name);
    // Visit entities created through the scene API outside World::Progress.
    void ForEachEntity(const std::function<void(Entity)>& visitor);

    template<typename... Components>
    void AddSystem(
        std::string_view name,
        std::function<void(float, Components&...)> function);

    bool Progress(float deltaTime = 0.0f);
    void ResetPhysics();
    void ResolveAssetReferences(const AsynchronousLoader& loader);

    [[nodiscard]] RenderScene ExtractRenderScene() const;

	void SerializeScene(const std::filesystem::path& path) const;
	// Imports into the existing world using Flecs merge semantics; does not clear it.
	// Component values require registered reflection. Parse errors may leave partial changes.
	void DeserializeScene(const std::filesystem::path& path);
	[[nodiscard]] flecs::world& GetFlecsWorld() { return world_; }
	[[nodiscard]] const flecs::world& GetFlecsWorld() const { return world_; }
private:
    struct SceneEntityTag {};

    flecs::world world_;
	std::unique_ptr<PhysicsWorld> physics_;
};


template<typename... Components>
void World::AddSystem(
	std::string_view name,
	std::function<void(float, Components&...)> function)
{
	const std::string ownedName{ name };

	world_.system<Components...>(ownedName.c_str())
		.each(
			[callback = std::move(function)](
				flecs::iter& iterator,
				std::size_t,
				Components&... components)
			{
				callback(iterator.delta_time(), components...);
			});
}

} // namespace Iryven
