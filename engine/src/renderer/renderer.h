#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

#include <iryven/math/color.h>
#include <iryven/rendering/render_scene.h>
#include <iryven/window.h>
#include <rhi/device.h>

namespace Iryven {

	class Renderer {
	public:
		explicit Renderer(Window& window);
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		void RenderFrame(const RenderScene& renderScene);

	private:
		bool BeginFrame();
		void BeginScenePass();
		void DrawObject(
			const RenderObject& object,
			const glm::mat4& viewProjection);
		void EndFrame();
		[[nodiscard]] glm::mat4 BuildViewProjection(
			const RenderCamera& camera) const;

		void CreatePipelineResources();
		void DestroyPipelineResources();
		void CreateDepthResources(std::uint32_t width, std::uint32_t height);
		void DestroyDepthResources();
		void DestroyMeshResources();
		void CollectUnusedMeshes();

		struct GpuMesh {
			std::weak_ptr<const MeshData> source;
			Velos::RHI::BufferHandle vertexBuffer;
			Velos::RHI::BufferHandle indexBuffer;
			std::uint32_t indexCount = 0;
		};

		[[nodiscard]] GpuMesh* ResolveOrCreateMesh(
			const std::shared_ptr<const MeshData>& mesh);

	private:
		Window& window_;

		std::unique_ptr<Velos::RHI::IDevice> device_;
		Velos::RHI::SwapchainHandle swapchain_;
		Velos::RHI::FrameBeginResult frame_;
		Velos::RHI::ImageHandle depthImage_;
		Velos::RHI::ImageViewHandle depthView_;

		std::unordered_map<const MeshData*, GpuMesh> meshes_;
		Velos::RHI::ShaderHandle vertexColorVertexShader_;
		Velos::RHI::ShaderHandle vertexColorFragmentShader_;
		Velos::RHI::PipelineHandle vertexColorPipeline_;

		bool swapchainDirty_ = false;
		bool frameActive_ = false;
		bool scenePassActive_ = false;
	};

}
