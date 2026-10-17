#include "win32_window.h"

#if defined(_WIN32)
#include "win32_input.h"
#include <Windowsx.h>
#include <iryven/events/application_event.h>
#include <iryven/events/keyboard_event.h>
#include <iryven/events/mouse_event.h>
#include <climits>
#include <stdexcept>
#include <utility>

namespace Iryven {
namespace {
constexpr wchar_t WindowClassName[] = L"IryvenWin32Window";

std::wstring ToWideTitle(const std::string &title) {
  if (title.empty())
    return {};
  if (title.size() > INT_MAX)
    throw std::invalid_argument("Window title is too long");
  const int count =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title.data(),
                          static_cast<int>(title.size()), nullptr, 0);
  if (!count)
    throw std::invalid_argument("Window title must be valid UTF-8");
  std::wstring result(count, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title.data(),
                      static_cast<int>(title.size()), result.data(), count);
  return result;
}
}  // namespace

Win32Window::Win32Window(int width, int height, const std::string &title,
                         bool resizable, bool vsync)
    : title_(title), width_(width), height_(height), vsync_(vsync) {
  if (width <= 0 || height <= 0)
    throw std::invalid_argument("Window dimensions must be positive");
  // Match GLFW: client sizes and framebuffer sizes must be physical pixels.
  // Otherwise Windows bitmap-scales the entire D3D12 window on HiDPI displays.
  // An embedding application or manifest may already have selected awareness.
  if (!SetProcessDpiAwarenessContext(
          DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) &&
      GetLastError() != ERROR_ACCESS_DENIED)
    throw std::runtime_error("Failed to enable per-monitor DPI awareness");
  const auto wideTitle = ToWideTitle(title);
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.style = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc = WindowProcedure;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  windowClass.lpszClassName = WindowClassName;
  if (!RegisterClassExW(&windowClass) &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    throw std::runtime_error(
        "Failed to register the Iryven Win32 window class");

  const DWORD style =
      resizable ? WS_OVERLAPPEDWINDOW
                : WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
  RECT rectangle{0, 0, width, height};
  if (!AdjustWindowRectEx(&rectangle, style, FALSE, 0))
    throw std::runtime_error("Failed to calculate Win32 client size");
  const auto created = CreateWindowExW(
      0, WindowClassName, wideTitle.c_str(), style, CW_USEDEFAULT,
      CW_USEDEFAULT, rectangle.right - rectangle.left,
      rectangle.bottom - rectangle.top, nullptr, nullptr, instance, this);
  if (!created)
    throw std::runtime_error("Failed to create the Iryven Win32 window");
  RECT client{};
  GetClientRect(handle_, &client);
  width_ = client.right;
  height_ = client.bottom;
  framebufferResized_ = false;
}

Win32Window::~Win32Window() {
  SetCursorCaptured(false);
  // The device/swapchain must be destroyed before its owning Window.
  callback_ = {};
  messageCallback_ = {};
  if (handle_)
    DestroyWindow(handle_);
}

void Win32Window::PollEvents() {
  cursorDelta_ = {};
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    if (message.message == WM_QUIT) {
      shouldClose_ = true;
      break;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
    if (callbackException_)
      break;
  }
  if (callbackException_)
    std::rethrow_exception(std::exchange(callbackException_, {}));
}

void Win32Window::Show() {
  ShowWindow(handle_, SW_SHOW);
}

bool Win32Window::IsFocused() const {
  return GetForegroundWindow() == handle_;
}

void Win32Window::SetCursorCaptured(bool captured) {
  if (cursorCaptured_ == captured)
    return;
  RAWINPUTDEVICE mouse{0x01, 0x02, captured ? 0u : RIDEV_REMOVE,
                       captured ? handle_ : nullptr};
  if (!RegisterRawInputDevices(&mouse, 1, sizeof(mouse)) && captured)
    throw std::runtime_error("Failed to register relative mouse input");
  cursorCaptured_ = captured;
  cursorDelta_ = {};
  if (captured) {
    GetCursorPos(&savedCursorPosition_);
    RECT client{};
    GetClientRect(handle_, &client);
    MapWindowPoints(handle_, nullptr, reinterpret_cast<POINT *>(&client), 2);
    ClipCursor(&client);
    SetCapture(handle_);
    ShowCursor(FALSE);
  } else {
    ClipCursor(nullptr);
    ShowCursor(TRUE);
    if (GetCapture() == handle_)
      ReleaseCapture();
    SetCursorPos(savedCursorPosition_.x, savedCursorPosition_.y);
  }
}
void Win32Window::Emit(Event &event) {
  if (callback_)
    callback_(event);
}

void Win32Window::ReleaseKeys() {
  for (size_t i = 1; i < KeyCount; ++i)
    if (keys_.test(i)) {
      keys_.reset(i);
      KeyReleasedEvent event(static_cast<Key>(i));
      Emit(event);
    }
}
void Win32Window::ReleaseMouseButtons() {
  for (size_t i = 1; i < MouseButtonCount; ++i)
    if (mouseButtons_.test(i)) {
      mouseButtons_.reset(i);
      MouseButtonReleasedEvent event(static_cast<MouseButton>(i));
      Emit(event);
    }
}

LRESULT CALLBACK Win32Window::WindowProcedure(HWND window, UINT message,
                                              WPARAM wParam,
                                              LPARAM lParam) noexcept {
  auto *self =
      reinterpret_cast<Win32Window *>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    self = static_cast<Win32Window *>(
        reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
    self->handle_ = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self)
    return DefWindowProcW(window, message, wParam, lParam);
  try {
    return self->HandleMessage(window, message, wParam, lParam);
  } catch (...) {
    if (!self->callbackException_)
      self->callbackException_ = std::current_exception();
    return 0;
  }
}

LRESULT Win32Window::HandleMessage(HWND window, UINT message, WPARAM wParam,
                                   LPARAM lParam) {
  if (messageCallback_) {
    const LRESULT result = messageCallback_(window, message, wParam, lParam);
    if (result)
      return result;
  }
  switch (message) {
    case WM_CLOSE: {
      if (!shouldClose_) {
        shouldClose_ = true;
        WindowCloseEvent event;
        Emit(event);
      }
      return 0;  // Keep HWND alive until the swapchain has been torn down.
    }
    case WM_NCDESTROY:
      SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      handle_ = nullptr;
      shouldClose_ = true;
      return DefWindowProcW(window, message, wParam, lParam);
    case WM_SIZE: {
      RECT client{};
      GetClientRect(window, &client);
      const int width = wParam == SIZE_MINIMIZED ? 0 : client.right;
      const int height = wParam == SIZE_MINIMIZED ? 0 : client.bottom;
      if (width != width_ || height != height_) {
        width_ = width;
        height_ = height;
        framebufferResized_ = true;
        WindowResizeEvent event(width_, height_);
        Emit(event);
      }
      return 0;
    }
    case WM_DPICHANGED: {
      const auto &suggested = *reinterpret_cast<const RECT *>(lParam);
      SetWindowPos(window, nullptr, suggested.left, suggested.top,
                   suggested.right - suggested.left,
                   suggested.bottom - suggested.top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_KILLFOCUS:
      SetCursorCaptured(false);
      ReleaseKeys();
      ReleaseMouseButtons();
      if (GetCapture() == window)
        ReleaseCapture();
      return 0;
    case WM_CAPTURECHANGED:
      if (reinterpret_cast<HWND>(lParam) != window) {
        SetCursorCaptured(false);
        ReleaseMouseButtons();
      }
      return 0;
    case WM_INPUT: {
      RAWINPUT input{};
      UINT size = sizeof(input);
      if (cursorCaptured_ &&
          GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT,
                          &input, &size, sizeof(RAWINPUTHEADER)) != UINT(-1) &&
          input.header.dwType == RIM_TYPEMOUSE &&
          !(input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
        cursorDelta_.first += static_cast<float>(input.data.mouse.lLastX);
        cursorDelta_.second += static_cast<float>(input.data.mouse.lLastY);
      }
      break;  // DefWindowProc releases foreground raw-input storage.
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      const Key key = TranslateWin32Key(wParam, lParam);
      if (IsValidKey(key)) {
        const bool repeat = keys_.test(ToIndex(key)) || (lParam & (1ll << 30));
        keys_.set(ToIndex(key));
        KeyPressedEvent event(key, repeat ? 1 : 0);
        Emit(event);
      }
      if (message == WM_KEYDOWN)
        return 0;
      break;  // DefWindowProc handles Alt+F4 and other system shortcuts.
    }
    case WM_KEYUP:
    case WM_SYSKEYUP: {
      const Key key = TranslateWin32Key(wParam, lParam);
      if (IsValidKey(key)) {
        if (key == Key::PrintScreen && !keys_.test(ToIndex(key))) {
          KeyPressedEvent pressed(key, 0);
          Emit(pressed);
        }
        keys_.reset(ToIndex(key));
        KeyReleasedEvent event(key);
        Emit(event);
      }
      if (message == WM_KEYUP)
        return 0;
      break;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN: {
      const auto button = TranslateWin32MouseButton(message, wParam);
      if (IsValidMouseButton(button)) {
        mouseButtons_.set(ToIndex(button));
        SetCapture(window);
        MouseButtonPressedEvent event(button);
        Emit(event);
      }
      return message == WM_XBUTTONDOWN ? TRUE : 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP: {
      const auto button = TranslateWin32MouseButton(message, wParam);
      if (IsValidMouseButton(button)) {
        mouseButtons_.reset(ToIndex(button));
        if (mouseButtons_.none() && GetCapture() == window)
          ReleaseCapture();
        MouseButtonReleasedEvent event(button);
        Emit(event);
      }
      return message == WM_XBUTTONUP ? TRUE : 0;
    }
    case WM_MOUSEMOVE: {
      MouseMovedEvent event(static_cast<float>(GET_X_LPARAM(lParam)),
                            static_cast<float>(GET_Y_LPARAM(lParam)));
      Emit(event);
      return 0;
    }
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
      const float delta =
          static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
      MouseScrolledEvent event(message == WM_MOUSEHWHEEL ? -delta : 0.0f,
                               message == WM_MOUSEWHEEL ? delta : 0.0f);
      Emit(event);
      return 0;
    }
  }
  return DefWindowProcW(window, message, wParam, lParam);
}
}  // namespace Iryven
#endif
