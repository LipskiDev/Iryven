#include "ssao.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <random>
#include <shader/shader_compiler.h>

namespace Iryven {

Renderer::SSAOPass::SSAOPass(Renderer &renderer) : renderer_(renderer) {
  auto &device = *renderer_.device_;

  // Create shader helper
  try {
    const auto createShader = [&](const char *path, ShaderStage stage) {
      const auto code = Velos::ShaderCompiler::CompileFile({
          .path = path,
          .stage = stage,
          .entryPoint = "main",
          .language = Velos::ShaderSourceLanguage::HLSL,
          .outputFormat = GetBuildShaderBinaryFormat(),
      });
      return device.CreateShader({
          .stage = stage,
          .bytecode = code.GetBytecode().data(),
          .bytecodeSize = code.GetBytecode().size(),
          .entryPoint = "main",
          .reflection = code.reflection,
          .debugName = path,
      });
    };

    // Create needed shaders
    ssaoCompute_ =
        createShader("assets/shaders/internal/ssao.hlsl", ShaderStage::Compute);

    // Define bindings
    const BindingDesc bindings[] = {{.binding = 0,
                                     .type = BindingType::SampledTexture,
                                     .visibility = ShaderStage::Compute},
                                    {.binding = 1,
                                     .type = BindingType::SampledTexture,
                                     .visibility = ShaderStage::Compute},
                                    {.binding = 2,
                                     .type = BindingType::SampledTexture,
                                     .visibility = ShaderStage::Compute},
                                    {.binding = 3,
                                     .type = BindingType::StorageImage,
                                     .visibility = ShaderStage::Compute},
                                    {.binding = 4,
                                     .type = BindingType::UniformBuffer,
                                     .visibility = ShaderStage::Compute},
                                    {.binding = 5,
                                     .type = BindingType::UniformBuffer,
                                     .visibility = ShaderStage::Compute}};

    ssaoTextureLayout_ = device.CreateBindingLayout({
        .bindings = bindings,
        .bindingCount = 6,
        .debugName = "SSAOPass input",
    });

    // Create pipeline
    ssaoPipeline_ = device.CreateComputePipeline({
        .computeShader = ssaoCompute_,
        .layout =
            {
                .descriptorSetLayouts = &ssaoTextureLayout_,
                .descriptorSetLayoutCount = 1,
            },
        .debugName = "SSAO",
    });

    // Create Binding pool
    const BindingPoolSize ssaoPoolSizes[] = {
        {BindingType::SampledTexture, k_FramesInFlight * 3},
        {BindingType::StorageImage, k_FramesInFlight},
        {BindingType::UniformBuffer, k_FramesInFlight * 2},
    };

    const BindingPoolSize blurPoolSizes[] = {
        {BindingType::SampledTexture, k_FramesInFlight},
        {BindingType::StorageImage, k_FramesInFlight},
    };

    ssaoPool_ = device.CreateBindingPool({
        .poolSizes = ssaoPoolSizes,
        .poolSizeCount = 3,
        .maxSets = k_FramesInFlight,
        .debugName = "SSAOPass bindings",
    });

    // Allocate binding sets
    for (auto &set : ssaoSets_) {
      set = device.AllocateBindingSet(
          {.pool = ssaoPool_, .layout = ssaoTextureLayout_});
    }

    std::mt19937 kernelRandom{0};
    std::uniform_real_distribution<float> signedRandom{-1.0f, 1.0f};
    std::uniform_real_distribution<float> positiveRandom{0.0f, 1.0f};

    std::array<glm::vec4, kKernelSize> kernel{};
    for (auto &sample : kernel) {
      glm::vec3 direction;
      do {
        direction = {signedRandom(kernelRandom), signedRandom(kernelRandom),
                     positiveRandom(kernelRandom)};
      } while (glm::dot(direction, direction) < 1e-6f);
      sample = glm::vec4{glm::normalize(direction), 0.0f};
    }

    sampleKernelBuffer_ = device.CreateBuffer({
        .size = sizeof(kernel),
        .usage = BufferUsage::Uniform,
        .memoryUsage = MemoryUsage::CPUToGPU,
        .initialData = kernel.data(),
        .debugName = "SSAO sample kernel",
    });

    for (std::size_t i = 0; i < constantsBuffers_.size(); ++i) {
      constantsBuffers_[i] = device.CreateBuffer({
          .size = 256,
          .usage = BufferUsage::Uniform,
          .memoryUsage = MemoryUsage::CPUToGPU,
          .debugName = "SSAO constants",
      });

      const BindingBufferInfo kernelInfo{.buffer = sampleKernelBuffer_,
                                         .range = sizeof(kernel)};

      const BindingBufferInfo constantsInfo{.buffer = constantsBuffers_[i],
                                            .range = sizeof(SSAOConstants)};

      device.UpdateBindingSet({.dstSet = ssaoSets_[i],
                               .binding = 4,
                               .type = BindingType::UniformBuffer,
                               .bufferInfo = &kernelInfo});
      device.UpdateBindingSet({.dstSet = ssaoSets_[i],
                               .binding = 5,
                               .type = BindingType::UniformBuffer,
                               .bufferInfo = &constantsInfo});
    }

    ssaoNoiseTexture_ = device.CreateImage({
        .width = 4,
        .height = 4,
        .depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .format = Format::RGBA32_FLOAT,
        .type = ImageType::Image2D,
        .usage = ImageUsage::Sampled | ImageUsage::TransferDst,
        .debugName = "SSAO Noise Texture",
    });

    ssaoNoiseTextureView_ = device.CreateImageView({
        .image = ssaoNoiseTexture_,
        .format = Format::RGBA32_FLOAT,
        .type = ImageViewType::View2D,
        .aspect = ImageAspect::Color,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = 1,
        .debugName = "SSAO Noise Texture View",
    });

    std::mt19937 random{0};
    std::array<glm::vec4, 16> ssaoNoise{};
    for (auto &noise : ssaoNoise) {
      noise = glm::vec4{(rand() / float(RAND_MAX)) * 2.0f - 1.0f,
                        (rand() / float(RAND_MAX)) * 2.0f - 1.0f, 0.0f, 0.0f};
      noise = glm::normalize(noise);
    }
    auto uploadContext = device.CreateUploadContext(sizeof(ssaoNoise));

    uploadContext->Begin();
    uploadContext->UploadImage(
        {
            .dstImage = ssaoNoiseTexture_,
            .oldLayout = ImageLayout::Undefined,
            .finalLayout = ImageLayout::ShaderReadOnly,
            .aspect = ImageAspect::Color,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
            .width = 4,
            .height = 4,
            .depth = 1,
        },
        ssaoNoise.data(), sizeof(ssaoNoise));
    uploadContext->Flush();
    device.AcquireUploadedImages(uploadContext->TakePendingImageAcquires());

  } catch (...) {
    // If anything failed, destroy the rest
    Destroy();
    throw;
  }
}

void Renderer::SSAOPass::SetSettings(std::uint32_t sampleCount, float radius,
                                     float bias) {
  ssaoConstants_.sampleCount = std::clamp(sampleCount, 1u, kKernelSize);
  ssaoConstants_.radius = std::clamp(radius, 0.01f, 5.0f);
  ssaoConstants_.bias = std::clamp(bias, 0.0f, 0.5f);
}

Renderer::SSAOPass::~SSAOPass() {
  // Ensure Destroy called before destruction
  Destroy();
}

void Renderer::SSAOPass::Destroy() {
  // Destroy resources in reverse order of creation

  auto &device = *renderer_.device_;
  if (ssaoPipeline_)
    device.DestroyPipeline(ssaoPipeline_);
  if (ssaoPool_)
    device.DestroyBindingPool(ssaoPool_);
  for (auto buffer : constantsBuffers_)
    if (buffer)
      device.DestroyBuffer(buffer);
  if (sampleKernelBuffer_)
    device.DestroyBuffer(sampleKernelBuffer_);
  if (ssaoNoiseTextureView_)
    device.DestroyImageView(ssaoNoiseTextureView_);
  if (ssaoNoiseTexture_)
    device.DestroyImage(ssaoNoiseTexture_);
  if (ssaoTextureLayout_)
    device.DestroyBindingLayout(ssaoTextureLayout_);
  if (ssaoCompute_)
    device.DestroyShader(ssaoCompute_);
}

void Renderer::SSAOPass::PreRender(ICommandList &, const RenderScene &) {
  const auto &sceneDepth = std::get<FrameGraphTextureInfo>(
      renderer_.frameGraph_.GetResource("depth")->info);
  const auto &sceneNormal = std::get<FrameGraphTextureInfo>(
      renderer_.frameGraph_.GetResource("gNormalRoughness")->info);

  const BindingImageInfo depthImage{
      .imageView = sceneDepth.view,
      .imageLayout = ImageLayout::ShaderReadOnly,
  };

  const BindingImageInfo normalImage{
      .imageView = sceneNormal.view,
      .imageLayout = ImageLayout::ShaderReadOnly,
  };

  renderer_.device_->UpdateBindingSet({
      .dstSet = ssaoSets_.at(renderer_.frame_.frameIndex),
      .binding = 0,
      .type = BindingType::SampledTexture,
      .imageInfo = &depthImage,
  });
  renderer_.device_->UpdateBindingSet({
      .dstSet = ssaoSets_.at(renderer_.frame_.frameIndex),
      .binding = 1,
      .type = BindingType::SampledTexture,
      .imageInfo = &normalImage,
  });
  const auto &output = std::get<FrameGraphTextureInfo>(
      renderer_.frameGraph_.GetResource("ssao")->info);
  const BindingImageInfo destination{
      .imageView = output.view,
      .imageLayout = ImageLayout::General,
  };
  renderer_.device_->UpdateBindingSet({
      .dstSet = ssaoSets_.at(renderer_.frame_.frameIndex),
      .binding = 3,
      .type = BindingType::StorageImage,
      .imageInfo = &destination,
  });

  const BindingImageInfo noiseImage{
      .imageView = ssaoNoiseTextureView_,
      .imageLayout = ImageLayout::ShaderReadOnly,
  };
  renderer_.device_->UpdateBindingSet({
      .dstSet = ssaoSets_.at(renderer_.frame_.frameIndex),
      .binding = 2,
      .type = BindingType::SampledTexture,
      .imageInfo = &noiseImage,
  });
}

void Renderer::SSAOPass::Render(ICommandList &commands,
                                const RenderScene &scene) {
  if (!scene.camera)
    return;

  static_assert(sizeof(glm::vec4) == 16);
  static_assert(sizeof(SSAOConstants) == 144);
  static_assert(offsetof(SSAOConstants, sampleCount) == 128);
  static_assert(offsetof(SSAOConstants, radius) == 132);
  static_assert(offsetof(SSAOConstants, bias) == 136);
  const auto frameData = renderer_.BuildFrameData(*scene.camera);
  ssaoConstants_.viewProjection = frameData.viewProjection;
  ssaoConstants_.inverseViewProjection = glm::inverse(frameData.viewProjection);

  // Bind Pipeline
  commands.BindComputePipeline(ssaoPipeline_);

  // Set Bindings
  commands.SetComputeBindings(ssaoPipeline_, 0,
                              ssaoSets_.at(renderer_.frame_.frameIndex));

  auto &device = *renderer_.device_;
  const auto buffer = constantsBuffers_.at(renderer_.frame_.frameIndex);
  std::memcpy(device.MapBuffer(buffer), &ssaoConstants_,
              sizeof(ssaoConstants_));
  device.UnmapBuffer(buffer);

  // Dispatch over the output texture
  const auto &outputInfo = std::get<FrameGraphTextureInfo>(
      renderer_.frameGraph_.GetResource("ssao")->info);
  commands.Dispatch((outputInfo.width + 15) / 16, (outputInfo.height + 15) / 16,
                    1);
}

}  // namespace Iryven
