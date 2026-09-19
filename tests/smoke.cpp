#include <iryven/iryven.h>
#include <iryven/rendering/framegraph.h>

#include <cassert>
#include <chrono>
#include <fstream>
#include <iterator>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif

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

int RunSmokeTests()
{
#ifdef _MSC_VER
    // Report test failures to the runner instead of opening a blocking CRT dialog.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_error_mode(_OUT_TO_STDERR);
#endif
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
        probe.Add<Iryven::Cloth>(Iryven::Cloth{
            .resolution = {12u, 8u},
            .size = {3.0f, 2.0f},
            .mass = 2.0f,
            .solverIterations = 5,
            .pinTopRight = false,
        });
        const auto probeMesh = Iryven::PrimitiveMeshes::Cube();
		assert(!probeMesh->meshlets.empty());
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
        assert(loadedProbe.Get<Iryven::Cloth>().resolution == glm::uvec2(12u, 8u));
        assert(loadedProbe.Get<Iryven::Cloth>().size == glm::vec2(3.0f, 2.0f));
        assert(loadedProbe.Get<Iryven::Cloth>().mass == 2.0f);
        assert(loadedProbe.Get<Iryven::Cloth>().solverIterations == 5);
        assert(!loadedProbe.Get<Iryven::Cloth>().pinTopRight);
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
			.name = "cloth-simulate",
			.outputs = {{
				.type = Iryven::FrameGraphResourceType::Reference,
				.name = "cloth-complete",
			}},
			.queue = Velos::RHI::QueueType::Compute,
		});
		graph.AddNode({
			.name = "cloth-draw",
			.inputs = {{
				.type = Iryven::FrameGraphResourceType::Reference,
				.name = "cloth-complete",
			}},
		});
		graph.Compile();
		assert(graph.ExecutionOrder().size() == 2);
		assert(graph.AccessNode(graph.ExecutionOrder()[0])->name ==
			"cloth-simulate");
		assert(graph.AccessNode(graph.ExecutionOrder()[1])->name == "cloth-draw");

		graph.Reset();
		graph.AddNode({
			.name = "attachment-producer",
			.outputs = {{
				.type = Iryven::FrameGraphResourceType::Attachment,
				.info = textureInfo,
				.external = true,
				.name = "loaded-attachment",
			}},
		});
		graph.AddNode({
			.name = "attachment-consumer",
			.inputs = {{
				.type = Iryven::FrameGraphResourceType::Attachment,
				.access = Iryven::FrameGraphAccess::ColorAttachmentReadWrite,
				.info = Iryven::FrameGraphTextureInfo{
					.loadOp = Iryven::RenderPassOperation::Load,
				},
				.name = "loaded-attachment",
			}},
		});
		graph.Compile();
		const auto* attachmentConsumer = graph.GetNode("attachment-consumer");
		const auto* attachmentInput = graph.AccessResource(
			attachmentConsumer->inputs.front());
		assert(std::get<Iryven::FrameGraphTextureInfo>(attachmentInput->info)
			.loadOp == Iryven::RenderPassOperation::Load);

        graph.Reset();
        graph.AddNode({
            .name = "draw",
            .inputs = {{
                .type = Iryven::FrameGraphResourceType::Buffer,
                .access = Iryven::FrameGraphAccess::ShaderStorageReadWrite,
                .name = "simulation",
            }},
        });
        graph.AddNode({
            .name = "simulate",
            .outputs = {{
                .type = Iryven::FrameGraphResourceType::Buffer,
                .access = Iryven::FrameGraphAccess::ShaderStorageWrite,
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
                .access = Iryven::FrameGraphAccess::ShaderStorageRead,
                .name = "simulation",
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
            .inputs = {
                {
                    .type = Iryven::FrameGraphResourceType::Buffer,
                    .access = Iryven::FrameGraphAccess::ShaderStorageReadWrite,
                    .name = "simulation",
                },
                {
                    .type = Iryven::FrameGraphResourceType::Texture,
                    .access = Iryven::FrameGraphAccess::ShaderSampledRead,
                    .name = "simulation-image",
                },
            },
        });
        graph.AddNode({
            .name = "simulate",
            .outputs = {
                {
                    .type = Iryven::FrameGraphResourceType::Buffer,
                    .access = Iryven::FrameGraphAccess::ShaderStorageWrite,
                    .info = Iryven::FrameGraphBufferInfo{
                        .size = 256,
                        .usage = Velos::RHI::BufferUsage::Storage,
                    },
                    .name = "simulation",
                },
                {
                    .type = Iryven::FrameGraphResourceType::Texture,
                    .access = Iryven::FrameGraphAccess::ShaderStorageWrite,
                    .info = Iryven::FrameGraphTextureInfo{
                        .width = 16,
                        .height = 16,
                        .format = Velos::RHI::Format::RGBA8_UNORM,
                        .usage = Velos::RHI::ImageUsage::Storage |
                                 Velos::RHI::ImageUsage::Sampled,
                    },
                    .name = "simulation-image",
                },
            },
            .queue = Velos::RHI::QueueType::Compute,
        });
        graph.AddNode({
            .name = "post",
            .inputs = {{
                .type = Iryven::FrameGraphResourceType::Buffer,
                .access = Iryven::FrameGraphAccess::ShaderStorageRead,
                .name = "simulation",
            }},
            .queue = Velos::RHI::QueueType::Compute,
        });
        graph.Compile();
        const auto queueRelationship = device->GetQueueRelationship(
            Velos::RHI::QueueType::Graphics,
            Velos::RHI::QueueType::Compute);
        const auto* simulation = graph.GetResource("simulation");
        assert(simulation != nullptr);
        assert(std::get<Iryven::FrameGraphBufferInfo>(simulation->info)
                   .concurrentQueues ==
               (queueRelationship ==
                Velos::RHI::QueueRelationship::DifferentFamily));

        const auto batches = graph.ExecutionBatches();
        assert(batches.size() ==
               (queueRelationship == Velos::RHI::QueueRelationship::SameQueue
                    ? 1u
                    : 3u));

        graph.BeginFrame();
        graph.Render({});
        assert(graph.GraphicsSubmissionWaits().size() ==
               (queueRelationship == Velos::RHI::QueueRelationship::SameQueue
                    ? 0u
                    : 1u));
        assert(computeRenderCount == 1);
        assert(graphicsRenderCount == 1);
        assert(postRenderCount == 1);
        const auto* simulationImage = graph.GetResource("simulation-image");
        assert(simulationImage != nullptr);
        assert(device->GetImageLayout(
            std::get<Iryven::FrameGraphTextureInfo>(simulationImage->info).handle,
            0) == Velos::RHI::ImageLayout::ShaderReadOnly);

        graph.BeginFrame();
        graph.Render({});
        assert(graph.GraphicsSubmissionWaits().size() ==
               (queueRelationship == Velos::RHI::QueueRelationship::SameQueue
                    ? 0u
                    : 1u));
        device->WaitIdle();
        assert(computeRenderCount == 2);
        assert(graphicsRenderCount == 2);
        assert(postRenderCount == 2);

		{
			const auto concurrentBuffer = device->CreateBuffer({
				.size = sizeof(std::uint32_t),
				.usage = Velos::RHI::BufferUsage::Storage |
					Velos::RHI::BufferUsage::TransferDst,
				.memoryUsage = Velos::RHI::MemoryUsage::GPUOnly,
				.concurrentQueues = true,
				.debugName = "Concurrent upload test buffer",
			});
			const std::uint32_t uploadValue = 42;
			auto upload = device->CreateUploadContext(sizeof(uploadValue));
			upload->Begin();
			upload->UploadBuffer({
				.dstBuffer = concurrentBuffer,
				.size = sizeof(uploadValue),
				.data = &uploadValue,
				.finalState = Velos::RHI::ResourceState::ShaderRead,
			});
			upload->Flush();
			assert(upload->TakePendingBufferAcquires().empty());
			device->DestroyBuffer(concurrentBuffer);
		}

		graph.Reset();
		int attachmentProducerCount = 0;
		int attachmentConsumerCount = 0;
		CountingFrameGraphPass attachmentProducer(attachmentProducerCount);
		CountingFrameGraphPass attachmentConsumer(attachmentConsumerCount);
		builder.RegisterRenderPass("attachment-producer", attachmentProducer);
		builder.RegisterRenderPass("attachment-consumer", attachmentConsumer);
		graph.AddNode({
			.name = "attachment-producer",
			.outputs = {{
				.type = Iryven::FrameGraphResourceType::Attachment,
				.access = Iryven::FrameGraphAccess::ColorAttachmentWrite,
				.info = Iryven::FrameGraphTextureInfo{
					.width = 16,
					.height = 16,
					.format = Velos::RHI::Format::RGBA8_UNORM,
					.usage = Velos::RHI::ImageUsage::ColorAttachment,
					.loadOp = Iryven::RenderPassOperation::Clear,
				},
				.external = true,
				.name = "live-attachment",
			}},
		});
		graph.AddNode({
			.name = "attachment-consumer",
			.inputs = {{
				.type = Iryven::FrameGraphResourceType::Attachment,
				.access = Iryven::FrameGraphAccess::ColorAttachmentReadWrite,
				.info = Iryven::FrameGraphTextureInfo{
					.loadOp = Iryven::RenderPassOperation::Load,
				},
				.name = "live-attachment",
			}},
		});
		graph.Compile();

		const auto attachmentImage = device->CreateImage({
			.width = 16,
			.height = 16,
			.format = Velos::RHI::Format::RGBA8_UNORM,
			.usage = Velos::RHI::ImageUsage::ColorAttachment,
			.debugName = "Frame graph live attachment test image",
		});
		const auto attachmentView = device->CreateImageView({
			.image = attachmentImage,
			.format = Velos::RHI::Format::RGBA8_UNORM,
			.aspect = Velos::RHI::ImageAspect::Color,
			.debugName = "Frame graph live attachment test view",
		});
		auto& liveAttachment = std::get<Iryven::FrameGraphTextureInfo>(
			graph.GetResource("live-attachment")->info);
		liveAttachment.handle = attachmentImage;
		liveAttachment.view = attachmentView;

		graph.BeginFrame();
		graph.Render({});
		device->WaitIdle();
		assert(attachmentProducerCount == 1);
		assert(attachmentConsumerCount == 1);

		// Hi-Z scheduling: graphics depth writes -> compute sampled reads,
		// then the following frame writes the same depth image again.
		graph.Reset();
		graph.AddNode({
			.name = "attachment-producer",
			.outputs = {{
				.type = Iryven::FrameGraphResourceType::Attachment,
				.access = Iryven::FrameGraphAccess::DepthStencilWrite,
				.info = Iryven::FrameGraphTextureInfo{
					.width = 17, .height = 9,
					.format = Velos::RHI::Format::D32_FLOAT,
					.usage = Velos::RHI::ImageUsage::DepthStencil |
						Velos::RHI::ImageUsage::Sampled,
					.loadOp = Iryven::RenderPassOperation::Clear,
					.concurrentQueues = true,
				},
				.external = true, .name = "hi-z-source-depth",
			}},
		});
		graph.AddNode({
			.name = "attachment-consumer",
			.inputs = {{
				.type = Iryven::FrameGraphResourceType::Texture,
				.access = Iryven::FrameGraphAccess::ShaderSampledRead,
				.name = "hi-z-source-depth",
			}},
			.queue = Velos::RHI::QueueType::Compute,
		});
		graph.Compile();
		const auto hiZDepth = device->CreateImage({
			.width = 17, .height = 9, .format = Velos::RHI::Format::D32_FLOAT,
			.usage = Velos::RHI::ImageUsage::DepthStencil | Velos::RHI::ImageUsage::Sampled,
			.concurrentQueues = true,
		});
		const auto hiZDepthView = device->CreateImageView({
			.image = hiZDepth, .format = Velos::RHI::Format::D32_FLOAT,
			.aspect = Velos::RHI::ImageAspect::Depth,
		});
		auto& hiZDepthInfo = std::get<Iryven::FrameGraphTextureInfo>(
			graph.GetResource("hi-z-source-depth")->info);
		hiZDepthInfo.handle = hiZDepth;
		hiZDepthInfo.view = hiZDepthView;
		for (int frame = 0; frame < 3; ++frame) {
			graph.BeginFrame();
			graph.Render({});
			assert(device->GetImageLayout(hiZDepth, 0) == Velos::RHI::ImageLayout::ShaderReadOnly);
			assert(graph.GraphicsSubmissionWaits().size() ==
				(queueRelationship == Velos::RHI::QueueRelationship::SameQueue ? 0u : 1u));
		}
		device->WaitIdle();
		assert(attachmentProducerCount == 4);
		assert(attachmentConsumerCount == 4);

        graph.Shutdown();
        builder.Shutdown();
		device->DestroyImageView(hiZDepthView);
		device->DestroyImage(hiZDepth);
		device->DestroyImageView(attachmentView);
		device->DestroyImage(attachmentImage);
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
	const auto clothVertexShader = Velos::ShaderCompiler::CompileFile({
		.path = "assets/shaders/internal/cloth.vert",
		.stage = Velos::RHI::ShaderStage::Vertex,
		.entryPoint = "main",
		.language = Velos::ShaderSourceLanguage::GLSL,
	});
	assert(!bindlessVertexShader.spirv.empty());
	assert(!bindlessFragmentShader.spirv.empty());
	assert(!clothVertexShader.spirv.empty());

	const auto hiZShader = Velos::ShaderCompiler::CompileFile({
		.path = "assets/shaders/internal/hi_z_reduce.comp",
		.stage = Velos::RHI::ShaderStage::Compute,
		.entryPoint = "main",
		.language = Velos::ShaderSourceLanguage::GLSL,
	});
	assert(!hiZShader.spirv.empty());
	assert(hiZShader.reflection.pushConstants.size() == 1);
	assert(hiZShader.reflection.pushConstants[0].offset == 0);
	assert(hiZShader.reflection.pushConstants[0].size == 32);
	assert(std::any_of(hiZShader.reflection.resources.begin(),
		hiZShader.reflection.resources.end(), [](const auto& binding) {
			return binding.set == 0 && binding.binding == 1 &&
				binding.type == Velos::ShaderResourceType::StorageImage;
		}));

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

	auto clothEntity = modelWorld.CreateEntity("Cloth");
	clothEntity.Add<Iryven::Transform>();
	clothEntity.Add<Iryven::Cloth>(Iryven::Cloth{
		.resolution = {16u, 10u},
		.size = {4.0f, 2.0f},
	});
	const Iryven::RenderScene extractedClothScene =
		modelWorld.ExtractRenderScene();
	assert(extractedClothScene.cloths.size() == 1);
	assert(extractedClothScene.cloths.front().id == clothEntity.GetId());
	assert(extractedClothScene.cloths.front().resolution == glm::uvec2(16u, 10u));

    Iryven::AssetManager gltfAssets;
    const Iryven::ModelHandle gltfModel = gltfAssets.LoadModel("tests/assets/basic_triangle.gltf");
    assert(gltfModel && gltfModel->IsValid());
    assert(gltfModel->vertices.size() == 3);
    assert(gltfModel->indices == std::vector<std::uint32_t>({ 0, 1, 2 }));
    assert(gltfModel->meshes.size() == 1);
    assert(gltfModel->meshes.front().primitives.front().indexCount == 3);
	assert(gltfModel->meshes.front().primitives.front().meshlets.size() == 1);
	assert(gltfModel->meshes.front().primitives.front().meshlets.front().triangleIndices.size() == 3);
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

    const auto extensionModel = gltfAssets.LoadModel("tests/assets/material_extensions.gltf");
    assert(extensionModel && extensionModel->IsValid());
    assert(extensionModel->materials.size() == 2);
    const auto& sgMaterial = extensionModel->materials[0];
    assert(sgMaterial->specularGlossiness);
    assert(sgMaterial->baseColor.R() == 0.6f); // Extension overrides metallic-roughness fallback.
    assert(sgMaterial->baseColor.A() == 0.8f);
    assert(sgMaterial->specular.G() == 0.4f);
    assert(sgMaterial->glossiness == 0.7f);
    assert(sgMaterial->metallic == 0.0f);
    assert(sgMaterial->specularGlossinessTexture != Iryven::InvalidTextureIndex);
    assert(extensionModel->textureRegistry.textures[sgMaterial->specularGlossinessTexture]
        .texture->colorSpace == Iryven::TextureColorSpace::SRGB);
    const auto& glassMaterial = extensionModel->materials[1];
    assert(!glassMaterial->specularGlossiness);
    assert(glassMaterial->transmission == 0.75f);
    assert(glassMaterial->transmissionTexture != Iryven::InvalidTextureIndex);
    assert(extensionModel->textureRegistry.textures[glassMaterial->transmissionTexture]
        .texture->colorSpace == Iryven::TextureColorSpace::Linear);
    assert(!extensionModel->meshes[0].primitives[1].meshlets.empty());

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
    return 0;
}

int main()
{
    try {
        return RunSmokeTests();
    } catch (const std::exception& error) {
        std::cerr << "Smoke test failed: " << error.what() << '\n';
        return 1;
    }
}
