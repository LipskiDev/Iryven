#pragma once
#include <memory>

#include <iryven/window.h>

#include <rhi/device.h>
#include <iryven/math/color.h>

namespace Iryven {
	class Renderer {
	public:
		explicit Renderer(Window& window);
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		bool BeginFrame();
		void Clear(const Color& color);
		void EndFrame();

	private:
		Window& window_;

		std::unique_ptr<Velos::RHI::IDevice> device_;
		Velos::RHI::SwapchainHandle swapchain_;
		Velos::RHI::FrameBeginResult frame_;

		bool swapchainDirty_ = false;
		bool frameActive_ = false;

	};
}
