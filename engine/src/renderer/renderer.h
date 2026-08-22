#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <iryven/math/color.h>
#include <iryven/rendering/frame_data.h>
#include <iryven/rendering/render_scene.h>
#include <iryven/rendering/render_context.h>
#include <iryven/window.h>
#include <rhi/device.h>
#include <iryven/assets/font.h>

namespace Iryven {

	constexpr uint32_t k_MaxLightSources = 128;
	constexpr uint32_t k_FramesInFlight = 2;

	class Renderer final : public RenderContext {
	public:
		explicit Renderer(Window& window);
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		[[nodiscard]] bool BeginFrame();
		void DrawScene(const RenderScene& renderScene) override;
		void EndFrame();

	private:
		void BeginScenePass();
		void DrawObject(
			const RenderObject& object,
			const FrameData& frameData);
		void DrawText(const RenderText& text);
		void UploadLights(const std::vector<RenderLight>& lights);
		void UploadFrameData(const FrameData& frameData);
		void UploadMaterials(const std::vector<RenderObject>& objects);
		[[nodiscard]] FrameData BuildFrameData(
			const RenderCamera& camera) const;

		void CreatePipelineResources();
		void DestroyPipelineResources();
		void CreateDepthResources(std::uint32_t width, std::uint32_t height);
		void DestroyDepthResources();
		void DestroyMeshResources();
		void CollectUnusedMeshes();
		void CollectUnusedFonts();
		void CreateBufferResources();
		void DestroyBufferResources();
		void DestroyFontResources();

		struct GpuMesh {
			std::weak_ptr<const MeshData> source;
			Velos::RHI::BufferHandle vertexBuffer;
			Velos::RHI::BufferHandle indexBuffer;
			std::uint32_t indexCount = 0;
		};
		struct GpuModel {
			std::weak_ptr<const Model> source;
			Velos::RHI::BufferHandle vertexBuffer;
			Velos::RHI::BufferHandle indexBuffer;
			std::vector<Velos::RHI::ImageHandle> textureImages;
			std::vector<Velos::RHI::ImageViewHandle> textureViews;
			std::vector<Velos::RHI::SamplerHandle> samplers;
			std::unordered_map<const Material*, Velos::RHI::BindingSetHandle>
				materialBindingSets;
		};

		struct GpuFont {
			std::weak_ptr<const Font> source;

			Velos::RHI::ImageHandle atlasImage;
			Velos::RHI::ImageViewHandle atlasView;
			Velos::RHI::SamplerHandle atlasSampler;
			Velos::RHI::BindingSetHandle bindingSet;
		};

		[[nodiscard]] GpuMesh* ResolveOrCreateMesh(
			const std::shared_ptr<const MeshData>& mesh);
		[[nodiscard]] GpuModel* ResolveOrCreateModel(const ModelHandle& model);
		[[nodiscard]] GpuFont* ResolveOrCreateFont(
			const std::shared_ptr<const Font>& font);

	private:
		Window& window_;

		std::unique_ptr<Velos::RHI::IDevice> device_;
		Velos::RHI::SwapchainHandle swapchain_;
		Velos::RHI::FrameBeginResult frame_;
		Velos::RHI::ImageHandle depthImage_;
		Velos::RHI::ImageViewHandle depthView_;

		std::unordered_map<const MeshData*, GpuMesh> meshes_;
		std::unordered_map<const Font*, GpuFont> fonts_;
		std::unordered_map<const Model*, GpuModel> models_;
		Velos::RHI::ShaderHandle gltfVertexShader_;
		Velos::RHI::ShaderHandle gltfFragmentShader_;
		Velos::RHI::PipelineHandle gltfPipeline_;
		Velos::RHI::ShaderHandle textVertexShader_;
		Velos::RHI::ShaderHandle textFragmentShader_;
		Velos::RHI::PipelineHandle textPipeline_;
		Velos::RHI::BindingLayoutHandle fontBindingLayout_;
		Velos::RHI::BindingPoolHandle fontBindingPool_;
		Velos::RHI::BindingLayoutHandle gltfMaterialBindingLayout_;
		Velos::RHI::BindingPoolHandle gltfMaterialBindingPool_;
		Velos::RHI::ImageHandle defaultBaseColorImage_;
		Velos::RHI::ImageViewHandle defaultBaseColorView_;
		Velos::RHI::ImageHandle defaultMetallicRoughnessImage_;
		Velos::RHI::ImageViewHandle defaultMetallicRoughnessView_;
		Velos::RHI::SamplerHandle defaultMaterialSampler_;
		Velos::RHI::BindingSetHandle defaultMaterialBindingSet_;
		std::array<std::vector<Velos::RHI::BufferHandle>, k_FramesInFlight>
			textVertexBuffers_;

		struct FrameLightingResource {
			Velos::RHI::BufferHandle lightBuffer;
			Velos::RHI::BufferHandle frameDataBuffer;
			Velos::RHI::BufferHandle materialBuffer;
			Velos::RHI::BindingSetHandle lightBindingSet;
		};

		std::array<FrameLightingResource, k_FramesInFlight> lightingFrames_;

		Velos::RHI::BindingLayoutHandle lightsBindingLayout_;
		Velos::RHI::BindingPoolHandle lightsBindingPool_;
		std::unordered_map<const Material*, std::uint32_t> materialSlots_;

		bool swapchainDirty_ = false;
		bool frameActive_ = false;
		bool scenePassActive_ = false;
	};

}
