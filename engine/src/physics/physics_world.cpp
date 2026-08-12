#include "physics_world.h"
#include <box3d/types.h>
#include <box3d/box3d.h>
#include <iryven/scene/components/components.h>

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace {

bool SameRigidBody(const Iryven::RigidBody& a, const Iryven::RigidBody& b)
{
	return a.type == b.type &&
		a.gravityScale == b.gravityScale &&
		a.fixedRotation == b.fixedRotation;
}

bool SameCollider(const Iryven::Collider& a, const Iryven::Collider& b)
{
	return a.type == b.type &&
		a.halfExtents == b.halfExtents &&
		a.radius == b.radius &&
		a.halfHeight == b.halfHeight &&
		a.density == b.density &&
		a.friction == b.friction &&
		a.restitution == b.restitution &&
		a.sensor == b.sensor &&
		a.categoryBits == b.categoryBits &&
		a.maskBits == b.maskBits;
}

b3Pos ToBox3D(const glm::vec3& position)
{
	return { position.x, position.y, position.z };
}

b3Quat ToBox3D(const glm::quat& rotation)
{
	return { { rotation.x, rotation.y, rotation.z }, rotation.w };
}

} // namespace

namespace Iryven {
	PhysicsWorld::PhysicsWorld(flecs::world& world) : entities_(world)
	{
		b3WorldDef worldDef = b3DefaultWorldDef();
		worldDef.gravity = { 0.0f, -10.0f, 0.0f };
		worldId_ = b3CreateWorld(&worldDef);
	}

	PhysicsWorld::~PhysicsWorld()
	{
		b3DestroyWorld(worldId_);
	}

	void PhysicsWorld::Update(float deltaTime)
	{
		Prepare();
		PushTransforms();
		accumulator_ += std::min(deltaTime, 0.25f);

		while (accumulator_ >= timeStep_) {
			b3World_Step(worldId_, timeStep_, substepCount_);
			accumulator_ -= timeStep_;
		}
		SynchronizeTransforms();
	}

	void PhysicsWorld::Prepare()
	{
		std::unordered_set<flecs::entity_t> activeEntities;
		entities_
			.query<Transform, RigidBody, Collider>()
			.each([this, &activeEntities](flecs::entity entity,
				Transform& transform,
				RigidBody& body,
				Collider& collider)
				{
					activeEntities.insert(entity.id());
					EnsureBodyExists(entity, transform, body, collider);
				});

		for (auto body = bodies_.begin(); body != bodies_.end();) {
			if (!activeEntities.contains(body->first)) {
				if (b3Body_IsValid(body->second.id)) {
					b3DestroyBody(body->second.id);
				}
				body = bodies_.erase(body);
			} else {
				++body;
			}
		}
	}

	void PhysicsWorld::PushTransforms()
	{
		entities_
			.query<const Transform, const RigidBody>()
			.each([this](flecs::entity entity,
				const Transform& transform,
				const RigidBody& rigidBody)
				{
					if (rigidBody.type == BodyType::Dynamic) {
						return;
					}

					const auto body = bodies_.find(entity.id());
					if (body == bodies_.end() || !b3Body_IsValid(body->second.id)) {
						return;
					}

					b3Body_SetTransform(
						body->second.id,
						ToBox3D(transform.position),
						ToBox3D(glm::normalize(transform.rotation)));
				});
	}

	void PhysicsWorld::SynchronizeTransforms()
	{
		entities_
			.query<Transform, const RigidBody>()
			.each([this](flecs::entity entity,
				Transform& transform,
				const RigidBody& rigidBody)
				{
					if (rigidBody.type != BodyType::Dynamic) {
						return;
					}

					const auto body = bodies_.find(entity.id());
					if (body == bodies_.end() || !b3Body_IsValid(body->second.id)) {
						return;
					}

					const b3Pos position = b3Body_GetPosition(body->second.id);
					const b3Quat rotation = b3Body_GetRotation(body->second.id);

					transform.position = {
						static_cast<float>(position.x),
						static_cast<float>(position.y),
						static_cast<float>(position.z)
					};
					transform.rotation = glm::normalize(glm::quat{
						rotation.s,
						rotation.v.x,
						rotation.v.y,
						rotation.v.z
					});
				});
	}

	void PhysicsWorld::EnsureBodyExists(flecs::entity& entity, Transform& transform, RigidBody& rb, Collider& collider)
	{
		const flecs::entity_t entityId = entity.id();
		if (const auto existing = bodies_.find(entityId);
			existing != bodies_.end()) {
			if (SameRigidBody(existing->second.rigidBody, rb) &&
				SameCollider(existing->second.collider, collider)) {
				return;
			}

			if (b3Body_IsValid(existing->second.id)) {
				b3DestroyBody(existing->second.id);
			}
			bodies_.erase(existing);
		}

		b3BodyDef bodyDef = b3DefaultBodyDef();
		switch (rb.type) {
		case BodyType::Static:
			bodyDef.type = b3_staticBody;
			break;
		case BodyType::Kinematic:
			bodyDef.type = b3_kinematicBody;
			break;
		case BodyType::Dynamic:
			bodyDef.type = b3_dynamicBody;
			break;
		}

		bodyDef.position = ToBox3D(transform.position);
		bodyDef.rotation = ToBox3D(glm::normalize(transform.rotation));
		bodyDef.gravityScale = rb.gravityScale;
		bodyDef.motionLocks.angularX = rb.fixedRotation;
		bodyDef.motionLocks.angularY = rb.fixedRotation;
		bodyDef.motionLocks.angularZ = rb.fixedRotation;

		const b3BodyId bodyId = b3CreateBody(worldId_, &bodyDef);

		b3ShapeDef shapeDef = b3DefaultShapeDef();
		shapeDef.density = collider.density;
		shapeDef.baseMaterial.friction = collider.friction;
		shapeDef.baseMaterial.restitution = collider.restitution;
		shapeDef.isSensor = collider.sensor;
		shapeDef.enableSensorEvents = collider.sensor;
		shapeDef.filter.categoryBits = collider.categoryBits;
		shapeDef.filter.maskBits = collider.maskBits;

		switch (collider.type) {
		case ColliderType::Box: {
			if (collider.halfExtents.x <= 0.0f ||
				collider.halfExtents.y <= 0.0f ||
				collider.halfExtents.z <= 0.0f) {
				b3DestroyBody(bodyId);
				throw std::invalid_argument("Box collider half-extents must be positive");
			}
			const b3BoxHull box = b3MakeBoxHull(
				collider.halfExtents.x,
				collider.halfExtents.y,
				collider.halfExtents.z);
			b3CreateHullShape(bodyId, &shapeDef, &box.base);
			break;
		}
		case ColliderType::Sphere: {
			if (collider.radius <= 0.0f) {
				b3DestroyBody(bodyId);
				throw std::invalid_argument("Sphere collider radius must be positive");
			}
			const b3Sphere sphere = {
				.center = { 0.0f, 0.0f, 0.0f },
				.radius = collider.radius
			};
			b3CreateSphereShape(bodyId, &shapeDef, &sphere);
			break;
		}
		case ColliderType::Capsule: {
			if (collider.radius <= 0.0f || collider.halfHeight < 0.0f) {
				b3DestroyBody(bodyId);
				throw std::invalid_argument(
					"Capsule collider radius must be positive and half-height non-negative");
			}
			const b3Capsule capsule = {
				.center1 = { 0.0f, -collider.halfHeight, 0.0f },
				.center2 = { 0.0f, collider.halfHeight, 0.0f },
				.radius = collider.radius
			};
			b3CreateCapsuleShape(bodyId, &shapeDef, &capsule);
			break;
		}
		}

		bodies_.emplace(entityId, BodyRecord{
			.id = bodyId,
			.rigidBody = rb,
			.collider = collider,
		});
	}
}
