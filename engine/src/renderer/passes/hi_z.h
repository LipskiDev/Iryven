#pragma once

#include "../renderer.h"

namespace Iryven {

// Compute-queue pass. Produces history after the frame's opaque draws.
class Renderer::HiZPass final : public FrameGraphRenderPass {
public:
    explicit HiZPass(Renderer& renderer);
    ~HiZPass() override;
    void AddUI() override {}
    void PreRender(Velos::RHI::ICommandList&, const RenderScene&) override {}
    void Render(Velos::RHI::ICommandList&, const RenderScene&) override;
    void OnResize(Velos::RHI::IDevice&, std::uint32_t width, std::uint32_t height) override;

    // Called before task-shader draws, outside dynamic rendering.
    void PrepareForSampling(Velos::RHI::ICommandList& commands);
    BindingLayoutHandle SamplingLayout() const { return samplingLayout_; }
    BindingSetHandle SamplingSet() const { return samplingSets_.at(renderer_.frame_.frameIndex); }

private:
    struct alignas(16) HistoryData {
        glm::mat4 viewProjection{1.0f};
        // x=width, y=height, z=mip count, w=history available.
        glm::uvec4 info{0u};
    };
    struct DispatchData {
        glm::uvec2 sourceExtent;
        glm::uvec2 destinationExtent;
        std::uint32_t destinationMip;
        std::uint32_t padding[3]{};
    };
    static_assert(sizeof(HistoryData) == 80);
    static_assert(sizeof(DispatchData) == 32);

    void CreateResources(std::uint32_t width, std::uint32_t height);
    void DestroyResources();
    void Destroy();

    Renderer& renderer_;
    ShaderHandle shader_;
    PipelineHandle pipeline_;
    BindingLayoutHandle reductionLayout_;
    BindingLayoutHandle samplingLayout_;
    BindingPoolHandle pool_;
    SamplerHandle sampler_;
    ImageHandle pyramid_;
    ImageViewHandle pyramidView_;
    std::vector<ImageViewHandle> mipViews_;
    std::vector<BindingSetHandle> reductionSets_;
    std::array<BindingSetHandle, k_FramesInFlight> samplingSets_{};
    std::array<UploadBackedBuffer, k_FramesInFlight> historyBuffers_{};
    std::uint32_t width_ = 0, height_ = 0, mipCount_ = 0;
    bool layoutsInitialized_ = false;
    bool historyAvailable_ = false;
    glm::mat4 historyViewProjection_{1.0f};
};

} // namespace Iryven
