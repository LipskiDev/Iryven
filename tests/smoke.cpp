#include <iryven/iryven.h>

#include <cassert>

#include <glm/common.hpp>

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
}
