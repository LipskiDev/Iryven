#include <iryven/iryven.h>
#include <iryven/rendering/framegraph.h>

#include <cassert>
#include <chrono>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <glm/common.hpp>
#include <shader/shader_compiler.h>

extern "C" int glfwInit();
extern "C" void glfwTerminate();

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

struct SnapshotVariable {
    int value = 0;
    std::string label;
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
    void OnImGuiRender() override { calls_.push_back(GetName() + ":imgui"); }
    bool OnEvent(Iryven::Event&) override
    {
        calls_.push_back(GetName() + ":event");
        return consumesEvents_;
    }

private:
    std::vector<std::string>& calls_;
    bool consumesEvents_;
};

class CountingFrameGraphPass final : public Iryven::FrameGraphRenderPass {
public:
    explicit CountingFrameGraphPass(int& renderCount)
        : renderCount_(renderCount) {}

    void AddUI() override {}
    void PreRender(Velos::RHI::ICommandList&,
                   const Iryven::RenderScene&) override {}
    void Render(Velos::RHI::ICommandList&,
                const Iryven::RenderScene&) override
    {
        ++renderCount_;
    }
    void OnResize(Velos::RHI::IDevice&, std::uint32_t, std::uint32_t) override {}

private:
    int& renderCount_;
};

} // namespace

int main()
{
    {
        Iryven::GameLayer pausedGame;
        auto body = pausedGame.GetWorld().CreateEntity("Paused Body");
        body.Add<Iryven::Transform>(Iryven::Transform{ .position = {0.0f, 4.0f, 0.0f} });
        body.Add<Iryven::RigidBody>(Iryven::RigidBody{ .type = Iryven::BodyType::Dynamic });
        body.Add<Iryven::Collider>(Iryven::Collider::Sphere(0.5f));
        pausedGame.SetSimulationEnabled(false);
        for (int step = 0; step < 30; ++step) pausedGame.OnUpdate(1.0f / 60.0f);
        assert(body.Get<Iryven::Transform>().position.y == 4.0f);
        pausedGame.SetSimulationEnabled(true);
        for (int step = 0; step < 30; ++step) pausedGame.OnUpdate(1.0f / 60.0f);
        assert(body.Get<Iryven::Transform>().position.y < 4.0f);
        pausedGame.SetSimulationEnabled(false);
        body.Get<Iryven::Transform>().position.y = 4.0f;
        pausedGame.GetWorld().ResetPhysics();
        pausedGame.SetSimulationEnabled(true);
        pausedGame.OnUpdate(1.0f / 60.0f);
        assert(body.Get<Iryven::Transform>().position.y > 3.9f);
    }
    {
        Iryven::World world;
        auto entity = world.CreateEntity("Snapshot Probe");
        entity.Add<SnapshotVariable>(SnapshotVariable{ .value = 42, .label = "before play" });
        auto flecsEntity = world.GetFlecsWorld().entity(entity.GetId());
        auto backup = flecsEntity.clone(true);
        backup.add(flecs::Prefab);
        std::size_t visibleEntityCount = 0;
        world.ForEachEntity([&visibleEntityCount](Iryven::Entity) { ++visibleEntityCount; });
        assert(visibleEntityCount == 1);

        entity.Get<SnapshotVariable>() = SnapshotVariable{ .value = 7, .label = "during play" };
        entity.Add<Iryven::Light>();
        flecsEntity.clear();
        backup.clone(true, flecsEntity.id());
        flecsEntity.remove(flecs::Prefab);
        flecsEntity.set_name("Snapshot Probe");

        assert(entity.Has<SnapshotVariable>());
        assert(entity.Get<SnapshotVariable>().value == 42);
        assert(entity.Get<SnapshotVariable>().label == "before play");
        assert(!entity.Has<Iryven::Light>());
        backup.destruct();
    }
    {
        Iryven::World scene;
        auto empty = scene.CreateEntity("EmptyEntity");
        std::vector<std::uint64_t> sceneEntityIds;
        scene.ForEachEntity([&sceneEntityIds](Iryven::Entity entity) {
            sceneEntityIds.push_back(entity.GetId());
        });
        assert(sceneEntityIds.size() == 1);
        assert(sceneEntityIds.front() == empty.GetId());
        assert(!empty.Has<Iryven::Transform>());
        auto probe = scene.CreateEntity("SerializationProbe");
        probe.Add<Iryven::Transform>(Iryven::Transform{.position = {3, 4, 5}});
        probe.Add<Iryven::Camera>(Iryven::Camera{.verticalFov = 75});
        probe.Add<Iryven::Light>(Iryven::Light{.intensity = 7});
        const auto probeMesh = Iryven::PrimitiveMeshes::Cube();
        probe.Add<Iryven::MeshRenderer>(probeMesh);
        const auto path = std::filesystem::temp_directory_path() /
            ("iryven-scene-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
        scene.SerializeScene(path);
        std::ifstream file(path, std::ios::binary);
        const std::string json{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        file.close();
        Iryven::World loadedScene;
        auto loadedProbe = loadedScene.CreateEntity("SerializationProbe");
        loadedProbe.Add<Iryven::Transform>();
        loadedProbe.Add<Iryven::MeshRenderer>(probeMesh);
        loadedScene.CreateEntity("PreservedEntity");
        loadedScene.DeserializeScene(path);
        std::size_t loadedEntityCount = 0;
        loadedScene.ForEachEntity([&loadedEntityCount](Iryven::Entity) {
            ++loadedEntityCount;
        });
        assert(loadedEntityCount == 3);
        assert(loadedProbe.Get<Iryven::Transform>().position == glm::vec3(3, 4, 5));
        assert(loadedProbe.Get<Iryven::Camera>().verticalFov == 75);
        assert(loadedProbe.Get<Iryven::Light>().intensity == 7);
        assert(loadedProbe.Get<Iryven::MeshRenderer>().mesh == probeMesh);
        loadedScene.SerializeScene(path);
        std::ifstream loadedFile(path, std::ios::binary);
        const std::string loadedJson{std::istreambuf_iterator<char>(loadedFile), std::istreambuf_iterator<char>()};
        loadedFile.close();
        assert(loadedJson.find("SerializationProbe") != std::string::npos);
        assert(loadedJson.find("PreservedEntity") != std::string::npos);
        for (const std::string invalid : {std::string{}, std::string{"not json"}, json + " trailing garbage", json + std::string(1, '\0')}) {
            { std::ofstream invalidFile(path, std::ios::binary); invalidFile << invalid; }
            bool rejected = false;
            try { loadedScene.DeserializeScene(path); }
            catch (const std::runtime_error&) { rejected = true; }
            assert(rejected);
        }
        std::filesystem::remove(path);
        bool rejectedMissing = false;
        try { loadedScene.DeserializeScene(path); }
        catch (const std::runtime_error&) { rejectedMissing = true; }
        assert(rejectedMissing);
        assert(json.find("SerializationProbe") != std::string::npos);
        bool rejectedDirectory = false;
        try {
            scene.SerializeScene(std::filesystem::temp_directory_path());
        } catch (const std::runtime_error&) {
            rejectedDirectory = true;
        }
        assert(rejectedDirectory);
    }
	{
        Iryven::FrameGraphBuilder builder;
        Iryven::FrameGraph graph;
        graph.Init(builder);

        const Iryven::FrameGraphTextureInfo textureInfo{
            .width = 1280,
            .height = 720,
            .format = Velos::RHI::Format::RGBA8_UNORM,
            .usage = Velos::RHI::ImageUsage::ColorAttachment |
                     Velos::RHI::ImageUsage::Sampled,
        };

        graph.AddNode({
            .name = "lighting",
            .inputs = {{
                .type = Iryven::FrameGraphResourceType::Texture,
                .name = "gbuffer",
            }},
        });
        graph.AddNode({
            .name = "geometry",
            .outputs = {{
                .type = Iryven::FrameGraphResourceType::Attachment,
                .info = textureInfo,
                .external = true,
                .name = "gbuffer",
            }},
        });

        graph.Compile();
        const auto order = graph.ExecutionOrder();
        assert(order.size() == 2);
        assert(graph.AccessNode(order[0])->name == "geometry");
        assert(graph.AccessNode(order[1])->name == "lighting");

        const auto* output = graph.GetResource("gbuffer");
        const auto* lighting = graph.GetNode("lighting");
        const auto* input = graph.AccessResource(lighting->inputs.front());
        assert(output != nullptr && output->referenceCount == 1);
        assert(input != nullptr && input->producer == output->producer);
        assert(input->outputHandle == output->outputHandle);

        graph.Reset();
        graph.AddNode({
            .name = "draw",
            .inputs = {{
                .type = Iryven::FrameGraphResourceType::Buffer,
                .name = "simulation",
            }},
            .outputs = {{
                .type = Iryven::FrameGraphResourceType::Buffer,
                .info = Iryven::FrameGraphBufferInfo{
                    .size = 256,
                    .usage = Velos::RHI::BufferUsage::Storage,
                    .concurrentQueues = true,
                },
                .external = true,
                .name = "lighting",
            }},
        });
        graph.AddNode({
            .name = "simulate",
            .outputs = {{
                .type = Iryven::FrameGraphResourceType::Buffer,
                .info = Iryven::FrameGraphBufferInfo{
                    .size = 256,
                    .usage = Velos::RHI::BufferUsage::Storage,
                    .concurrentQueues = true,
                },
                .external = true,
                .name = "simulation",
            }},
            .queue = Velos::RHI::QueueType::Compute,
        });
        graph.AddNode({
            .name = "post",
            .inputs = {{
                .type = Iryven::FrameGraphResourceType::Buffer,
                .name = "lighting",
            }},
            .queue = Velos::RHI::QueueType::Compute,
        });
        graph.Compile();
        const auto batches = graph.ExecutionBatches();
        assert(batches.size() == 3);
        assert(batches[0].queue == Velos::RHI::QueueType::Compute);
        assert(batches[0].nodes.size() == 1);
        assert(batches[1].queue == Velos::RHI::QueueType::Graphics);
        assert(batches[1].dependencies.size() == 1);
        assert(batches[1].dependencies[0] == 0);
        assert(batches[2].queue == Velos::RHI::QueueType::Compute);
        assert(batches[2].dependencies.size() == 1);
        assert(batches[2].dependencies[0] == 1);

        graph.Reset();
        graph.AddNode({
            .name = "a",
            .inputs = {{ .name = "b-out" }},
            .outputs = {{
                .info = textureInfo,
                .external = true,
                .name = "a-out",
            }},
        });
        graph.AddNode({
            .name = "b",
            .inputs = {{ .name = "a-out" }},
            .outputs = {{
                .info = textureInfo,
                .external = true,
                .name = "b-out",
            }},
        });

        bool cycleDetected = false;
        try {
            graph.Compile();
        } catch (const std::logic_error&) {
            cycleDetected = true;
        }
        assert(cycleDetected);
        graph.Shutdown();
    }

    {
        assert(glfwInit() != 0);
        std::unique_ptr<Velos::RHI::IDevice, void(*)(Velos::RHI::IDevice*)>
            device(Velos::RHI::CreateDevice({
                .graphicsAPI = Velos::RHI::GraphicsAPI::Vulkan,
                .enableValidation = true,
                .applicationName = "Iryven framegraph async test",
                .pipelineCachePath = nullptr,
            }), Velos::RHI::DestroyDevice);
        assert(device != nullptr);

        int computeRenderCount = 0;
        int graphicsRenderCount = 0;
        int postRenderCount = 0;
        CountingFrameGraphPass computePass(computeRenderCount);
        CountingFrameGraphPass graphicsPass(graphicsRenderCount);
        CountingFrameGraphPass postPass(postRenderCount);
        Iryven::FrameGraphBuilder builder;
        builder.Init(*device);
        builder.RegisterRenderPass("simulate", computePass);
        builder.RegisterRenderPass("draw", graphicsPass);
        builder.RegisterRenderPass("post", postPass);
        Iryven::FrameGraph graph;
        graph.Init(builder);
        graph.AddNode({
            .name = "draw",
            .inputs = {{
                .type = Iryven::FrameGraphResourceType::Buffer,
                .name = "simulation",
            }},
            .outputs = {{
                .type = Iryven::FrameGraphResourceType::Buffer,
                .info = Iryven::FrameGraphBufferInfo{
                    .size = 256,
                    .usage = Velos::RHI::BufferUsage::Storage,
                },
                .name = "lighting",
            }},
        });
        graph.AddNode({
            .name = "simulate",
            .outputs = {{
                .type = Iryven::FrameGraphResourceType::Buffer,
                .info = Iryven::FrameGraphBufferInfo{
                    .size = 256,
                    .usage = Velos::RHI::BufferUsage::Storage,
                },
                .name = "simulation",
            }},
            .queue = Velos::RHI::QueueType::Compute,
        });
        graph.AddNode({
            .name = "post",
            .inputs = {{
                .type = Iryven::FrameGraphResourceType::Buffer,
                .name = "lighting",
            }},
            .queue = Velos::RHI::QueueType::Compute,
        });
        graph.Compile();
        const auto* simulation = graph.GetResource("simulation");
        assert(simulation != nullptr);
        assert(std::get<Iryven::FrameGraphBufferInfo>(simulation->info)
                   .concurrentQueues);

        graph.BeginFrame();
        graph.Render({});
        assert(graph.GraphicsSubmissionWaits().size() == 1);
        device->WaitIdle();
        assert(computeRenderCount == 1);
        assert(graphicsRenderCount == 1);
        assert(postRenderCount == 1);

        graph.Shutdown();
        builder.Shutdown();
        device.reset();
        glfwTerminate();
    }

	const auto bindlessVertexShader = Velos::ShaderCompiler::CompileFile({
		.path = "assets/shaders/internal/gltf.vert",
		.stage = Velos::RHI::ShaderStage::Vertex,
		.entryPoint = "main",
		.language = Velos::ShaderSourceLanguage::GLSL,
	});
	const auto bindlessFragmentShader = Velos::ShaderCompiler::CompileFile({
		.path = "assets/shaders/internal/gltf_bindless.frag",
		.stage = Velos::RHI::ShaderStage::Fragment,
		.entryPoint = "main",
		.language = Velos::ShaderSourceLanguage::GLSL,
	});
	assert(!bindlessVertexShader.spirv.empty());
	assert(!bindlessFragmentShader.spirv.empty());

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
    layers.RenderImGui();

    Iryven::AppTickEvent event;
    layers.PropagateEvent(event);
    assert(event.IsHandled());
    assert((layerCalls == std::vector<std::string>{
        "game:attach", "ui:attach", "debug:attach",
        "game:update", "ui:update", "debug:update",
        "game:imgui", "ui:imgui", "debug:imgui",
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
