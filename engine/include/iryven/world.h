#pragma once

#include <cstddef>
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

class World {
public:
    World();
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    Entity CreateEntity(std::string_view name);

    template<typename... Components>
    void AddSystem(
        std::string_view name,
        std::function<void(float, Components&...)> function);

    bool Progress(float deltaTime = 0.0f);

    [[nodiscard]] RenderScene ExtractRenderScene() const;
private:
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
