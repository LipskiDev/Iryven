#include "hi_z.h"

#include <algorithm>
#include <bit>
#include <shader/shader_compiler.h>

namespace Iryven {

Renderer::HiZPass::HiZPass(Renderer& renderer) : renderer_(renderer)
{
    auto& device = *renderer_.device_;
    try {
        // Explicit layouts keep the stub's unused input bindings available
        // even when the shader compiler optimizes them out.
        const BindingDesc reductionBindings[] = {
            {.binding = 0, .type = BindingType::CombinedImageSampler, .visibility = ShaderStage::Compute},
            {.binding = 1, .type = BindingType::StorageImage, .visibility = ShaderStage::Compute},
        };
        reductionLayout_ = device.CreateBindingLayout({
            .bindings = reductionBindings, .bindingCount = 2, .debugName = "Hi-Z reduction layout"});
        const BindingDesc samplingBindings[] = {
            {.binding = 0, .type = BindingType::CombinedImageSampler, .visibility = ShaderStage::Task},
            {.binding = 1, .type = BindingType::UniformBuffer, .visibility = ShaderStage::Task},
        };
        samplingLayout_ = device.CreateBindingLayout({
            .bindings = samplingBindings, .bindingCount = 2, .debugName = "Hi-Z history layout"});
        const auto shader = Velos::ShaderCompiler::CompileFile({
            .path = "assets/shaders/internal/hi_z_reduce.comp.spv",
            .stage = ShaderStage::Compute, .entryPoint = "main",
            .language = Velos::ShaderSourceLanguage::SpirvBinary,
        });
        shader_ = device.CreateShader({
            .stage = ShaderStage::Compute, .bytecode = shader.spirv.data(),
            .bytecodeSize = shader.spirv.size() * sizeof(std::uint32_t),
            .entryPoint = "main", .reflection = shader.reflection, .debugName = "Hi-Z reduction shader",
        });
        pipeline_ = device.CreateComputePipeline({
            .computeShader = shader_,
            .layout = {.descriptorSetLayouts = &reductionLayout_, .descriptorSetLayoutCount = 1},
            .debugName = "Hi-Z pyramid pipeline",
        });
        const auto size = device.GetSwapchainDimensions();
        CreateResources(size.width, size.height);
    } catch (...) {
        Destroy();
        throw;
    }
}

Renderer::HiZPass::~HiZPass() { Destroy(); }

void Renderer::HiZPass::CreateResources(std::uint32_t width, std::uint32_t height)
{
    DestroyResources();
    if (width == 0 || height == 0) return;
    auto& device = *renderer_.device_;
    width_ = width;
    height_ = height;
    mipCount_ = std::bit_width(std::max(width, height));
    try {
        pyramid_ = device.CreateImage({
            .width = width_, .height = height_, .mipLevels = mipCount_,
            .format = Format::R32_FLOAT,
            .usage = ImageUsage::Storage | ImageUsage::Sampled,
            .concurrentQueues = true,
            .debugName = "Hi-Z depth pyramid",
        });
        pyramidView_ = device.CreateImageView({
            .image = pyramid_, .format = Format::R32_FLOAT,
            .mipLevelCount = mipCount_, .debugName = "Hi-Z full mip chain",
        });
        sampler_ = device.CreateSampler({
            .minFilter = Filter::Nearest, .magFilter = Filter::Nearest,
            .addressU = SamplerAddressMode::ClampToEdge,
            .addressV = SamplerAddressMode::ClampToEdge,
            .addressW = SamplerAddressMode::ClampToEdge,
            .maxLod = static_cast<float>(mipCount_ - 1), .debugName = "Hi-Z nearest sampler",
        });
        const BindingPoolSize sizes[] = {
            {BindingType::CombinedImageSampler, mipCount_ + k_FramesInFlight},
            {BindingType::StorageImage, mipCount_},
            {BindingType::UniformBuffer, k_FramesInFlight},
        };
        pool_ = device.CreateBindingPool({
            .poolSizes = sizes, .poolSizeCount = 3,
            .maxSets = mipCount_ + k_FramesInFlight, .debugName = "Hi-Z descriptor pool",
        });
        for (std::uint32_t mip = 0; mip < mipCount_; ++mip) {
            mipViews_.push_back(device.CreateImageView({
                .image = pyramid_, .format = Format::R32_FLOAT,
                .baseMipLevel = mip, .mipLevelCount = 1, .debugName = "Hi-Z single mip",
            }));
            const auto set = device.AllocateBindingSet({
                .pool = pool_, .layout = reductionLayout_, .debugName = "Hi-Z reduction set"});
            reductionSets_.push_back(set);
            const BindingImageInfo source{
                .sampler = sampler_,
                .imageView = mip == 0 ? renderer_.depthView_ : mipViews_[mip - 1],
                .imageLayout = ImageLayout::ShaderReadOnly,
            };
            const BindingImageInfo destination{
                .imageView = mipViews_[mip], .imageLayout = ImageLayout::General,
            };
            device.UpdateBindingSet({.dstSet = set, .binding = 0,
                .type = BindingType::CombinedImageSampler, .imageInfo = &source});
            device.UpdateBindingSet({.dstSet = set, .binding = 1,
                .type = BindingType::StorageImage, .imageInfo = &destination});
        }
        for (std::uint32_t frame = 0; frame < k_FramesInFlight; ++frame) {
            historyBuffers_[frame] = renderer_.CreateUploadBackedBuffer(
                sizeof(HistoryData), BufferUsage::Uniform, "Hi-Z history metadata", "Hi-Z history upload");
            samplingSets_[frame] = device.AllocateBindingSet({
                .pool = pool_, .layout = samplingLayout_, .debugName = "Hi-Z task sampling set"});
            const BindingImageInfo image{.sampler = sampler_, .imageView = pyramidView_,
                .imageLayout = ImageLayout::ShaderReadOnly};
            const BindingBufferInfo buffer{.buffer = historyBuffers_[frame].gpuBuffer,
                .range = sizeof(HistoryData)};
            device.UpdateBindingSet({.dstSet = samplingSets_[frame], .binding = 0,
                .type = BindingType::CombinedImageSampler, .imageInfo = &image});
            device.UpdateBindingSet({.dstSet = samplingSets_[frame], .binding = 1,
                .type = BindingType::UniformBuffer, .bufferInfo = &buffer});
        }
    } catch (...) {
        DestroyResources();
        throw;
    }
}

void Renderer::HiZPass::PrepareForSampling(ICommandList& commands)
{
    if (!layoutsInitialized_) {
        commands.Barrier(ImageBarrier{
            .image = pyramid_, .oldLayout = ImageLayout::Undefined,
            .newLayout = ImageLayout::ShaderReadOnly,
            .oldState = ResourceState::Undefined, .newState = ResourceState::ShaderRead,
            .useExplicitStates = true, .mipLevelCount = mipCount_,
        });
        layoutsInitialized_ = true;
    }
    // This metadata describes the SAME depth image sampled by the task shader,
    // not the current moving camera. No sampling is allowed until info.w != 0.
    const HistoryData history{
        .viewProjection = historyViewProjection_,
        .info = glm::uvec4(width_, height_, mipCount_, historyAvailable_ ? 1u : 0u),
    };
    renderer_.UploadBuffer(commands, historyBuffers_.at(renderer_.frame_.frameIndex),
        &history, sizeof(history), ResourceState::UniformBuffer);
}

void Renderer::HiZPass::Render(ICommandList& commands, const RenderScene& scene)
{
    if (!scene.camera || renderer_.cullingCameraFrozen_) return;
    // The graph transitions source depth to sampled-read before this call.
    // The depth dependency waits for this frame's graphics draws (including
    // pyramid reads). Next frame's opaque depth use waits for this compute
    // batch, also making the completed pyramid available to its task shaders.
    // Both depth and pyramid use concurrent queue-family sharing. Preserve
    // that dependency if depth is ever changed to per-frame images.
    commands.BindComputePipeline(pipeline_);
    for (std::uint32_t mip = 0; mip < mipCount_; ++mip) {
        const std::uint32_t dstWidth = std::max(1u, width_ >> mip);
        const std::uint32_t dstHeight = std::max(1u, height_ >> mip);
        const std::uint32_t sourceMip = mip == 0 ? 0 : mip - 1;
        const DispatchData constants{
            .sourceExtent = {std::max(1u, width_ >> sourceMip), std::max(1u, height_ >> sourceMip)},
            .destinationExtent = {dstWidth, dstHeight}, .destinationMip = mip,
        };
        commands.Barrier(ImageBarrier{
            .image = pyramid_,
            .oldLayout = layoutsInitialized_ ? ImageLayout::ShaderReadOnly : ImageLayout::Undefined,
            .newLayout = ImageLayout::General,
            .oldState = layoutsInitialized_ ? ResourceState::ShaderRead : ResourceState::Undefined,
            .newState = ResourceState::ShaderWrite, .useExplicitStates = true,
            .baseMipLevel = mip, .mipLevelCount = 1,
            .sourceQueue = QueueType::Compute, .destinationQueue = QueueType::Compute,
        });
        commands.SetComputeBindings(pipeline_, 0, reductionSets_[mip]);
        commands.PushConstants(ShaderStage::Compute, 0, sizeof(constants), &constants);
        commands.Dispatch((dstWidth + 7) / 8, (dstHeight + 7) / 8, 1);
        // Makes this mip available to the next dispatch, and to future task reads.
        commands.Barrier(ImageBarrier{
            .image = pyramid_, .oldLayout = ImageLayout::General,
            .newLayout = ImageLayout::ShaderReadOnly,
            .oldState = ResourceState::ShaderWrite, .newState = ResourceState::ShaderRead,
            .useExplicitStates = true, .baseMipLevel = mip, .mipLevelCount = 1,
            .sourceQueue = QueueType::Compute, .destinationQueue = QueueType::Compute,
        });
    }
    layoutsInitialized_ = true;
    historyViewProjection_ = renderer_.BuildFrameData(*scene.camera).viewProjection;
    historyAvailable_ = true;
}

void Renderer::HiZPass::OnResize(IDevice&, std::uint32_t width, std::uint32_t height)
{
    // Renderer has already waited idle and recreated depthView_.
    CreateResources(width, height);
}

void Renderer::HiZPass::DestroyResources()
{
    auto& device = *renderer_.device_;
    if (pool_) device.DestroyBindingPool(pool_);
    pool_ = {};
    samplingSets_ = {};
    reductionSets_.clear();
    for (auto& buffer : historyBuffers_) renderer_.DestroyUploadBackedBuffer(buffer);
    for (auto view : mipViews_) device.DestroyImageView(view);
    mipViews_.clear();
    if (pyramidView_) device.DestroyImageView(pyramidView_);
    if (pyramid_) device.DestroyImage(pyramid_);
    if (sampler_) device.DestroySampler(sampler_);
    pyramidView_ = {};
    pyramid_ = {};
    sampler_ = {};
    layoutsInitialized_ = historyAvailable_ = false;
    historyViewProjection_ = glm::mat4(1.0f);
    width_ = height_ = mipCount_ = 0;
}

void Renderer::HiZPass::Destroy()
{
    DestroyResources();
    auto& device = *renderer_.device_;
    if (pipeline_) device.DestroyPipeline(pipeline_);
    if (shader_) device.DestroyShader(shader_);
    if (reductionLayout_) device.DestroyBindingLayout(reductionLayout_);
    if (samplingLayout_) device.DestroyBindingLayout(samplingLayout_);
    pipeline_ = {};
    shader_ = {};
    reductionLayout_ = {};
    samplingLayout_ = {};
}

} // namespace Iryven
