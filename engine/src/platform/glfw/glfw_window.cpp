#include "glfw_window.h"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <iryven/log.h>
#include <iryven/events/application_event.h>
#include <iryven/events/keyboard_event.h>
#include <iryven/events/mouse_event.h>
#include "glfw_input.h"

namespace Iryven {

	static void GLFWErrorCallback(int error, const char* description) {
		IRYVEN_CORE_ERROR("GLFW ERROR ({0}): {1}", error, description);
	}

	GlfwWindow::GlfwWindow(int width, int height, const std::string& title, bool resizable)
		: windowWidth_(width), windowHeight_(height), title_(title)
	{
		if (!glfwInit()) {
			throw std::runtime_error("Failed to initialize GLFW");
		}

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);
		glfwSetErrorCallback(GLFWErrorCallback);

		window_ = glfwCreateWindow(windowWidth_, windowHeight_, title_.c_str(),
			nullptr, nullptr);

		if (!window_) {
			const char* description = nullptr;
			glfwGetError(&description);
			throw std::runtime_error(description ? description : "Failed to create GLFW window");
		}
		glfwSetWindowUserPointer(window_, this);

		glfwGetWindowSize(window_, &windowWidth_, &windowHeight_);
		glfwGetFramebufferSize(window_, &framebufferWidth_, &framebufferHeight_);

		glfwSetWindowSizeCallback(window_, [](GLFWwindow* window, int wwidth, int wheight)
			{
				auto& self = *static_cast<GlfwWindow*>(
					glfwGetWindowUserPointer(window)
					);

				WindowData& data = self.data_;
				data.width = wwidth;
				data.height = wheight;

				WindowResizeEvent event(wwidth, wheight);
				data.eventCallback(event);
			});

		glfwSetWindowCloseCallback(window_, [](GLFWwindow* window)
			{
				auto& self = *static_cast<GlfwWindow*>(
					glfwGetWindowUserPointer(window)
					);

				WindowData& data = self.data_;
				WindowCloseEvent event;
				data.eventCallback(event);
			});

		glfwSetKeyCallback(window_, [](GLFWwindow* window, int key, int scancode, int action, int mods)
			{
				auto& self = *static_cast<GlfwWindow*>(
					glfwGetWindowUserPointer(window)
					);

				WindowData& data = self.data_;

				switch (action)
				{
				case GLFW_PRESS:
				{
					KeyPressedEvent event(TranslateKey(key), 0);
					data.eventCallback(event);
					break;
				}
				case GLFW_RELEASE:
				{
					KeyReleasedEvent event(TranslateKey(key));
					data.eventCallback(event);
					break;
				}
				case GLFW_REPEAT:
				{
					KeyPressedEvent event(TranslateKey(key), 1);
					data.eventCallback(event);
					break;
				}
				}
			});

		glfwSetMouseButtonCallback(window_, [](GLFWwindow* window, int button, int action, int mods)
			{
				auto& self = *static_cast<GlfwWindow*>(
					glfwGetWindowUserPointer(window)
					);

				WindowData& data = self.data_;

				switch (action)
				{
				case GLFW_PRESS:
				{
					MouseButtonPressedEvent event(TranslateMouseButton(button));
					data.eventCallback(event);
					break;
				}
				case GLFW_RELEASE:
				{
					MouseButtonReleasedEvent event(TranslateMouseButton(button));
					data.eventCallback(event);
					break;
				}
				}
			});

		glfwSetScrollCallback(window_, [](GLFWwindow* window, double xOffset, double yOffset)
			{
				auto& self = *static_cast<GlfwWindow*>(
					glfwGetWindowUserPointer(window)
					);

				WindowData& data = self.data_;

				MouseScrolledEvent event((float)xOffset, (float)yOffset);
				data.eventCallback(event);
			});

		glfwSetCursorPosCallback(window_, [](GLFWwindow* window, double xPos, double yPos)
			{
				auto& self = *static_cast<GlfwWindow*>(
					glfwGetWindowUserPointer(window)
					);

				WindowData& data = self.data_;

				MouseMovedEvent event((float)xPos, (float)yPos);
				data.eventCallback(event);
			});


	}

	GlfwWindow::~GlfwWindow()
	{
		if (window_) {
			glfwDestroyWindow(window_);
			window_ = nullptr;
		}

		glfwTerminate();
	}

	void GlfwWindow::PollEvents()
	{
		glfwPollEvents();
	}

	bool GlfwWindow::ShouldClose() const
	{
		return glfwWindowShouldClose(window_);
	}

	int GlfwWindow::GetWidth() const
	{
		int width, height;
		glfwGetWindowSize(window_, &width, &height);
		return width;
	}

	int GlfwWindow::GetHeight() const
	{
		int width, height;
		glfwGetWindowSize(window_, &width, &height);
		return height;
	}

	int GlfwWindow::GetFramebufferWidth() const
	{
		int width, height;
		glfwGetFramebufferSize(window_, &width, &height);
		return width;
	}

	int GlfwWindow::GetFramebufferHeight() const
	{
		int width, height;
		glfwGetFramebufferSize(window_, &width, &height);
		return height;
	}

	bool GlfwWindow::WasFramebufferResized() const
	{
		return framebufferResized_;
	}

	void GlfwWindow::ResetFramebufferResizedFlag()
	{
		framebufferResized_ = false;
	}

	const std::string& GlfwWindow::GetTitle() const
	{
		return title_;
	}

	void* GlfwWindow::GetNativeHandle() const
	{
		return window_;
	}

	void GlfwWindow::SetVSync(bool enabled)
	{
		data_.vsync = enabled;
	}

	bool GlfwWindow::IsVSync() const
	{
		return data_.vsync;
	}
}
