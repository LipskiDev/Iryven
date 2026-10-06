#include "gbuffer.h"
#include "../renderer.h"
#include "hi_z.h"
#include <algorithm>
#include <unordered_map>


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
		struct BatchKey {
			const MeshData* mesh = nullptr;
			const Model* model = nullptr;
			const Material* material = nullptr;
			std::uint32_t firstIndex = 0;
			std::uint32_t indexCount = 0;
			std::int32_t vertexOffset = 0;
			bool operator==(const BatchKey&) const = default;
		};
		struct BatchKeyHash {
			std::size_t operator()(const BatchKey& key) const noexcept {
				std::size_t hash = std::hash<const void*>{}(key.mesh);
				const auto combine = [&hash](std::size_t value) {
					hash ^= value + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
				};
				combine(std::hash<const void*>{}(key.model));
				combine(std::hash<const void*>{}(key.material));
				combine(key.firstIndex);
				combine(key.indexCount);
				combine(static_cast<std::uint32_t>(key.vertexOffset));
				return hash;
			}
		};

		std::unordered_map<BatchKey, std::vector<const RenderObject*>, BatchKeyHash>
			batches;
		batches.reserve(scene.objects.size());
		for (const RenderObject& object : scene.objects) {
			if (object.material && object.material->transmission > 0.0f) continue;
			batches[{
				.mesh = object.mesh.get(),
				.model = object.model.get(),
				.material = object.material.get(),
				.firstIndex = object.firstIndex,
				.indexCount = object.indexCount,
				.vertexOffset = object.vertexOffset,
			}].push_back(&object);
		}
		for (const auto& [key, objects] : batches) {
			if (objects.size() > 1) renderer_.DrawObjectBatch(commands, objects);
			else renderer_.DrawObject(commands, *objects.front(), frameData_);
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
