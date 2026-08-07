#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include <flecs.h>
#include <iryven/entity.h>
#include <iryven/rendering/render_scene.h>

namespace Iryven {

class World {
public:
    World() = default;

    Entity CreateEntity(std::string_view name);

    template<typename... Components>
    void AddSystem(
        std::string_view name,
        std::function<void(float, Components&...)> function);

    bool Progress(float deltaTime = 0.0f);

    [[nodiscard]] RenderScene ExtractRenderScene() const;
private:
    flecs::world world_;
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
