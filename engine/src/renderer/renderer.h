#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <iryven/math/color.h>
#include <iryven/rendering/frame_data.h>
#include <iryven/rendering/framegraph.h>
#include <iryven/rendering/render_scene.h>
#include <iryven/rendering/render_context.h>
#include <iryven/window.h>
#include <rhi/device.h>
#include <iryven/assets/font.h>
#include "bindless_texture_manager.h"
#include <iryven/renderer/asset_upload_queue.h>

namespace Iryven {
    class ImGuiRenderer;

	struct RendererCpuTimings {
		FrameGraphCpuTimings frameGraph{};
		float uploadLightsMs = 0.0f;
		float uploadMaterialsMs = 0.0f;
		float uploadFrameDataMs = 0.0f;
	};

	constexpr uint32_t k_MaxLightSources = 128;
	constexpr uint32_t k_FramesInFlight = 2;

	class Renderer final : public RenderContext {
	public:
		explicit Renderer(Window& window, AssetUploadQueue& assetUploads);
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		[[nodiscard]] bool BeginFrame();
		[[nodiscard]] float GetFrameFenceWaitMs() const noexcept {
			return frame_.frameFenceWaitMs;
		}
		[[nodiscard]] float GetAcquireImageMs() const noexcept {
			return frame_.acquireImageMs;
		}
		[[nodiscard]] const RendererCpuTimings& GetCpuTimings() const noexcept {
			return cpuTimings_;
		}
		void DrawScene(const RenderScene& renderScene) override;
        void InitializeImGui();
        void ShutdownImGui();
        void BeginImGuiFrame();
        void DrawImGui();
		void EndFrame();

	private:
		void DrawObject(
			Velos::RHI::ICommandList& commands,
			const RenderObject& object,
			const FrameData& frameData);
		void DrawText(Velos::RHI::ICommandList& commands,
			const RenderText& text);
		void UploadLights(Velos::RHI::ICommandList& commands,
			const std::vector<RenderLight>& lights);
		void UploadFrameData(Velos::RHI::ICommandList& commands,
			const FrameData& frameData);
		void UploadMaterials(Velos::RHI::ICommandList& commands,
			const std::vector<RenderObject>& objects);
		[[nodiscard]] FrameData BuildFrameData(
			const RenderCamera& camera) const;

		void CreatePipelineResources();
		void DestroyPipelineResources();
		void CreateDepthResources(std::uint32_t width, std::uint32_t height);
		void DestroyDepthResources();
		void DestroyMeshResources();
		void CollectUnusedMeshes();
		void CollectRetiredModels(std::uint64_t completedSubmission);
		void CollectUnusedFonts();
		void CreateBufferResources();
		void DestroyBufferResources();
		void DestroyFontResources();
		void CreateBindlessResources();
		void DestroyBindlessResources();
		void ProcessAssetUploads();

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
			std::vector<BindlessTextureIndex> bindlessTextureIndices;
		};
		struct RetiredModel {
			GpuModel resources;
			std::uint64_t retirementSubmission = 0;
		};

		struct GpuFont {
			std::weak_ptr<const Font> source;

			Velos::RHI::ImageHandle atlasImage;
			Velos::RHI::ImageViewHandle atlasView;
			Velos::RHI::SamplerHandle atlasSampler;
			Velos::RHI::BindingSetHandle bindingSet;
		};

		struct UploadBackedBuffer {
			Velos::RHI::BufferHandle gpuBuffer;
			Velos::RHI::BufferHandle uploadBuffer;
			Velos::RHI::ResourceState state = Velos::RHI::ResourceState::Undefined;
		};

		[[nodiscard]] UploadBackedBuffer CreateUploadBackedBuffer(
			std::uint64_t size,
			Velos::RHI::BufferUsage usage,
			const char* gpuDebugName,
			const char* uploadDebugName);
		void DestroyUploadBackedBuffer(UploadBackedBuffer& buffer);
		void UploadBuffer(
			Velos::RHI::ICommandList& commands,
			UploadBackedBuffer& buffer,
			const void* data,
			std::uint64_t size,
			Velos::RHI::ResourceState finalState);

		[[nodiscard]] GpuMesh* ResolveOrCreateMesh(
			const std::shared_ptr<const MeshData>& mesh);
		[[nodiscard]] GpuModel* ResolveOrCreateModel(const ModelHandle& model);
		[[nodiscard]] GpuFont* ResolveOrCreateFont(
			const std::shared_ptr<const Font>& font);
		void DestroyGpuModel(GpuModel& model);

	private:
		class OpaquePass;

		Window& window_;
		AssetUploadQueue& assetUploads_;

		std::unique_ptr<Velos::RHI::IDevice> device_;
        std::unique_ptr<ImGuiRenderer> imGui_;
		Velos::RHI::SwapchainHandle swapchain_;
		Velos::RHI::FrameBeginResult frame_;
		Velos::RHI::ImageHandle depthImage_;
		Velos::RHI::ImageViewHandle depthView_;
		FrameGraphBuilder frameGraphBuilder_;
		FrameGraph frameGraph_;
		RendererCpuTimings cpuTimings_{};
		std::unique_ptr<OpaquePass> opaquePass_;

		std::unordered_map<const MeshData*, GpuMesh> meshes_;
		std::unordered_map<const Font*, GpuFont> fonts_;
		std::unordered_map<const Model*, GpuModel> models_;
		std::vector<RetiredModel> retiredModels_;
		Velos::RHI::ShaderHandle gltfVertexShader_;
		Velos::RHI::ShaderHandle gltfFragmentShader_;
		Velos::RHI::PipelineHandle gltfPipeline_;
		Velos::RHI::GeneratedPipelineLayout gltfGeneratedLayout_;
		Velos::RHI::ShaderHandle textVertexShader_;
		Velos::RHI::ShaderHandle textFragmentShader_;
		Velos::RHI::PipelineHandle textPipeline_;
		Velos::RHI::GeneratedPipelineLayout textGeneratedLayout_;
		Velos::RHI::BindingLayoutHandle fontBindingLayout_;
		Velos::RHI::BindingPoolHandle fontBindingPool_;
		std::array<std::vector<UploadBackedBuffer>, k_FramesInFlight>
			textVertexBuffers_;

		struct FrameLightingResource {
			UploadBackedBuffer lightBuffer;
			UploadBackedBuffer frameDataBuffer;
			UploadBackedBuffer materialBuffer;
			Velos::RHI::BindingSetHandle lightBindingSet;
		};

		std::array<FrameLightingResource, k_FramesInFlight> lightingFrames_;

		Velos::RHI::BindingLayoutHandle lightsBindingLayout_;
		Velos::RHI::BindingPoolHandle lightsBindingPool_;
		struct MaterialSlotKey {
			const Model* model = nullptr;
			const Material* material = nullptr;
			bool operator==(const MaterialSlotKey&) const = default;
		};
		struct MaterialSlotKeyHash {
			std::size_t operator()(const MaterialSlotKey& key) const noexcept {
				const auto modelHash = std::hash<const Model*>{}(key.model);
				const auto materialHash = std::hash<const Material*>{}(key.material);
				return modelHash ^ (materialHash + 0x9e3779b9u +
					(modelHash << 6u) + (modelHash >> 2u));
			}
		};
		std::unordered_map<MaterialSlotKey, std::uint32_t, MaterialSlotKeyHash>
			materialSlots_;

		std::unique_ptr<BindlessTextureManager> bindlessTextureManager_;
		BindlessTextureIndex missingTextureIndex_ = 0;
		Velos::RHI::ImageHandle missingTextureImage_;
		Velos::RHI::ImageViewHandle missingTextureView_;
		Velos::RHI::SamplerHandle missingTextureSampler_;
		std::uint64_t nextSubmissionSerial_ = 1;
		std::uint64_t lastSubmittedSerial_ = 0;
		std::uint64_t completedSubmissionSerial_ = 0;
		std::array<std::uint64_t, k_FramesInFlight> frameSubmissionSerials_{};

		bool swapchainDirty_ = false;
		bool frameActive_ = false;
	};

}
