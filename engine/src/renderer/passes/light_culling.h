#pragma once

#include "../renderer.h"

namespace Iryven {

// Owns per-view light preparation and tile/depth-bin culling.
// Uploads directional lights followed by depth-sorted local lights.
// Produces conservative tile bitfields for deferred and forward lighting.
class Renderer::LightCullingPass final : public FrameGraphRenderPass {
public:
    explicit LightCullingPass(Renderer& renderer);
    ~LightCullingPass() override;

    void AddUI() override {}
    void PreRender(Velos::RHI::ICommandList&, const RenderScene&) override;
    void Render(Velos::RHI::ICommandList&, const RenderScene&) override;
    void OnResize(Velos::RHI::IDevice&, std::uint32_t width, std::uint32_t height) override;
    [[nodiscard]] FrameGraphBufferInfo TileBufferInfo() const;
    [[nodiscard]] FrameGraphBufferInfo DepthBinBufferInfo() const;
	[[nodiscard]] std::uint32_t TileCount() const noexcept { return tilesX_ * tilesY_; }
	[[nodiscard]] static constexpr std::uint32_t WordsPerTile() noexcept {
		return kWordsPerTile;
	}
	[[nodiscard]] BufferHandle TileBuffer(std::uint32_t frame) const {
		return tileBuffers_.at(frame);
	}
	[[nodiscard]] std::size_t TileBufferSize() const noexcept { return tileBufferSize_; }

private:
    static constexpr std::uint32_t kTileSize = 8;
    static constexpr std::uint32_t kWordsPerTile = (k_MaxLightSources + 31) / 32;
    static_assert(kWordsPerTile == 16);
    struct alignas(16) CullingData {
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        glm::uvec4 viewport{0u};
    };
    static_assert(sizeof(CullingData) == 144);

    void CreateResources(std::uint32_t width, std::uint32_t height);
    void DestroyResources();
    void Destroy();

    struct LocalLight {
        std::size_t sceneIndex;
        float depth;
    };
	struct ShadowCandidate {
		std::size_t sceneIndex = 0;
		std::uint64_t stableId = 0;
		std::uint32_t packedLightIndex = 0;
		float projectedInfluence = 0.0f;
		std::uint32_t requestedResolution = 256;
		std::uint32_t slot = 0;
	};

    Renderer& renderer_;
    ShaderHandle shader_;
    PipelineHandle pipeline_;
    BindingLayoutHandle layout_;
    BindingPoolHandle pool_;
    std::array<BindingSetHandle, k_FramesInFlight> sets_{};
    std::array<BufferHandle, k_FramesInFlight> tileBuffers_{};
	std::array<BufferHandle, k_FramesInFlight> shadowResolutionBuffers_{};
	std::array<BufferHandle, k_FramesInFlight> shadowResolutionResetUploads_{};
	std::array<BufferHandle, k_FramesInFlight> shadowResolutionReadbacks_{};
	std::array<std::vector<std::uint64_t>, k_FramesInFlight> resolutionSceneIndices_{};
	std::array<bool, k_FramesInFlight> resolutionReadbackReady_{};
    std::array<UploadBackedBuffer, k_FramesInFlight> cullingBuffers_{};
    std::array<UploadBackedBuffer, k_FramesInFlight> depthBinBuffers_{};
    std::uint32_t width_ = 0, height_ = 0, tilesX_ = 0, tilesY_ = 0;
    std::size_t tileBufferSize_ = 0;
    // Reused each frame; scene.lights retains its original ordering.
    std::vector<RenderLight> packedLights_;
    std::vector<LocalLight> localLights_;
	std::vector<ShadowCandidate> shadowCandidates_;
	std::unordered_map<std::uint64_t, std::uint32_t> measuredShadowResolutions_;
    std::uint32_t directionalLightCount_ = 0;
};

} // namespace Iryven
