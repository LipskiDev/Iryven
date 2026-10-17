#pragma once

#include <string>
#include <memory>
#include <functional>
#include <utility>
#include <iryven/events/event.h>
#include <rhi/device.h>
#include <iryven/graphics_backend.h>

namespace Iryven {

struct NativeWindowHandle {
  void *handle = nullptr;
};

struct WindowProperties {
  std::string title;
  unsigned int width;
  unsigned int height;
  bool vsync = true;
  Velos::RHI::GraphicsAPI graphicsAPI = GetBuildGraphicsBackend();

  WindowProperties(
      const std::string &title = "Iryven Engine", uint32_t width = 1920,
      uint32_t height = 1080, bool vsync = true,
      Velos::RHI::GraphicsAPI graphicsAPI = GetBuildGraphicsBackend())
      : title(title),
        width(width),
        height(height),
        vsync(vsync),
        graphicsAPI(graphicsAPI) {}
};

class Window {
 public:
  using EventCallbackFn = std::function<void(Event &)>;
  virtual ~Window() = default;
  virtual void PollEvents() = 0;
  virtual void Show() = 0;
  virtual bool IsFocused() const = 0;
  virtual void SetCursorCaptured(bool captured) = 0;
  virtual std::pair<float, float> GetCursorDelta() const = 0;
  virtual bool ShouldClose() const = 0;

  virtual int GetHeight() const = 0;
  virtual int GetWidth() const = 0;

  virtual int GetFramebufferHeight() const = 0;
  virtual int GetFramebufferWidth() const = 0;

  virtual bool WasFramebufferResized() const = 0;
  virtual void ResetFramebufferResizedFlag() = 0;

  virtual const std::string &GetTitle() const = 0;

  virtual void *GetNativeHandle() const = 0;
  virtual Velos::RHI::WindowHandleType GetNativeHandleType() const {
    return Velos::RHI::WindowHandleType::GLFW;
  }

  virtual void SetEventCallback(const EventCallbackFn &callback) = 0;
  virtual void SetVSync(bool enabled) = 0;
  virtual bool IsVSync() const = 0;
};

std::unique_ptr<Window> CreateWindow(
    const WindowProperties &properties = WindowProperties());

}  // namespace Iryven
