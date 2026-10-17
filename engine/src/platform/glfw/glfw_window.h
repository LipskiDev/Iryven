#pragma once

#include <iryven/window.h>
#include <string>

struct GLFWwindow;

namespace Iryven {
class GlfwWindow : public Window {
 public:
  GlfwWindow(int width, int height, const std::string &title, bool resizable,
             bool vsync);
  GlfwWindow(const GlfwWindow &) = delete;
  GlfwWindow &operator=(const GlfwWindow &) = delete;
  ~GlfwWindow() override;

  void PollEvents() override;
  void Show() override;
  bool IsFocused() const override;
  void SetCursorCaptured(bool captured) override;
  std::pair<float, float> GetCursorDelta() const override {
    return cursorDelta_;
  }
  bool ShouldClose() const override;

  int GetWidth() const override;
  int GetHeight() const override;

  int GetFramebufferWidth() const override;
  int GetFramebufferHeight() const override;

  bool WasFramebufferResized() const override;
  void ResetFramebufferResizedFlag() override;

  const std::string &GetTitle() const override;

  void *GetNativeHandle() const override;

  inline void SetEventCallback(const EventCallbackFn &callback) override {
    data_.eventCallback = callback;
  }
  void SetVSync(bool enabled) override;
  bool IsVSync() const override;

 private:
  int windowWidth_ = 0;
  int windowHeight_ = 0;

  int framebufferWidth_ = 0;
  int framebufferHeight_ = 0;

  bool framebufferResized_ = false;

 private:
  GLFWwindow *window_ = nullptr;
  std::pair<float, float> cursorDelta_{};

  struct WindowData {
    std::string title;
    uint32_t width = 0, height = 0;
    bool vsync = false;

    EventCallbackFn eventCallback;
  };

  WindowData data_;

  std::string title_;
};
}  // namespace Iryven
