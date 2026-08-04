#pragma once

#include <iryven/window.h>
#include <string>

struct GLFWwindow;

namespace Iryven {
	class GlfwWindow : public Window {
	public:
		GlfwWindow(int width, int height, const std::string& title, bool resizable);
		~GlfwWindow() override;

		void PollEvents() override;
		bool ShouldClose() const override;

		int GetWidth() const override;
		int GetHeight() const override;

		int GetFramebufferWidth() const override;
		int GetFramebufferHeight() const override;

		bool WasFramebufferResized() const override;
		void ResetFramebufferResizedFlag() override;

		const std::string &GetTitle() const override;

		void* GetNativeHandle() const override;

	private:
		int windowWidth_ = 0;
		int windowHeight_ = 0;

		int framebufferWidth_ = 0;
		int framebufferHeight_ = 0;

		bool framebufferResized_ = false;

	private:
		GLFWwindow* window_ = nullptr;

		std::string title_;
	};
}
