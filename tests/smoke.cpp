#include <iryven/iryven.h>

#include <cassert>
#include <memory>
#include <string>
#include <vector>

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

class TrackingLayer final : public Iryven::Layer {
public:
    TrackingLayer(
        std::string name,
        std::vector<std::string>& calls,
        bool consumesEvents = false)
        : Layer(std::move(name)), calls_(calls), consumesEvents_(consumesEvents) {}

    void OnAttach() override { calls_.push_back(GetName() + ":attach"); }
    void OnDetach() override { calls_.push_back(GetName() + ":detach"); }
    void OnUpdate(float) override { calls_.push_back(GetName() + ":update"); }
    bool OnEvent(Iryven::Event&) override
    {
        calls_.push_back(GetName() + ":event");
        return consumesEvents_;
    }

private:
    std::vector<std::string>& calls_;
    bool consumesEvents_;
};

} // namespace

int main()
{
    const auto texture = std::make_shared<const Iryven::Texture>(Iryven::Texture{
        .width = 1,
        .height = 1,
        .colorSpace = Iryven::TextureColorSpace::SRGB,
        .pixels = { 255, 255, 255, 255 },
    });
    assert(texture->IsValid());
    assert(texture->ExpectedByteSize() == 4);

    Iryven::AssetManager textureAssets;
    textureAssets.StoreTexture("virtual/white.png", texture);
    assert(textureAssets.GetTexture("virtual/white.png") == texture);
    assert(textureAssets.GetTexture(
        "virtual/white.png", Iryven::TextureColorSpace::Linear) == nullptr);

    const auto model = std::make_shared<const Iryven::Model>(Iryven::Model{
        .vertices = {
            { .position = { 0.0f, 0.0f, 0.0f } },
            { .position = { 1.0f, 0.0f, 0.0f } },
            { .position = { 0.0f, 1.0f, 0.0f } },
        },
        .indices = { 0, 1, 2 },
        .meshes = { Iryven::Mesh{
            .name = "Triangle",
            .primitives = { Iryven::MeshPrimitive{ .indexCount = 3 } },
        } },
        .nodes = { Iryven::ModelNode{ .name = "Triangle", .meshIndex = 0 } },
        .sceneRoots = { 0 },
    });
    assert(model->IsValid());

    Iryven::World modelWorld;
    auto modelEntity = modelWorld.CreateEntity("Model");
    modelEntity.Add<Iryven::Transform>();
    modelEntity.Add<Iryven::MeshRenderer>(model);
    const Iryven::RenderScene extractedModelScene = modelWorld.ExtractRenderScene();
    assert(extractedModelScene.objects.size() == 1);
    assert(extractedModelScene.objects.front().model == model);
    assert(extractedModelScene.objects.front().indexCount == 3);

    Iryven::AssetManager gltfAssets;
    const Iryven::ModelHandle gltfModel = gltfAssets.LoadModel("tests/assets/basic_triangle.gltf");
    assert(gltfModel && gltfModel->IsValid());
    assert(gltfModel->vertices.size() == 3);
    assert(gltfModel->indices == std::vector<std::uint32_t>({ 0, 1, 2 }));
    assert(gltfModel->meshes.size() == 1);
    assert(gltfModel->meshes.front().primitives.front().indexCount == 3);
    assert(gltfModel->nodes.size() == 1);
    assert(gltfModel->nodes.front().localTransform[3].x == 2.0f);
    assert(gltfModel->materials.size() == 1);
    const auto& gltfMaterial = gltfModel->materials.front();
    assert(gltfMaterial->metallic == 0.25f);
    assert(gltfMaterial->roughness == 0.75f);
    assert(gltfMaterial->baseColorTexture != Iryven::InvalidTextureIndex);
    assert(gltfMaterial->metallicRoughnessTexture != Iryven::InvalidTextureIndex);
    assert(gltfMaterial->baseColorTexture != gltfMaterial->metallicRoughnessTexture);
    assert(gltfModel->textureRegistry.textures.size() == 2);
    assert(gltfModel->textureRegistry.samplers.size() == 2);
    const auto& baseColorTexture = gltfModel->textureRegistry.textures[
        gltfMaterial->baseColorTexture];
    const auto& metallicRoughnessTexture = gltfModel->textureRegistry.textures[
        gltfMaterial->metallicRoughnessTexture];
    assert(baseColorTexture.texture->colorSpace == Iryven::TextureColorSpace::SRGB);
    assert(metallicRoughnessTexture.texture->colorSpace == Iryven::TextureColorSpace::Linear);
    assert(baseColorTexture.samplerIndex == metallicRoughnessTexture.samplerIndex);
    const auto& gltfSampler = gltfModel->textureRegistry.samplers[baseColorTexture.samplerIndex];
    assert(gltfSampler.magFilter == Iryven::TextureFilter::Nearest);
    assert(gltfSampler.minFilter == Iryven::TextureFilter::LinearMipmapLinear);
    assert(gltfSampler.wrapU == Iryven::TextureWrap::ClampToEdge);
    assert(gltfSampler.wrapV == Iryven::TextureWrap::MirroredRepeat);

    const Iryven::ModelHandle objModel = gltfAssets.LoadModel("assets/models/cube.obj");
    assert(objModel && objModel->IsValid());
    assert(objModel->meshes.size() == 1);
    assert(!objModel->vertices.empty());
    assert(!objModel->indices.empty());

    const Iryven::EngineConfig config{ .title = "Test" };
    assert(config.title == "Test");

    std::vector<std::string> layerCalls;
    Iryven::LayerStack layers;
    auto& gameLayer = layers.PushLayer(
        std::make_unique<TrackingLayer>("game", layerCalls));
    layers.PushOverlay(
        std::make_unique<TrackingLayer>("ui", layerCalls, true));
    layers.PushOverlay(
        std::make_unique<TrackingLayer>("debug", layerCalls));
    layers.Update(1.0f / 60.0f);

    Iryven::AppTickEvent event;
    layers.PropagateEvent(event);
    assert(event.IsHandled());
    assert((layerCalls == std::vector<std::string>{
        "game:attach", "ui:attach", "debug:attach",
        "game:update", "ui:update", "debug:update",
        "debug:event", "ui:event" }));

    auto removedGameLayer = layers.PopLayer(gameLayer);
    assert(removedGameLayer);
    assert(layerCalls.back() == "game:detach");

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
