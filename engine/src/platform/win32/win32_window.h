#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#ifdef CreateWindow
#undef CreateWindow
#endif
#ifdef CreateSemaphore
#undef CreateSemaphore
#endif
#include <iryven/window.h>
#include <iryven/input/key.h>
#include <iryven/input/mouse_button.h>
#include <bitset>
#include <exception>
#include <utility>

namespace Iryven {
class Win32Window final : public Window {
 public:
  Win32Window(int width, int height, const std::string &title, bool resizable,
              bool vsync);
  ~Win32Window() override;
  Win32Window(const Win32Window &) = delete;
  Win32Window &operator=(const Win32Window &) = delete;

  void PollEvents() override;
  void Show() override;
  bool IsFocused() const override;
  void SetCursorCaptured(bool captured) override;
  std::pair<float, float> GetCursorDelta() const override {
    return cursorDelta_;
  }
  bool ShouldClose() const override { return shouldClose_; }
  int GetWidth() const override { return width_; }
  int GetHeight() const override { return height_; }
  int GetFramebufferWidth() const override { return width_; }
  int GetFramebufferHeight() const override { return height_; }
  bool WasFramebufferResized() const override { return framebufferResized_; }
  void ResetFramebufferResizedFlag() override { framebufferResized_ = false; }
  const std::string &GetTitle() const override { return title_; }
  void *GetNativeHandle() const override { return handle_; }
  Velos::RHI::WindowHandleType GetNativeHandleType() const override {
    return Velos::RHI::WindowHandleType::Win32;
  }
  void SetEventCallback(const EventCallbackFn &callback) override {
    callback_ = callback;
  }
  void SetVSync(bool enabled) override { vsync_ = enabled; }
  bool IsVSync() const override { return vsync_; }
  using MessageCallback = std::function<LRESULT(HWND, UINT, WPARAM, LPARAM)>;
  void SetMessageCallback(MessageCallback callback) {
    messageCallback_ = std::move(callback);
  }

 private:
  static LRESULT CALLBACK WindowProcedure(HWND window, UINT message,
                                          WPARAM wParam,
                                          LPARAM lParam) noexcept;
  LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam,
                        LPARAM lParam);
  void Emit(Event &event);
  void ReleaseKeys();
  void ReleaseMouseButtons();

  HWND handle_ = nullptr;
  std::string title_;
  int width_ = 0, height_ = 0;
  bool vsync_ = true, shouldClose_ = false, framebufferResized_ = false;
  EventCallbackFn callback_;
  MessageCallback messageCallback_;
  std::bitset<KeyCount> keys_;
  std::bitset<MouseButtonCount> mouseButtons_;
  std::exception_ptr callbackException_;
  bool cursorCaptured_ = false;
  POINT savedCursorPosition_{};
  std::pair<float, float> cursorDelta_{};
};
}  // namespace Iryven
#endif
