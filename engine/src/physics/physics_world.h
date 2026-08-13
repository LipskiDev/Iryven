#pragma once

#include <flecs.h>
#include <box3d/id.h>
#include <iryven/scene/components/components.h>

#include <unordered_map>

namespace Iryven {
	class PhysicsWorld {
	public:
		explicit PhysicsWorld(flecs::world& world);
		~PhysicsWorld();

		PhysicsWorld(const PhysicsWorld&) = delete;
		PhysicsWorld& operator=(const PhysicsWorld&) = delete;

		void Update(float deltaTime);


	private:
		void Prepare();
		void PushTransforms();
		void SynchronizeTransforms();
		void EnsureBodyExists(flecs::entity& entity, Transform& transform, RigidBody& rb, Collider& collider);

		void DispatchTriggerEnter(flecs::entity_t self, flecs::entity_t other);
		void DispatchTriggerExit(flecs::entity_t self, flecs::entity_t other);
		void ProcessSensorEvents();
		void DispatchCollisionEnter(flecs::entity_t self, flecs::entity_t other);
		void DispatchCollisionExit(flecs::entity_t self, flecs::entity_t other);
		void ProcessContactEvents();

		flecs::entity_t GetEntityFromShape(b3ShapeId shapeId) const;

		struct BodyRecord {
			b3BodyId id{};
			b3ShapeId shapeId{};
			RigidBody rigidBody{};
			Collider collider{};
		};

	private:
		// Box3D state
		b3WorldId worldId_;
		flecs::world& entities_;
		const float timeStep_ = 1 / 60.0f;
		const uint32_t substepCount_ = 4;
		std::unordered_map<flecs::entity_t, BodyRecord> bodies_;

		float accumulator_ = 0.0f;

	};
}
