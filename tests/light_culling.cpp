#include <rhi/device.h>
#include <shader/shader_compiler.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "../engine/src/renderer/light_depth_bins.h"

static void TestDepthBins()
{
    using namespace Iryven;
    RenderCamera camera;
    camera.nearPlane = 1;
    camera.farPlane = 257; // One depth unit per bin.
    camera.view = glm::translate(glm::mat4(1), glm::vec3(0, 0, 2));
    const std::array lights{
        RenderLight{.type = LightType::Point, .position = {0, 0, 10}, .range = 1}, // Behind camera.
        RenderLight{.type = LightType::Spot, .position = {0, 0, -4}, .range = 2}, // Near-plane overlap.
        RenderLight{.type = LightType::Point, .position = {0, 0, -12}, .range = 1},
        RenderLight{.type = LightType::Spot, .position = {0, 0, -15}, .range = 5},
        RenderLight{.type = LightType::Point, .position = {0, 0, -259}, .range = 1},
        RenderLight{.type = LightType::Point, .position = {0, 0, -300}, .range = 1},
    };
    const auto bins = BuildLightDepthBins(camera, lights);
    if (bins.ranges[0] != 0x00010001u || bins.ranges[8] != 0x00030002u ||
        bins.ranges[100] != LightDepthBins::kEmpty || bins.ranges[255] != 0x00040004u)
        throw std::runtime_error("Depth-bin bounds/empty range mismatch");
    // Independently sample each bin: any light intersecting that sample must lie
    // in the encoded interval, including variable radii and exact boundaries.
    for (std::uint32_t bin = 0; bin < LightDepthBins::kCount; ++bin) {
        const float depth = camera.nearPlane + bin + 0.5f;
        for (std::uint32_t index = 0; index < lights.size(); ++index) {
            const float center = -lights[index].position.z - 2;
            if (std::abs(depth - center) <= lights[index].range &&
                ((bins.ranges[bin] & 0xffffu) > index || (bins.ranges[bin] >> 16u) < index))
                throw std::runtime_error("Depth bins excluded an overlapping light");
        }
    }
    const auto empty = BuildLightDepthBins(camera, {});
    for (auto range : empty.ranges) if (range != LightDepthBins::kEmpty)
        throw std::runtime_error("Empty depth-bin frame retained light indices");
    std::array<RenderLight, 512> full;
    for (auto& light : full) { light.position.z = -12; light.range = 1000; }
    const auto maximum = BuildLightDepthBins(camera, full);
    for (auto range : maximum.ranges) if (range != (511u << 16u))
        throw std::runtime_error("Depth-bin maximum light index mismatch");
    std::cout << "Depth bins: camera transform, near/far overlap, point/spot bounds, empty bins, 512 lights passed\n";
}

extern "C" int glfwInit();
extern "C" void glfwTerminate();

void RunLightTilingTests(bool benchmark, const char* shaderPath)
{
    TestDepthBins();
    using namespace Velos::RHI;
    if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
    std::unique_ptr<IDevice, decltype(&DestroyDevice)> device(CreateDevice({
        .graphicsAPI = GraphicsAPI::Vulkan, .enableValidation = true,
        .applicationName = "Light tiling readback", .pipelineCachePath = nullptr}), DestroyDevice);
    const auto code = Velos::ShaderCompiler::CompileFile({
        .path = shaderPath ? shaderPath : "assets/shaders/internal/light_culling.comp.spv", .stage = ShaderStage::Compute,
        .language = Velos::ShaderSourceLanguage::SpirvBinary});
    auto shader = device->CreateShader({.stage = ShaderStage::Compute, .bytecode = code.spirv.data(),
        .bytecodeSize = code.spirv.size() * 4, .reflection = code.reflection});
    const std::array reflections{code.reflection};
    auto layout = device->BuildPipelineLayout(Velos::ShaderCompiler::MergeShaderReflection(reflections));
    auto pipeline = device->CreateComputePipeline({.computeShader = shader,
        .layout = {.descriptorSetLayouts = layout.setLayouts.data(), .descriptorSetLayoutCount = 1}});
    const BindingPoolSize sizes[]{{BindingType::UniformBuffer, 1}, {BindingType::StorageBuffer, 2}};
    auto pool = device->CreateBindingPool({.poolSizes = sizes, .poolSizeCount = 2, .maxSets = 1});
    auto set = device->AllocateBindingSet({.pool = pool, .layout = layout.setLayouts[0]});
    struct Light { glm::vec4 position, direction, color, spot; };
    struct Lights { glm::uvec4 header{}; std::array<Light, 512> lights{}; } lights;
    struct alignas(16) Data { glm::mat4 view{1}, projection{1}; glm::uvec4 viewport{}; } data;
    static_assert(sizeof(Data) == 144);
    auto lightBuffer = device->CreateBuffer({.size = sizeof(lights), .usage = BufferUsage::Storage,
        .memoryUsage = MemoryUsage::CPUToGPU});
    auto dataBuffer = device->CreateBuffer({.size = sizeof(data), .usage = BufferUsage::Uniform,
        .memoryUsage = MemoryUsage::CPUToGPU});
    constexpr std::size_t outputSize = (4 + 6 * 16) * sizeof(std::uint32_t);
    auto output = device->CreateBuffer({.size = outputSize,
        .usage = BufferUsage::Storage | BufferUsage::TransferSrc});
    auto readback = device->CreateBuffer({.size = outputSize, .usage = BufferUsage::TransferDst,
        .memoryUsage = MemoryUsage::GPUToCPU});
    const BindingBufferInfo lightInfo{.buffer = lightBuffer, .range = sizeof(lights)};
    const BindingBufferInfo dataInfo{.buffer = dataBuffer, .range = sizeof(data)};
    const BindingBufferInfo outputInfo{.buffer = output, .range = outputSize};
    device->UpdateBindingSet({.dstSet = set, .binding = 0,
        .type = BindingType::StorageBuffer, .bufferInfo = &lightInfo});
    device->UpdateBindingSet({.dstSet = set, .binding = 1,
        .type = BindingType::UniformBuffer, .bufferInfo = &dataInfo});
    device->UpdateBindingSet({.dstSet = set, .binding = 3,
        .type = BindingType::StorageBuffer, .bufferInfo = &outputInfo});

    ResourceState outputState = ResourceState::Undefined;
    for (int test = 0; test < 4; ++test) {
        const bool empty = test == 1;
        const bool directionalOnly = test == 2;
        const std::uint32_t width = test == 3 ? 1 : 17, height = test == 3 ? 1 : 9;
        const std::uint32_t columns = (width + 7) / 8, rows = (height + 7) / 8;
        data.viewport = {width, height, columns, rows};
        data.view = glm::translate(glm::mat4(1), glm::vec3(-2, 1, 0));
        data.projection = glm::perspectiveRH_ZO(glm::radians(90.0f), float(width) / height, 0.1f, 100.0f);
        data.projection[1][1] *= -1;
        lights = {};
        lights.header = empty ? glm::uvec4(0) : directionalOnly
            ? glm::uvec4(1, 1, 1, 0) : glm::uvec4(512, 1, 1, 511);
        lights.lights[0].position.w = 0; // Directional prefix must not occupy mask bit 0.
        for (std::uint32_t i = 1; i < 512; ++i) {
            lights.lights[i].position = {1002, -1, -3, 1}; // Off screen.
            lights.lights[i].direction.w = 0.001f;
        }
        const auto setLocal = [&](std::uint32_t local, glm::vec3 viewCenter, float radius, float type = 1) {
            lights.lights[local + 1].position = glm::vec4(viewCenter + glm::vec3(2, -1, 0), type);
            lights.lights[local + 1].direction.w = radius;
        };
        setLocal(0, {0, 0, -3}, 100);  // All tiles.
        setLocal(2, {0, 0, 3}, 0.01f); // Behind camera: no tiles.
        setLocal(3, {0, 0, 0}, 1);     // Camera inside light: all tiles.
        setLocal(31, {0, 0, -3}, 100);
        setLocal(32, {0, 0, -3}, 100);
        setLocal(510, {0, 0, -3}, 100);
        const auto atPixel = [&](float x, float y) {
            return glm::vec3((2 * x / width - 1) * 3 / data.projection[0][0],
                (2 * y / height - 1) * 3 / data.projection[1][1], -3);
        };
        setLocal(4, atPixel(0.5f, 0.5f), 0.001f, 2); // Spot sphere, first tile.
        setLocal(5, atPixel(width - 0.5f, height - 0.5f), 0.001f); // Partial edge tile.

        std::memcpy(device->MapBuffer(lightBuffer), &lights, sizeof(lights));
        device->UnmapBuffer(lightBuffer);
        std::memcpy(device->MapBuffer(dataBuffer), &data, sizeof(data));
        device->UnmapBuffer(dataBuffer);
        auto& commands = device->GetCommandList();
        commands.Begin();
        commands.Barrier(BufferBarrier{.buffer = output, .oldState = outputState,
            .newState = ResourceState::ShaderWrite});
        commands.BindComputePipeline(pipeline);
        commands.SetComputeBindings(pipeline, 0, set);
        commands.Dispatch(columns, rows, 1);
        commands.Barrier(BufferBarrier{.buffer = output, .oldState = ResourceState::ShaderWrite,
            .newState = ResourceState::TransferSrc});
        commands.CopyBuffer(output, readback, {.size = outputSize});
        commands.End(); device->Submit(); device->WaitIdle();
        outputState = ResourceState::TransferSrc;
        const auto* mapped = static_cast<const std::uint32_t*>(device->MapBuffer(readback));
        std::vector<std::uint32_t> result(mapped, mapped + outputSize / sizeof(std::uint32_t));
        device->UnmapBuffer(readback);
        if (result[0] != columns || result[1] != rows || result[2] != 8 || result[3] != 16)
            throw std::runtime_error("Light tile header mismatch");
        for (std::uint32_t tile = 0; tile < columns * rows; ++tile) {
            for (std::uint32_t local = 0; local < 512; ++local) {
                const bool expected = !empty && !directionalOnly &&
                    (local == 0 || local == 3 || local == 31 || local == 32 || local == 510 ||
                        (local == 4 && tile == 0) || (local == 5 && tile == columns * rows - 1));
                const bool actual = (result[4 + tile * 16 + local / 32] & (1u << (local % 32))) != 0;
                if (actual != expected) throw std::runtime_error("Light tile mask mismatch: case " +
                    std::to_string(test) + ", tile " + std::to_string(tile) + ", light " + std::to_string(local));
            }
        }
    }
    if (benchmark) {
        constexpr std::uint32_t columns = 160, rows = 90;
        auto largeOutput = device->CreateBuffer({.size = (4 + columns * rows * 16) * 4,
            .usage = BufferUsage::Storage});
        const BindingBufferInfo largeInfo{.buffer = largeOutput, .range = (4 + columns * rows * 16) * 4};
        device->UpdateBindingSet({.dstSet = set, .binding = 3,
            .type = BindingType::StorageBuffer, .bufferInfo = &largeInfo});
        data.viewport = {1280, 720, columns, rows};
        data.view = glm::mat4(1);
        data.projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 1280.0f / 720, 0.1f, 1000.0f);
        data.projection[1][1] *= -1;
        lights.header = {100, 0, 0, 100};
        for (std::uint32_t i = 0; i < 100; ++i) {
            lights.lights[i].position = {float(i % 10) * 2 - 9, float(i / 10) * 2 - 9, -10, 1};
            lights.lights[i].direction.w = 10;
        }
        std::memcpy(device->MapBuffer(lightBuffer), &lights, sizeof(lights));
        device->UnmapBuffer(lightBuffer);
        std::memcpy(device->MapBuffer(dataBuffer), &data, sizeof(data));
        device->UnmapBuffer(dataBuffer);
        auto queries = device->CreateTimestampQueryPool({.queryCount = 21});
        auto& commands = device->GetCommandList();
        commands.Begin();
        commands.ResetQueryPool(queries, 0, 21);
        commands.BindComputePipeline(pipeline);
        commands.SetComputeBindings(pipeline, 0, set);
        commands.WriteTimestamp(queries, 0, ShaderStage::Compute);
        for (std::uint32_t sample = 0; sample < 20; ++sample) {
            commands.Dispatch(columns, rows, 1);
            commands.Barrier(BufferBarrier{.buffer = largeOutput,
                .oldState = ResourceState::ShaderWrite, .newState = ResourceState::ShaderWrite});
            commands.WriteTimestamp(queries, sample + 1, ShaderStage::Compute);
        }
        commands.End(); device->Submit(); device->WaitIdle();
        std::array<Velos::u64, 21> timestamps{};
        if (!device->GetTimestampQueryResults(queries, 0, 21, timestamps.data()))
            throw std::runtime_error("Tiling timestamp results unavailable");
        std::vector<double> samples;
        for (std::size_t i = 5; i < 20; ++i)
            samples.push_back((timestamps[i + 1] - timestamps[i]) * device->GetTimestampPeriodNanoseconds() / 1e6);
        std::sort(samples.begin(), samples.end());
        std::cout << "Tiling GPU benchmark: 1280x720, 100 lights: " << samples[samples.size() / 2] << " ms median\n";
        device->DestroyQueryPool(queries);
        device->DestroyBuffer(largeOutput);
    }
    device->DestroyPipeline(pipeline); device->DestroyBindingPool(pool);
    for (auto buffer : {lightBuffer, dataBuffer, output, readback}) device->DestroyBuffer(buffer);
    for (auto setLayout : layout.ownedSetLayouts) device->DestroyBindingLayout(setLayout);
    device->DestroyShader(shader);
    device.reset(); glfwTerminate();
    std::cout << "Light tiling readback: point/spot bounds, partial tiles, camera transform, empty/directional-only frames, 512 bits passed\n";
}
