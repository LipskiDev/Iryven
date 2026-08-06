#pragma once

#include <string>
#include <memory>
#include <functional>
#include <iryven/events/event.h>

namespace Iryven {

	struct NativeWindowHandle {
		void* handle = nullptr;
	};

	struct WindowProperties
	{
		std::string title;
		unsigned int width;
		unsigned int height;

		WindowProperties(const std::string& title = "Iryven Engine",
			uint32_t width = 1920,
			uint32_t height = 1080)
			: title(title), width(width), height(height)
		{
		}
	};

	class Window {
	public:
		using EventCallbackFn = std::function<void(Event&)>;
		virtual ~Window() = default;
		virtual void PollEvents() = 0;
		virtual bool ShouldClose() const = 0;

		virtual int GetHeight() const = 0;
		virtual int GetWidth() const = 0;

		virtual int GetFramebufferHeight() const = 0;
		virtual int GetFramebufferWidth() const = 0;

		virtual bool WasFramebufferResized() const = 0;
		virtual void ResetFramebufferResizedFlag() = 0;

		virtual const std::string& GetTitle() const = 0;

		virtual void* GetNativeHandle() const = 0;

		virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;
	};

	std::unique_ptr<Window> CreateWindow(const WindowProperties& properties = WindowProperties());

} // namespace Iryven
