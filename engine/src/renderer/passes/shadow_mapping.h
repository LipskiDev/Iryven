#pragma once

#include "../renderer.h"

namespace Iryven {

// Chapter 8 shadow pipeline scaffold:
//   1. Cull render instances against each shadow-casting point light.
//   2. Generate six indirect mesh-task commands per non-empty light.
//   3. Cull meshlets per cubemap face and render layered depth.
//   4. Expose the cubemap array to the lighting passes.
//
// A single sparse cube-array backs all lights. Per-light resolution demand is
// represented by image mips, and only pages selected by the allocation plan
// are resident.
class Renderer::ShadowMappingPass final : public FrameGraphRenderPass {
public:
    static constexpr std::uint32_t kFacesPerLight = 6;
    static constexpr std::uint32_t kMaxShadowLights = 128;
	static constexpr std::uint32_t kMaxShadowObjects = 4096;

    explicit ShadowMappingPass(Renderer& renderer);
    ~ShadowMappingPass() override;

    void AddUI() override {}
    void PreRender(ICommandList& commands, const RenderScene& scene) override;
    void Render(ICommandList& commands, const RenderScene& scene) override;
    void OnResize(IDevice&, std::uint32_t, std::uint32_t) override;

    [[nodiscard]] FrameGraphTextureInfo ShadowMapInfo() const;
    [[nodiscard]] ImageViewHandle ShadowSamplingView() const noexcept {
        return shadowCubeView_;
    }
    [[nodiscard]] SamplerHandle ShadowSampler() const noexcept {
        return shadowSampler_;
    }
    [[nodiscard]] FrameGraphBufferInfo IndirectCommandsInfo() const;

private:
    struct alignas(16) ShadowFrameData {
        std::array<glm::mat4, kMaxShadowLights * kFacesPerLight> viewProjections{};
        glm::uvec4 counts{}; // x=lights, y=instances, z=max candidates/light.
    };

    // The first three fields match VkDrawMeshTasksIndirectCommandEXT.
    struct alignas(16) ShadowDrawCommand {
        std::uint32_t groupCountX = 0;
        std::uint32_t groupCountY = 1;
        std::uint32_t groupCountZ = 1;
        std::uint32_t lightAndFace = 0;
    };
	struct alignas(16) ShadowCullData { glm::uvec4 counts{}; };
	struct alignas(16) ShadowCullLight {
		glm::vec4 positionAndRange{};
		glm::uvec4 info{};
	};
	struct alignas(16) ShadowCullObject {
		glm::vec4 centerAndRadius{};
		glm::uvec4 info{};
	};
	struct ShadowRenderItem {
		glm::mat4 model{1.0f};
		BindingSetHandle meshSet{};
		std::uint32_t meshletOffset = 0;
		std::uint32_t meshletCount = 0;
	};

    void CreateResources();
    void DestroyResources();
    void Destroy();

    Renderer& renderer_;
    std::uint32_t activeLightCount_ = 0;
	std::uint32_t dirtyLightCount_ = 0;

    ImageHandle shadowImage_{};
    ImageViewHandle shadowCubeView_{};
    std::array<ImageViewHandle, 4> shadowMipDepthViews_{};
	std::array<std::array<ImageViewHandle, kMaxShadowLights>, 4>
		shadowSlotDepthViews_{};
    SamplerHandle shadowSampler_{};
	std::array<BufferHandle, k_FramesInFlight> indirectCommands_{};
	std::array<bool, k_FramesInFlight> indirectCommandsInitialized_{};
    std::array<UploadBackedBuffer, k_FramesInFlight> frameData_{};
	std::array<UploadBackedBuffer, k_FramesInFlight> cullData_{};
	std::array<UploadBackedBuffer, k_FramesInFlight> cullLights_{};
	std::array<UploadBackedBuffer, k_FramesInFlight> cullObjects_{};

    BindingLayoutHandle shadowLayout_{};
    BindingPoolHandle pool_{};
    std::array<BindingSetHandle, k_FramesInFlight> shadowSets_{};
	BindingLayoutHandle cullLayout_{};
	BindingPoolHandle cullPool_{};
	std::array<BindingSetHandle, k_FramesInFlight> cullSets_{};

    ShadowFrameData shadowFrameData_{};
	std::vector<ShadowRenderItem> renderItems_;
	std::vector<bool> dirtyAssignments_;
	std::vector<std::uint64_t> pendingSignatures_;
	std::array<std::uint64_t, kMaxShadowLights> cachedSignatures_{};
	std::array<bool, kMaxShadowLights> cachedSlots_{};

    ShaderHandle instanceCullShader_{};
    ShaderHandle meshShader_{};
    ShaderHandle fragmentShader_{};
    PipelineHandle instanceCullPipeline_{};
    PipelineHandle shadowPipeline_{};
};

} // namespace Iryven
