#pragma once

#include <array>
#include <cstddef>
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

	using namespace Velos::RHI;

    class ImGuiRenderer;

	struct RendererCpuTimings {
		FrameGraphCpuTimings frameGraph{};
		float uploadLightsMs = 0.0f;
		float uploadMaterialsMs = 0.0f;
		float uploadFrameDataMs = 0.0f;
	};

	constexpr uint32_t k_MaxLightSources = 512;
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
		void ToggleCullingCameraFreeze();
		void InitializeImGui();
		void ShutdownImGui();
		void BeginImGuiFrame();
		void DrawImGui();
		void EndFrame();

	private:
		bool cullingCameraFrozen_ = false;
		bool cullingCameraValid_ = false;
		FrameData cullingFrameData_{};

		struct alignas(16) GpuFrameData {
			glm::mat4 view;
			glm::mat4 projection;
			glm::mat4 viewProjection;
			glm::vec4 cameraPosition;
			glm::mat4 cullingView;
			glm::mat4 cullingProjection;
			glm::mat4 cullingViewProjection;
			glm::vec4 cullingCameraPosition;
		};
		static_assert(sizeof(GpuFrameData) == 416);
		static_assert(offsetof(GpuFrameData, cullingView) == 208);
		static_assert(offsetof(GpuFrameData, cullingCameraPosition) == 400);

		void DrawObject(
			Velos::RHI::ICommandList& commands,
			const RenderObject& object,
			const FrameData& frameData, std::uint32_t transmissionPhase = 0);
		void DrawText(Velos::RHI::ICommandList& commands,
			const RenderText& text);
		void DrawTransmissiveObjects(Velos::RHI::ICommandList& commands,
			const RenderScene& scene);
		void UploadLights(Velos::RHI::ICommandList& commands,
			const std::vector<RenderLight>& lights);
		void UploadFrameData(Velos::RHI::ICommandList& commands,
			const FrameData& frameData);
		void UploadMaterials(Velos::RHI::ICommandList& commands,
			const RenderScene& scene);
		[[nodiscard]] FrameData BuildFrameData(
			const RenderCamera& camera) const;
		void PrepareCloths(const RenderScene& scene);
		void SimulateCloths(Velos::RHI::ICommandList& commands,
			const RenderScene& scene);
		void DrawCloths(Velos::RHI::ICommandList& commands,
			const RenderScene& scene);

		void CreatePipelineResources();
		void DestroyPipelineResources();
		void CreateDepthResources(std::uint32_t width, std::uint32_t height);
		void DestroyDepthResources();
		void DestroyMeshResources();
		void CollectUnusedMeshes();
		void CollectRetiredModels(std::uint64_t completedSubmission);
		void CollectRetiredCloths(std::uint64_t completedSubmission);
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

			BufferHandle vertexStorageBuffer;
			BufferHandle meshletBuffer;
			BufferHandle meshletVertexIndexBuffer;
			BufferHandle meshletTriangleIndexBuffer;
			BindingPoolHandle meshletBindingPool;
			BindingSetHandle meshletBindingSet;
			uint32_t meshletCount = 0;
		};

		struct alignas(16) GpuMeshlet {
			uint32_t vertexOffset;
			uint32_t triangleOffset;
			uint32_t vertexCount;
			uint32_t triangleCount;

			// Bounding sphere
			glm::vec3 center{ 0.0f };
			float radius{ 0.0f };

			// Cone culling
			glm::vec3 coneApex{ 0.0f };
			float coneApexPadding{ 0.0f }; // std430 aligns the next vec3 to 16 bytes.
			glm::vec3 coneAxis{ 0.0f, 0.0f, 1.0f };
			float coneCutoff{ 1.0f };
		};
		static_assert(sizeof(GpuMeshlet) == 64);
		static_assert(offsetof(GpuMeshlet, center) == 16);
		static_assert(offsetof(GpuMeshlet, radius) == 28);
		static_assert(offsetof(GpuMeshlet, coneApex) == 32);
		static_assert(offsetof(GpuMeshlet, coneAxis) == 48);
		static_assert(offsetof(GpuMeshlet, coneCutoff) == 60);


		struct GpuModel {
			struct PrimitiveMeshlets {
				std::uint32_t firstIndex = 0;
				std::uint32_t indexCount = 0;
				std::int32_t vertexOffset = 0;
				std::uint32_t meshletOffset = 0;
				std::uint32_t meshletCount = 0;
			};

			std::weak_ptr<const Model> source;
			BufferHandle vertexBuffer;
			BufferHandle indexBuffer;
			BufferHandle vertexStorageBuffer;
			BufferHandle meshletBuffer;
			BufferHandle meshletVertexIndexBuffer;
			BufferHandle meshletTriangleIndexBuffer;
			BindingPoolHandle meshletBindingPool;
			BindingSetHandle meshletBindingSet;
			std::vector<PrimitiveMeshlets> primitiveMeshlets;
			std::vector<ImageHandle> textureImages;
			std::vector<ImageViewHandle> textureViews;
			std::vector<SamplerHandle> samplers;
			std::vector<BindlessTextureIndex> bindlessTextureIndices;
		};
		struct RetiredModel {
			GpuModel resources;
			std::uint64_t retirementSubmission = 0;
		};

		struct alignas(16) ClothParticle {
			glm::vec4 positionAndInverseMass{};
			glm::vec4 previousPosition{};
			glm::vec4 normal{};
		};
		static_assert(sizeof(ClothParticle) == 48);

		// assets/shaders/internal/cloth.comp contract:
		//   set 0, binding 0: readonly ClothParticle inputParticles[]
		//   set 0, binding 1: ClothParticle outputParticles[]
		//   local_size_x: 64
		// Integrate copies input to output and advances it. Constraint and normal
		// phases operate on output so the displayed input buffer is never written
		// while a previous graphics frame may still be reading it.
		enum class ClothSimulationPhase : std::uint32_t {
			Integrate = 0,
			SolveConstraints = 1,
			RecalculateNormals = 2,
		};

		struct alignas(16) ClothSimulationConstants {
			std::uint32_t resolutionX = 0;
			std::uint32_t resolutionY = 0;
			float clothWidth = 0.0f;
			float clothHeight = 0.0f;
			float deltaTime = 0.0f;
			float stiffness = 0.0f;
			float damping = 0.0f;
			float gravityScale = 0.0f;
			std::uint32_t phase = 0;
			std::uint32_t iteration = 0;
			std::uint32_t solverIterations = 0;
			std::uint32_t particleCount = 0;
			float time = 0.0f;
			glm::vec3 padding = {};
		};
		static_assert(sizeof(ClothSimulationConstants) == 64);

		struct GpuCloth {
			glm::uvec2 resolution{};
			glm::vec2 size{};
			float mass = 1.0f;
			bool pinTopLeft = true;
			bool pinTopRight = true;
			uint32_t particleCount = 0;
			uint32_t indexCount = 0;

			std::array<BufferHandle, 2> particleBuffers{};
			uint32_t currentState = 0;

			BufferHandle indexBuffer{};
			BindingPoolHandle bindingPool{};
			std::array<BindingSetHandle, 2> simulationBindings{};
			std::array<BindingSetHandle, 2> renderBindings{};
			std::uint64_t lastSeenGeneration = 0;
		};
		struct RetiredCloth {
			GpuCloth resources;
			std::uint64_t retirementSubmission = 0;
		};

		struct GpuFont {
			std::weak_ptr<const Font> source;

			ImageHandle atlasImage;
			ImageViewHandle atlasView;
			SamplerHandle atlasSampler;
			BindingSetHandle bindingSet;
		};

		struct UploadBackedBuffer {
			BufferHandle gpuBuffer;
			BufferHandle uploadBuffer;
			ResourceState state = ResourceState::Undefined;
		};

		[[nodiscard]] UploadBackedBuffer CreateUploadBackedBuffer(
			std::uint64_t size,
			BufferUsage usage,
			const char* gpuDebugName,
			const char* uploadDebugName);
		void DestroyUploadBackedBuffer(UploadBackedBuffer& buffer);
		void UploadBuffer(
			ICommandList& commands,
			UploadBackedBuffer& buffer,
			const void* data,
			std::uint64_t size,
			Velos::RHI::ResourceState finalState);

		[[nodiscard]] GpuMesh* ResolveOrCreateMesh(
			const std::shared_ptr<const MeshData>& mesh);
		[[nodiscard]] GpuModel* ResolveOrCreateModel(const ModelHandle& model);
		[[nodiscard]] GpuCloth* ResolveOrCreateCloth(const RenderCloth& cloth);
		[[nodiscard]] GpuFont* ResolveOrCreateFont(
			const std::shared_ptr<const Font>& font);
		void DestroyGpuModel(GpuModel& model);
		void DestroyGpuCloth(GpuCloth& cloth);

	private:
		class OpaquePass;
		class HiZPass;
		class ClothCompute;
		class ClothDraw;

		Window& window_;
		AssetUploadQueue& assetUploads_;

		std::unique_ptr<IDevice> device_;
        std::unique_ptr<ImGuiRenderer> imGui_;
		SwapchainHandle swapchain_;
		FrameBeginResult frame_;
		ImageHandle depthImage_;
		ImageViewHandle depthView_;
		FrameGraphBuilder frameGraphBuilder_;
		FrameGraph frameGraph_;
		RendererCpuTimings cpuTimings_{};
		std::unique_ptr<OpaquePass> opaquePass_;
		std::unique_ptr<HiZPass> hiZPass_;
		std::unique_ptr<ClothCompute> clothCompute_;
		std::unique_ptr<ClothDraw> clothDraw_;

		std::unordered_map<const MeshData*, GpuMesh> meshes_;
		std::unordered_map<const Font*, GpuFont> fonts_;
		std::unordered_map<const Model*, GpuModel> models_;
		std::unordered_map<uint64_t, GpuCloth> cloths_;
		std::vector<RetiredModel> retiredModels_;
		std::vector<RetiredCloth> retiredCloths_;
		std::uint64_t clothSceneGeneration_ = 0;
		ShaderHandle gltfVertexShader_;
		ShaderHandle gltfFragmentShader_;
		PipelineHandle gltfPipeline_;
		std::array<PipelineHandle, 2> gltfTransmissionPipelines_{};
		GeneratedPipelineLayout gltfGeneratedLayout_;
		ShaderHandle clothVertexShader_;
		ShaderHandle clothComputeShader_;
		PipelineHandle clothGraphicsPipeline_;
		PipelineHandle clothComputePipeline_;
		GeneratedPipelineLayout clothGraphicsGeneratedLayout_;
		GeneratedPipelineLayout clothComputeGeneratedLayout_;
		BindingLayoutHandle clothSimulationBindingLayout_;
		BindingLayoutHandle clothRenderBindingLayout_;
		ShaderHandle textVertexShader_;
		ShaderHandle textFragmentShader_;
		PipelineHandle textPipeline_;
		GeneratedPipelineLayout textGeneratedLayout_;
		BindingLayoutHandle fontBindingLayout_;
		BindingPoolHandle fontBindingPool_;
		std::array<std::vector<UploadBackedBuffer>, k_FramesInFlight>
			textVertexBuffers_;

		ShaderHandle meshletTaskShader_;
		ShaderHandle  meshletShader_;
		ShaderHandle meshletFragmentShader_;
		PipelineHandle meshletPipeline_;
		std::array<PipelineHandle, 2> meshletTransmissionPipelines_{};
		GeneratedPipelineLayout meshletGeneratedLayout_;
		BindingLayoutHandle meshletBindingLayout_;

		struct FrameLightingResource {
			UploadBackedBuffer lightBuffer;
			UploadBackedBuffer frameDataBuffer;
			UploadBackedBuffer materialBuffer;
			BindingSetHandle lightBindingSet;
		};

		std::array<FrameLightingResource, k_FramesInFlight> lightingFrames_;

		BindingLayoutHandle lightsBindingLayout_;
		BindingPoolHandle lightsBindingPool_;
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
