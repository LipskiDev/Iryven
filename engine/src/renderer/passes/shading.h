#pragma once
#include "../renderer.h"

namespace Iryven {

inline constexpr std::array<const char*, 4> GBufferNames{
    "gBaseMetallic", "gNormalRoughness", "gEmissiveOcclusion", "gSpecular"};
inline constexpr std::array<Format, 4> GBufferFormats{
    Format::RGBA8_UNORM, Format::RGBA16_FLOAT, Format::RGBA16_FLOAT, Format::RGBA8_UNORM};

inline std::vector<AttachmentDesc> GBufferAttachments()
{
    std::vector<AttachmentDesc> result;
    for (auto format : GBufferFormats) result.push_back({.format = format});
    return result;
}

class Renderer::ShadingPass final : public FrameGraphRenderPass {
public:
    explicit ShadingPass(Renderer& renderer);
    ~ShadingPass() override;
    void AddUI() override {}
    void PreRender(ICommandList&, const RenderScene&) override;
    void Render(ICommandList&, const RenderScene&) override;
    void OnResize(IDevice&, std::uint32_t, std::uint32_t) override {}
private:
    void Destroy();
    Renderer& renderer_;
    ShaderHandle vertex_, fragment_;
    PipelineHandle pipeline_;
    BindingLayoutHandle textureLayout_;
    BindingPoolHandle pool_;
    SamplerHandle sampler_;
    std::array<BindingSetHandle, k_FramesInFlight> sets_{};
};
}
