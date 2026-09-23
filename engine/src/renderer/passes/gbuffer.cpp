#include "gbuffer.h"
#include "../renderer.h"
#include "hi_z.h"
#include <algorithm>


namespace Iryven {
	using CpuClock = std::chrono::steady_clock;

	[[nodiscard]] float ElapsedMilliseconds(CpuClock::time_point start)
	{
		return std::chrono::duration<float, std::milli>(CpuClock::now() - start)
			.count();
	}

	void Renderer::GBufferPass::PreRender(
		Velos::RHI::ICommandList& commands, const RenderScene& scene)
	{
		renderer_.hiZPass_->PrepareForSampling(commands);
		const auto lightsStart = CpuClock::now();
		renderer_.UploadLights(commands, scene.lights);
		renderer_.cpuTimings_.uploadLightsMs += ElapsedMilliseconds(lightsStart);

		const auto materialsStart = CpuClock::now();
		renderer_.UploadMaterials(commands, scene);
		renderer_.cpuTimings_.uploadMaterialsMs +=
			ElapsedMilliseconds(materialsStart);
		hasCamera_ = scene.camera.has_value();
		if (hasCamera_) {
			frameData_ = renderer_.BuildFrameData(*scene.camera);
			const auto frameDataStart = CpuClock::now();
			renderer_.UploadFrameData(commands, frameData_);
			renderer_.cpuTimings_.uploadFrameDataMs +=
				ElapsedMilliseconds(frameDataStart);
		}
	}

	void Renderer::GBufferPass::Render(
		Velos::RHI::ICommandList& commands, const RenderScene& scene)
	{
		if (!hasCamera_) return;
		for (const RenderObject& object : scene.objects) {
			if (!object.material || object.material->transmission <= 0.0f)
				renderer_.DrawObject(commands, object, frameData_);
		}
		renderer_.DrawCloths(commands, scene);
	}

	void Renderer::DrawTransmissiveObjects(
		Velos::RHI::ICommandList& commands, const RenderScene& scene)
	{
		if (scene.camera) {
			const FrameData frameData = BuildFrameData(*scene.camera);
			struct TransmissionDraw { const RenderObject* object; float depth; };
			std::vector<TransmissionDraw> transmissionDraws;
			for (const RenderObject& object : scene.objects) {
				if (!object.material || object.material->transmission <= 0.0f) {
					continue;
				}
				glm::vec3 center(0.0f);
				if (object.model) {
					for (const auto& mesh : object.model->meshes)
						for (const auto& primitive : mesh.primitives)
							if (primitive.firstIndex == object.firstIndex &&
								primitive.vertexOffset == object.vertexOffset &&
								primitive.indexCount == object.indexCount)
								center = primitive.boundingSphere.center;
				} else if (object.mesh && !object.mesh->vertices.empty()) {
					glm::vec3 minimum = object.mesh->vertices.front().position;
					glm::vec3 maximum = minimum;
					for (const auto& vertex : object.mesh->vertices) {
						minimum = glm::min(minimum, vertex.position);
						maximum = glm::max(maximum, vertex.position);
					}
					center = (minimum + maximum) * 0.5f;
				}
				const float depth = (frameData.view * object.transform * glm::vec4(center, 1.0f)).z;
				transmissionDraws.push_back({ &object, depth });
			}
			std::stable_sort(transmissionDraws.begin(), transmissionDraws.end(),
				[](const auto& a, const auto& b) { return a.depth < b.depth; });
			for (const auto& draw : transmissionDraws) {
				DrawObject(commands, *draw.object, frameData, 1);
				DrawObject(commands, *draw.object, frameData, 2);
			}
		}
	}

	void Renderer::GBufferPass::OnResize(Velos::RHI::IDevice&, std::uint32_t, std::uint32_t) {}

}

