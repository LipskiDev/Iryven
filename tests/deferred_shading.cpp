#include <rhi/device.h>
#include <shader/shader_compiler.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

extern "C" int glfwInit();
extern "C" void glfwTerminate();

void RunDeferredShadingPixelTests()
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
    const auto fs = compile("assets/shaders/internal/shading.frag.spv", ShaderStage::Fragment);
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
        {.type = BindingType::UniformBuffer, .count = 2},
        {.type = BindingType::CombinedImageSampler, .count = 5}};
    auto pool = device->CreateBindingPool({.poolSizes = sizes, .poolSizeCount = 2, .maxSets = 2});
    auto frameSet = device->AllocateBindingSet({.pool = pool, .layout = layout.setLayouts[0]});
    auto imageSet = device->AllocateBindingSet({.pool = pool, .layout = layout.setLayouts[1]});
    struct Light { glm::vec4 position, direction, color, spot; };
    struct Lights { glm::uvec4 header{1, 0, 0, 0}; std::array<Light, 512> lights{}; } lights;
    // Position reconstructed from depth must be (0,0,2.5), exactly one unit from this point light.
    lights.lights[0] = {{0, 0, 3.5f, 1}, {0, 0, -1, 10}, {1, 1, 1, 1}, {}};
    std::array<float, 104> frame{};
    frame[50] = 4; frame[51] = 1; // Camera position at byte offset 192.
    auto lightBuffer = device->CreateBuffer({.size = sizeof(lights), .usage = BufferUsage::Uniform,
        .memoryUsage = MemoryUsage::CPUToGPU, .initialData = &lights});
    auto frameBuffer = device->CreateBuffer({.size = sizeof(frame), .usage = BufferUsage::Uniform,
        .memoryUsage = MemoryUsage::CPUToGPU, .initialData = frame.data()});
    const BindingBufferInfo buffers[]{{.buffer = lightBuffer, .range = sizeof(lights)},
        {.buffer = frameBuffer, .range = sizeof(frame)}};
    for (std::uint32_t i = 0; i < 2; ++i) device->UpdateBindingSet({.dstSet = frameSet,
        .binding = i, .type = BindingType::UniformBuffer, .bufferInfo = &buffers[i]});
    auto sampler = device->CreateSampler({.minFilter = Filter::Nearest, .magFilter = Filter::Nearest});
    std::array<ImageHandle, 6> images;
    std::array<ImageViewHandle, 6> views;
    for (std::uint32_t i = 0; i < images.size(); ++i) {
        images[i] = device->CreateImage({.width = 1, .height = 1, .format = Format::RGBA32_FLOAT,
            .usage = ImageUsage::ColorAttachment | ImageUsage::Sampled | ImageUsage::TransferSrc});
        views[i] = device->CreateImageView({.image = images[i], .format = Format::RGBA32_FLOAT});
        if (i < 5) {
            const BindingImageInfo image{.sampler = sampler, .imageView = views[i],
                .imageLayout = ImageLayout::ShaderReadOnly};
            device->UpdateBindingSet({.dstSet = imageSet, .binding = i,
                .type = BindingType::CombinedImageSampler, .imageInfo = &image});
        }
    }
    auto readback = device->CreateBuffer({.size = 16, .usage = BufferUsage::TransferDst,
        .memoryUsage = MemoryUsage::GPUToCPU});
    const glm::vec3 base{0.2f, 0.4f, 0.6f}, emissive{0.1f, 0.05f, 0.0f};
    constexpr float pi = 3.14159265359f;
    for (int mode = 0; mode < 4; ++mode) {
        const bool sg = mode == 2, metallic = mode == 3;
        const glm::vec3 f0 = sg ? glm::vec3(0.2f, 0.3f, 0.4f) : metallic ? base : glm::vec3(0.04f);
        const std::array<ClearColor, 6> clear{{
            {base.r, base.g, base.b, metallic ? 1.0f : 0.0f},
            {0, 0, mode == 0 ? 0.0f : 1.0f, 1},
            {emissive.r, emissive.g, emissive.b, 0.5f},
            {f0.r, f0.g, f0.b, sg ? 1.0f : 0.0f},
            {0.5f, 0, 0, 0}, {0.25f, 0.5f, 0.75f, 1}}};
        auto& commands = device->GetCommandList();
        commands.Begin();
        for (std::size_t i = 0; i < images.size(); ++i) {
            commands.Barrier(ImageBarrier{.image = images[i],
                .oldLayout = device->GetImageLayout(images[i], 0), .newLayout = ImageLayout::ColorAttachment});
            const ColorAttachmentDesc attachment{.view = views[i], .clearValue = clear[i]};
            commands.BeginRendering({.renderArea = {.extent = {1, 1}},
                .colorAttachments = &attachment, .colorAttachmentCount = 1});
            commands.EndRendering();
            if (i < 5) commands.Barrier(ImageBarrier{.image = images[i],
                .oldLayout = ImageLayout::ColorAttachment, .newLayout = ImageLayout::ShaderReadOnly});
        }
        const ColorAttachmentDesc output{.view = views[5], .loadOp = LoadOp::Load};
        commands.BeginRendering({.renderArea = {.extent = {1, 1}},
            .colorAttachments = &output, .colorAttachmentCount = 1});
        commands.BindPipeline(pipeline);
        commands.SetViewport({.width = 1, .height = 1});
        commands.SetScissor({.extent = {1, 1}});
        commands.SetBindings(pipeline, 0, frameSet);
        commands.SetBindings(pipeline, 1, imageSet);
        const auto inverseVP = glm::translate(glm::mat4(1), glm::vec3(0, 0, 2));
        commands.PushConstants(ShaderStage::Fragment, 0, sizeof(inverseVP), &inverseVP);
        commands.Draw(3);
        commands.EndRendering();
        commands.Barrier(ImageBarrier{.image = images[5],
            .oldLayout = ImageLayout::ColorAttachment, .newLayout = ImageLayout::TransferSrc});
        commands.CopyImageToBuffer(images[5], readback, {.imageExtent = {1, 1, 1}});
        commands.End(); device->Submit(); device->WaitIdle();
        const float diffuseWeight = sg ? 0.6f : metallic ? 0.0f : 1.0f;
        const glm::vec3 diffuse = (sg ? glm::vec3(1) : 1.0f - f0) * diffuseWeight * base / pi;
        const glm::vec3 expected = mode == 0 ? glm::vec3(0.25f, 0.5f, 0.75f)
            : base * diffuseWeight * 0.025f + (diffuse + f0 / (4 * pi)) * 0.81f + emissive;
        const auto* pixel = static_cast<const float*>(device->MapBuffer(readback));
        bool matches = std::abs(pixel[3] - 1.0f) < 0.0001f;
        for (int c = 0; c < 3; ++c) matches &= std::abs(pixel[c] - expected[c]) < 0.0001f;
        device->UnmapBuffer(readback);
        if (!matches) throw std::runtime_error("Deferred shading pixel mismatch, case " + std::to_string(mode));
    }
    device->DestroyBuffer(readback);
    device->DestroyPipeline(pipeline); device->DestroyBindingPool(pool);
    for (auto view : views) device->DestroyImageView(view);
    for (auto image : images) device->DestroyImage(image);
    device->DestroySampler(sampler);
    device->DestroyBuffer(lightBuffer); device->DestroyBuffer(frameBuffer);
    for (auto setLayout : layout.ownedSetLayouts) device->DestroyBindingLayout(setLayout);
    device->DestroyShader(fragment); device->DestroyShader(vertex);
    device.reset(); glfwTerminate();
    std::cout << "Deferred shading readback: background, dielectric, specular-glossiness, metallic passed\n";
}
