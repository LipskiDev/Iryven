#include <iryven/iryven.h>

#include <cassert>

#include <glm/common.hpp>

namespace {

struct TriggerVisitorTag {
    bool enabled = true;
};

struct CollisionBodyATag {
    bool enabled = true;
};

struct CollisionBodyBTag {
    bool enabled = true;
};

} // namespace

int main()
{
    const Iryven::EngineConfig config{ .title = "Test" };
    assert(config.title == "Test");

    const auto box = Iryven::Collider::Box({ 1.0f, 2.0f, 3.0f });
    assert(box.type == Iryven::ColliderType::Box);
    assert(glm::all(glm::equal(
        box.halfExtents, glm::vec3{ 1.0f, 2.0f, 3.0f })));

    const auto sphere = Iryven::Collider::Sphere(2.0f);
    assert(sphere.type == Iryven::ColliderType::Sphere);
    assert(sphere.radius == 2.0f);

    const auto capsule = Iryven::Collider::Capsule(0.5f, 1.5f);
    assert(capsule.type == Iryven::ColliderType::Capsule);
    assert(capsule.radius == 0.5f);
    assert(capsule.halfHeight == 1.5f);

    Iryven::World world;

    auto ground = world.CreateEntity("Ground");
    ground.Add<Iryven::Transform>(Iryven::Transform{
        .position = { 0.0f, -0.5f, 0.0f },
    });
    ground.Add<Iryven::RigidBody>(Iryven::RigidBody{
        .type = Iryven::BodyType::Static,
    });
    ground.Add<Iryven::Collider>(
        Iryven::Collider::Box({ 5.0f, 0.5f, 5.0f }));

    auto fallingBody = world.CreateEntity("Falling Body");
    fallingBody.Add<Iryven::Transform>(Iryven::Transform{
        .position = { 0.0f, 4.0f, 0.0f },
    });
    fallingBody.Add<Iryven::RigidBody>(Iryven::RigidBody{
        .type = Iryven::BodyType::Dynamic,
    });
    fallingBody.Add<Iryven::Collider>(
        Iryven::Collider::Box({ 0.5f, 0.5f, 0.5f }));

    const float initialHeight =
        fallingBody.Get<Iryven::Transform>().position.y;
    for (int step = 0; step < 30; ++step) {
        world.Progress(1.0f / 60.0f);
    }
    assert(fallingBody.Get<Iryven::Transform>().position.y < initialHeight);

    // Removing and restoring a collider exercises backend body cleanup and
    // recreation without exposing Box3D handles to the test.
    fallingBody.Remove<Iryven::Collider>();
    world.Progress(1.0f / 60.0f);
    fallingBody.Add<Iryven::Collider>(
        Iryven::Collider::Sphere(0.5f));
    world.Progress(1.0f / 60.0f);

    bool triggerEntered = false;
    bool triggerExited = false;

    auto sensor = world.CreateEntity("Sensor");
    sensor.Add<Iryven::Transform>();
    sensor.Add<Iryven::RigidBody>(Iryven::RigidBody{
        .type = Iryven::BodyType::Dynamic,
        .gravityScale = 0.0f,
        .fixedRotation = true,
    });
    auto sensorCollider = Iryven::Collider::Sphere(1.0f);
    sensorCollider.OnTriggerEnter(
        [&triggerEntered](Iryven::Entity, Iryven::Entity other) {
            if (other.Has<TriggerVisitorTag>()) {
                triggerEntered = true;
            }
        });
    sensorCollider.OnTriggerExit(
        [&triggerExited](Iryven::Entity, Iryven::Entity other) {
            if (other.Has<TriggerVisitorTag>()) {
                triggerExited = true;
            }
        });
    assert(sensorCollider.sensor);
    sensor.Add<Iryven::Collider>(std::move(sensorCollider));

    auto visitor = world.CreateEntity("Trigger Visitor");
    visitor.Add<TriggerVisitorTag>();
    visitor.Add<Iryven::Transform>(Iryven::Transform{
        .position = { 4.0f, 0.0f, 0.0f },
    });
    visitor.Add<Iryven::RigidBody>(Iryven::RigidBody{
        .type = Iryven::BodyType::Kinematic,
    });
    visitor.Add<Iryven::Collider>(Iryven::Collider::Sphere(0.5f));

    world.Progress(1.0f / 60.0f);
    visitor.Get<Iryven::Transform>().position = { 0.0f, 0.0f, 0.0f };
    world.Progress(1.0f / 60.0f);
    assert(triggerEntered);

    visitor.Get<Iryven::Transform>().position = { 4.0f, 0.0f, 0.0f };
    world.Progress(1.0f / 60.0f);
    assert(triggerExited);

    bool collisionEnteredA = false;
    bool collisionEnteredB = false;
    bool collisionExitedA = false;
    bool collisionExitedB = false;

    auto collisionBodyA = world.CreateEntity("Collision Body A");
    collisionBodyA.Add<CollisionBodyATag>();
    collisionBodyA.Add<Iryven::Transform>(Iryven::Transform{
        .position = { 20.0f, 0.0f, 0.0f },
    });
    collisionBodyA.Add<Iryven::RigidBody>(Iryven::RigidBody{
        .type = Iryven::BodyType::Dynamic,
        .gravityScale = 0.0f,
        .fixedRotation = true,
    });
    auto colliderA = Iryven::Collider::Sphere(1.0f);
    colliderA.OnCollisionEnter(
        [&collisionEnteredA](Iryven::Entity, Iryven::Entity other) {
            if (other.Has<CollisionBodyBTag>()) {
                collisionEnteredA = true;
            }
        });
    colliderA.OnCollisionExit(
        [&collisionExitedA](Iryven::Entity, Iryven::Entity other) {
            if (other.Has<CollisionBodyBTag>()) {
                collisionExitedA = true;
            }
        });
    collisionBodyA.Add<Iryven::Collider>(std::move(colliderA));

    auto collisionBodyB = world.CreateEntity("Collision Body B");
    collisionBodyB.Add<CollisionBodyBTag>();
    collisionBodyB.Add<Iryven::Transform>(Iryven::Transform{
        .position = { 21.25f, 0.0f, 0.0f },
    });
    collisionBodyB.Add<Iryven::RigidBody>(Iryven::RigidBody{
        .type = Iryven::BodyType::Kinematic,
    });
    auto colliderB = Iryven::Collider::Sphere(0.5f);
    colliderB.OnCollisionEnter(
        [&collisionEnteredB](Iryven::Entity, Iryven::Entity other) {
            if (other.Has<CollisionBodyATag>()) {
                collisionEnteredB = true;
            }
        });
    colliderB.OnCollisionExit(
        [&collisionExitedB](Iryven::Entity, Iryven::Entity other) {
            if (other.Has<CollisionBodyATag>()) {
                collisionExitedB = true;
            }
        });
    collisionBodyB.Add<Iryven::Collider>(std::move(colliderB));

    world.Progress(1.0f / 60.0f);
    assert(collisionEnteredA);
    assert(collisionEnteredB);

    collisionBodyB.Get<Iryven::Transform>().position =
        { 24.0f, 0.0f, 0.0f };
    for (int step = 0; step < 4 &&
        (!collisionExitedA || !collisionExitedB); ++step) {
        world.Progress(1.0f / 60.0f);
    }
    assert(collisionExitedA);
    assert(collisionExitedB);
}
