#include "opaque.h"
#include "../renderer.h"


namespace Iryven {
	using CpuClock = std::chrono::steady_clock;

	[[nodiscard]] float ElapsedMilliseconds(CpuClock::time_point start)
	{
		return std::chrono::duration<float, std::milli>(CpuClock::now() - start)
			.count();
	}

	void Renderer::OpaquePass::PreRender(
		Velos::RHI::ICommandList& commands, const RenderScene& scene)
	{
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

	void Renderer::OpaquePass::Render(
		Velos::RHI::ICommandList& commands, const RenderScene& scene)
	{
		if (hasCamera_) {
			for (const RenderObject& object : scene.objects) {
				renderer_.DrawObject(commands, object, frameData_);
			}
		}
		for (const RenderText& text : scene.texts) {
			renderer_.DrawText(commands, text);
		}
	}

	void Renderer::OpaquePass::OnResize(Velos::RHI::IDevice&, std::uint32_t, std::uint32_t) {}

}

