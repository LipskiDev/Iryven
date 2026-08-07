#pragma once

#include <flecs.h>
#include <utility>

namespace Iryven {
	struct Entity {
	public:
		Entity() = default;
		explicit Entity(flecs::entity entity) : entity_(entity) {}
		template<typename T, typename... Args>
		T& Add(Args&&... args);

		template<typename T>
		T& Get();

		template<typename T>
		bool Has() const;

		template<typename T>
		void Remove();

	private:
		flecs::entity entity_;

	};

	template<typename T, typename ...Args>
	inline T& Entity::Add(Args && ...args)
	{
		entity_.emplace<T>(std::forward<Args>(args)...);
		return entity_.get_mut<T>();
	}

	template<typename T>
	inline T& Entity::Get()
	{
		return entity_.get_mut<T>();
	}

	template<typename T>
	inline bool Entity::Has() const
	{
		return entity_.has<T>();
	}

	template<typename T>
	inline void Entity::Remove()
	{
		entity_.remove<T>();
	}

} // namespace Iryven
