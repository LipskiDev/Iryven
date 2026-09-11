#pragma once
#include "../renderer.h"

namespace Iryven {

	using CpuClock = std::chrono::steady_clock;

	[[nodiscard]] float ElapsedMilliseconds(CpuClock::time_point start);

	class Renderer::OpaquePass final : public FrameGraphRenderPass {
	public:
		explicit OpaquePass(Renderer& renderer) : renderer_(renderer) {}

		void AddUI() override {}

		void PreRender(
			Velos::RHI::ICommandList& commands, const RenderScene& scene) override;

		void Render(
			Velos::RHI::ICommandList& commands, const RenderScene& scene) override;

		void OnResize(Velos::RHI::IDevice&, std::uint32_t, std::uint32_t) override;

	private:
		Renderer& renderer_;
		FrameData frameData_{};
		bool hasCamera_ = false;
	};
}