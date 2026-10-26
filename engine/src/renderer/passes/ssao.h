#pragma once

#include "../renderer.h"

namespace Iryven {

class Renderer::SSAOPass final : public FrameGraphRenderPass {
 public:
  explicit SSAOPass(Renderer &renderer);
  ~SSAOPass() override;

  void AddUI() override {}
  void SetSettings(std::uint32_t sampleCount, float radius, float bias);
  void PreRender(ICommandList &, const RenderScene &) override;
  void Render(ICommandList &, const RenderScene &) override;
  void OnResize(IDevice &, std::uint32_t, std::uint32_t) override {}

 private:
  void Destroy();

  Renderer &renderer_;

  ShaderHandle ssaoCompute_{};
  PipelineHandle ssaoPipeline_{};
  BindingLayoutHandle ssaoTextureLayout_{};

  BindingPoolHandle ssaoPool_{};
  std::array<BindingSetHandle, k_FramesInFlight> ssaoSets_{};

  static constexpr std::uint32_t kKernelSize = 64;
  struct alignas(16) SSAOConstants {
    glm::mat4 viewProjection{1.0f};
    glm::mat4 inverseViewProjection{1.0f};
    std::uint32_t sampleCount = 64;
    float radius = 0.25f;
    float bias = 0.015f;
    float padding = 0.0f;
  } ssaoConstants_;
  BufferHandle sampleKernelBuffer_{};
  std::array<BufferHandle, k_FramesInFlight> constantsBuffers_{};

  ImageHandle ssaoNoiseTexture_{};
  ImageViewHandle ssaoNoiseTextureView_{};
};

}  // namespace Iryven
