#include <rhi/device.h>
#include <shader/shader_compiler.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include "../engine/src/renderer/light_depth_bins.h"
#include <cstring>
#include <vector>
#include <algorithm>

extern "C" int glfwInit();
extern "C" void glfwTerminate();

void RunDeferredShadingPixelTests(bool benchmark, const char* fragmentPath)
{
    using namespace Velos::RHI;
    if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
    std::unique_ptr<IDevice, decltype(&DestroyDevice)> device(CreateDevice({
        .graphicsAPI = GraphicsAPI::Vulkan, .enableValidation = true,
        .applicationName = "Deferred shading readback", .pipelineCachePath = nullptr}), DestroyDevice);
    const auto compile = [&](const char* path, ShaderStage stage) {
        return Velos::ShaderCompiler::CompileFile({.path = path, .stage = stage,
            .language = Velos::ShaderSourceLanguage::SpirvBinary});
    };
    const auto vs = compile("assets/shaders/internal/shading.vert.spv", ShaderStage::Vertex);
    const auto fs = compile(fragmentPath ? fragmentPath : "assets/shaders/internal/shading.frag.spv", ShaderStage::Fragment);
    auto vertex = device->CreateShader({.stage = ShaderStage::Vertex, .bytecode = vs.spirv.data(),
        .bytecodeSize = vs.spirv.size() * 4, .reflection = vs.reflection});
    auto fragment = device->CreateShader({.stage = ShaderStage::Fragment, .bytecode = fs.spirv.data(),
        .bytecodeSize = fs.spirv.size() * 4, .reflection = fs.reflection});
    const std::array reflections{vs.reflection, fs.reflection};
    auto layout = device->BuildPipelineLayout(Velos::ShaderCompiler::MergeShaderReflection(reflections));
    auto pipeline = device->CreateGraphicsPipeline({.vertexShader = vertex, .fragmentShader = fragment,
        .layout = {.descriptorSetLayouts = layout.setLayouts.data(), .descriptorSetLayoutCount = 2},
        .raster = {.cullBackFaces = false}, .colorFormat = Format::RGBA32_FLOAT});
    const BindingPoolSize sizes[]{
        {.type = BindingType::UniformBuffer, .count = 1},
        {.type = BindingType::StorageBuffer, .count = 3},
        {.type = BindingType::CombinedImageSampler, .count = 5}};
    auto pool = device->CreateBindingPool({.poolSizes = sizes, .poolSizeCount = 3, .maxSets = 2});
    auto frameSet = device->AllocateBindingSet({.pool = pool, .layout = layout.setLayouts[0]});
    auto imageSet = device->AllocateBindingSet({.pool = pool, .layout = layout.setLayouts[1]});
    struct Light { glm::vec4 position, direction, color, spot; };
    struct Lights { glm::uvec4 header{1, 0, 0, 1}; std::array<Light, 512> lights{}; } lights;
    // Position reconstructed from depth must be (0,0,2.5), exactly one unit from this point light.
    lights.lights[0] = {{0, 0, 3.5f, 1}, {0, 0, -1, 10}, {1, 1, 1, 1}, {}};
    std::array<float, 104> frame{};
    frame[50] = 4; frame[51] = 1; // Camera position at byte offset 192.
    auto lightBuffer = device->CreateBuffer({.size = sizeof(lights), .usage = BufferUsage::Storage,
        .memoryUsage = MemoryUsage::CPUToGPU, .initialData = &lights});
    auto frameBuffer = device->CreateBuffer({.size = sizeof(frame), .usage = BufferUsage::Uniform,
        .memoryUsage = MemoryUsage::CPUToGPU, .initialData = frame.data()});
    const BindingBufferInfo buffers[]{{.buffer = lightBuffer, .range = sizeof(lights)},
        {.buffer = frameBuffer, .range = sizeof(frame)}};
    for (std::uint32_t i = 0; i < 2; ++i) device->UpdateBindingSet({.dstSet = frameSet,
        .binding = i, .type = i == 0 ? BindingType::StorageBuffer : BindingType::UniformBuffer, .bufferInfo = &buffers[i]});
    const std::uint32_t width = benchmark ? 1280 : 1, height = benchmark ? 720 : 1;
    const auto columns = (width + 7) / 8, rows = (height + 7) / 8;
    std::vector<std::uint32_t> tiles(4 + columns * rows * 16);
    tiles[0] = columns; tiles[1] = rows; tiles[2] = 8; tiles[3] = 16; tiles[4] = 1;
    auto tileBuffer = device->CreateBuffer({.size = tiles.size() * 4, .usage = BufferUsage::Storage,
        .memoryUsage = MemoryUsage::CPUToGPU, .initialData = tiles.data()});
    const BindingBufferInfo tileInfo{.buffer = tileBuffer, .range = tiles.size() * 4};
    device->UpdateBindingSet({.dstSet = frameSet, .binding = 3,
        .type = BindingType::StorageBuffer, .bufferInfo = &tileInfo});
    Iryven::RenderCamera binCamera;
    binCamera.view[2][2] = -1; // This fixture shades positive world Z.
    binCamera.farPlane = 10;
    const std::array localLights{Iryven::RenderLight{.type = Iryven::LightType::Point,
        .position = {0, 0, 3.5f}, .range = 10}};
    const auto initialBins = Iryven::BuildLightDepthBins(binCamera, localLights);
    auto binBuffer = device->CreateBuffer({.size = sizeof(initialBins), .usage = BufferUsage::Storage,
        .memoryUsage = MemoryUsage::CPUToGPU, .initialData = &initialBins});
    const BindingBufferInfo binInfo{.buffer = binBuffer, .range = sizeof(initialBins)};
    device->UpdateBindingSet({.dstSet = frameSet, .binding = 4,
        .type = BindingType::StorageBuffer, .bufferInfo = &binInfo});
    auto sampler = device->CreateSampler({.minFilter = Filter::Nearest, .magFilter = Filter::Nearest});
    std::array<ImageHandle, 6> images;
    std::array<ImageViewHandle, 6> views;
    for (std::uint32_t i = 0; i < images.size(); ++i) {
        images[i] = device->CreateImage({.width = width, .height = height, .format = Format::RGBA32_FLOAT,
            .usage = ImageUsage::ColorAttachment | ImageUsage::Sampled | ImageUsage::TransferSrc});
        views[i] = device->CreateImageView({.image = images[i], .format = Format::RGBA32_FLOAT});
        if (i < 5) {
            const BindingImageInfo image{.sampler = sampler, .imageView = views[i],
                .imageLayout = ImageLayout::ShaderReadOnly};
            device->UpdateBindingSet({.dstSet = imageSet, .binding = i,
                .type = BindingType::CombinedImageSampler, .imageInfo = &image});
        }
    }
    auto readback = device->CreateBuffer({.size = std::uint64_t(width) * height * 16, .usage = BufferUsage::TransferDst,
        .memoryUsage = MemoryUsage::GPUToCPU});
    const glm::vec3 base{0.2f, 0.4f, 0.6f}, emissive{0.1f, 0.05f, 0.0f};
    constexpr float pi = 3.14159265359f;
    auto queries = benchmark ? device->CreateTimestampQueryPool({.queryCount = 21}) : QueryPoolHandle{};
    for (int mode = 0; mode < (benchmark ? 4 : 17); ++mode) {
        auto bins = initialBins;
        if (mode == 5 || mode == 7 || mode == 8) bins.ranges.fill(Iryven::LightDepthBins::kEmpty);
        if (mode == 6) {
            // Select local index 1, verifying min/max decoding and range offset.
            bins.ranges.fill(0x00010001u);
            lights.lights[1] = lights.lights[0];
            lights.header = {2, 0, 0, 2};
        } else lights.header = {1, 0, 0, 1};
        if (mode == 7) {
            // Directional lighting survives both an empty bin and an empty tile.
            lights.header = {1, 1, 1, 0};
            lights.lights[0].position.w = 0;
            lights.lights[0].color.w = 0.81f;
        } else {
            lights.lights[0].position.w = 1;
            lights.lights[0].color.w = 1;
        }
        if (mode == 8) bins.mapping = {0, 1, 256, 0}; // Outside interval: conservative fallback.
        if (mode >= 9 && mode <= 11) {
            // Exercise clipped first/last words, a full word, and the highest bit.
            const std::uint32_t local = mode == 11 ? 511 : 32;
            lights.header = {512, 0, 0, 512};
            lights.lights[local] = lights.lights[0];
            bins.ranges.fill(mode == 9 ? (32u << 16u) | 31u
                : mode == 10 ? (63u << 16u) | 32u : (511u << 16u) | 511u);
        }
        if (!benchmark && mode >= 12) {
            // The masks admit this light even though its final attenuation is zero.
            lights.lights[0] = {{0, 0, 3.5f, 1}, {0, 0, -1, 10}, {1, 1, 1, 1}, {}};
            if (mode == 12) lights.lights[0].direction.w = 0.5f; // Beyond range.
            if (mode == 16) lights.lights[0].color.w = 0; // Zero-energy light must not count.
            if (mode == 13) lights.lights[0].direction.w = 1.0f; // Exactly at range.
            if (mode == 14) {
                lights.lights[0].position.w = 2;
                lights.lights[0].direction.z = 1; // Spot points away from the fragment.
                lights.lights[0].spot = {0.9f, 0.8f, 0, 0};
            }
        }
        if (benchmark) {
            lights.header = {100, 0, 0, 100};
            for (std::uint32_t i = 0; i < 100; ++i)
                lights.lights[i] = {{float(i % 10) * 2 - 9, float(i / 10) * 2 - 9, 3.5f, 1},
                    {0, 0, -1, 10}, {1, 1, 1, 1}, {}};
            bins.ranges.fill(99u << 16);
            if (mode == 3) {
                // Vary ranges within a subgroup, including empty lanes.
                bins.depthPlane = {1, 0, 0, 0};
                bins.mapping = {-1, 1, 128, 0};
                for (std::uint32_t bin = 0; bin < 256; ++bin)
                    bins.ranges[bin] = bin % 3 == 0 ? Iryven::LightDepthBins::kEmpty
                        : bin % 3 == 1 ? (32u << 16) | 31u : (99u << 16) | 64u;
            }
        }
        std::memcpy(device->MapBuffer(lightBuffer), &lights, sizeof(lights));
        device->UnmapBuffer(lightBuffer);
        std::memcpy(device->MapBuffer(binBuffer), &bins, sizeof(bins));
        device->UnmapBuffer(binBuffer);
        auto* mask = static_cast<std::uint32_t*>(device->MapBuffer(tileBuffer));
        std::fill_n(mask + 4, 16, 0u);
        mask[4] = mode == 4 || mode == 7 ? 0 : mode == 6 ? 2 : 1;
        if (mode == 9 || mode == 10) {
            mask[5] = 1u;
            mask[6] = 1u; // Outside the range: must not be visited.
        }
        if (mode == 11) mask[19] = 1u << 31u;
        if (benchmark) {
            const std::uint32_t visible = mode == 0 ? 1 : mode == 1 ? 10 : 100;
            for (std::uint32_t tile = 0; tile < columns * rows; ++tile) {
                std::fill_n(mask + 4 + tile * 16, 16, 0u);
                for (std::uint32_t i = 0; i < visible; ++i) {
                    const auto local = (tile + i * 7) % 100;
                    mask[4 + tile * 16 + local / 32] |= 1u << (local % 32);
                }
            }
        }
        device->UnmapBuffer(tileBuffer);
        const bool sg = mode == 2, metallic = mode == 3;
        const glm::vec3 f0 = sg ? glm::vec3(0.2f, 0.3f, 0.4f) : metallic ? base : glm::vec3(0.04f);
        const std::array<ClearColor, 6> clear{{
            {base.r, base.g, base.b, metallic ? 1.0f : 0.0f},
            {0, 0, mode == 0 && !benchmark ? 0.0f : 1.0f, 1},
            {emissive.r, emissive.g, emissive.b, 0.5f},
            {f0.r, f0.g, f0.b, sg ? 1.0f : 0.0f},
            {0.5f, 0, 0, 0}, {0.25f, 0.5f, 0.75f, 1}}};
        auto& commands = device->GetCommandList();
        commands.Begin();
        if (benchmark) commands.ResetQueryPool(queries, 0, 21);
        for (std::size_t i = 0; i < images.size(); ++i) {
            commands.Barrier(ImageBarrier{.image = images[i],
                .oldLayout = device->GetImageLayout(images[i], 0), .newLayout = ImageLayout::ColorAttachment});
            const ColorAttachmentDesc attachment{.view = views[i], .clearValue = clear[i]};
            commands.BeginRendering({.renderArea = {.extent = {width, height}},
                .colorAttachments = &attachment, .colorAttachmentCount = 1});
            commands.EndRendering();
            if (i < 5) commands.Barrier(ImageBarrier{.image = images[i],
                .oldLayout = ImageLayout::ColorAttachment, .newLayout = ImageLayout::ShaderReadOnly});
        }
        const ColorAttachmentDesc output{.view = views[5], .loadOp = LoadOp::Load};
        commands.BeginRendering({.renderArea = {.extent = {width, height}},
            .colorAttachments = &output, .colorAttachmentCount = 1});
        commands.BindPipeline(pipeline);
        commands.SetViewport({.width = float(width), .height = float(height)});
        commands.SetScissor({.extent = {width, height}});
        commands.SetBindings(pipeline, 0, frameSet);
        commands.SetBindings(pipeline, 1, imageSet);
        const auto inverseVP = glm::translate(glm::mat4(1), glm::vec3(0, 0, 2));
        struct Constants { glm::mat4 inverseVP; std::uint32_t lightCountView; };
        const Constants constants{inverseVP, !benchmark && mode >= 15 ? 1u : 0u};
        commands.PushConstants(ShaderStage::Fragment, 0, 68, &constants);
        if (benchmark) {
            commands.WriteTimestamp(queries, 0, ShaderStage::Fragment);
            for (std::uint32_t sample = 0; sample < 20; ++sample) {
                commands.Draw(3);
                commands.WriteTimestamp(queries, sample + 1, ShaderStage::Fragment);
            }
        } else commands.Draw(3);
        commands.EndRendering();
        commands.Barrier(ImageBarrier{.image = images[5],
            .oldLayout = ImageLayout::ColorAttachment, .newLayout = ImageLayout::TransferSrc});
        commands.CopyImageToBuffer(images[5], readback, {.imageExtent = {width, height, 1}});
        commands.End(); device->Submit(); device->WaitIdle();
        if (benchmark) {
            std::array<Velos::u64, 21> timestamps{};
            if (!device->GetTimestampQueryResults(queries, 0, 21, timestamps.data()))
                throw std::runtime_error("Lighting timestamp results unavailable");
            std::vector<double> samples;
            for (std::size_t i = 5; i < 20; ++i)
                samples.push_back((timestamps[i + 1] - timestamps[i]) * device->GetTimestampPeriodNanoseconds() / 1e6);
            std::sort(samples.begin(), samples.end());
            std::cout << "Lighting GPU benchmark: 1280x720, 100 candidates, " << (mode == 0 ? 1 : mode == 1 ? 10 : 100)
                << " tile bits: " << samples[samples.size() / 2] << " ms median\n";
            const auto* pixels = static_cast<const std::uint32_t*>(device->MapBuffer(readback));
            std::uint64_t hash = 14695981039346656037ull;
            for (std::size_t i = 0; i < std::size_t(width) * height * 4; ++i)
                hash = (hash ^ pixels[i]) * 1099511628211ull;
            device->UnmapBuffer(readback);
            std::cout << "Lighting image case " << mode << " hash: " << hash << '\n';
            continue;
        }
        const float diffuseWeight = sg ? 0.6f : metallic ? 0.0f : 1.0f;
        const glm::vec3 diffuse = (sg ? glm::vec3(1) : 1.0f - f0) * diffuseWeight * base / pi;
        const glm::vec3 expected = mode == 15 ? glm::vec3(0, 0, 1)
            : mode == 16 ? glm::vec3(0) : mode == 0 ? glm::vec3(0.25f, 0.5f, 0.75f)
            : mode == 4 || mode == 5 || mode >= 12 ? base * 0.025f + emissive
            : base * diffuseWeight * 0.025f + (diffuse + f0 / (4 * pi)) * 0.81f + emissive;
        const auto* pixel = static_cast<const float*>(device->MapBuffer(readback));
        bool matches = std::abs(pixel[3] - 1.0f) < 0.0001f;
        for (int c = 0; c < 3; ++c) matches &= std::abs(pixel[c] - expected[c]) < 0.0001f;
        device->UnmapBuffer(readback);
        if (!matches) throw std::runtime_error("Deferred shading pixel mismatch, case " + std::to_string(mode));
    }
    device->DestroyBuffer(readback);
    if (queries) device->DestroyQueryPool(queries);
    device->DestroyPipeline(pipeline); device->DestroyBindingPool(pool);
    for (auto view : views) device->DestroyImageView(view);
    for (auto image : images) device->DestroyImage(image);
    device->DestroySampler(sampler);
    device->DestroyBuffer(lightBuffer); device->DestroyBuffer(frameBuffer);
    device->DestroyBuffer(tileBuffer);
    device->DestroyBuffer(binBuffer);
    for (auto setLayout : layout.ownedSetLayouts) device->DestroyBindingLayout(setLayout);
    device->DestroyShader(fragment); device->DestroyShader(vertex);
    device.reset(); glfwTerminate();
    if (!benchmark) std::cout << "Deferred shading readback: materials, tile rejection, empty bins, local offsets, directional bypass, depth fallback, word boundaries passed\n";
}
