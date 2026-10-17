#include "platform/win32/win32_window.h"
#include "platform/win32/win32_input.h"
#include <iryven/events/keyboard_event.h>
#include <iryven/events/mouse_event.h>
#include <iryven/log.h>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace Iryven;
static void Require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

int main() {
  try {
    Log::Init();
    Require(TranslateWin32Key(VK_CONTROL, 0) == Key::LeftControl,
            "Left control");
    Require(TranslateWin32Key(VK_CONTROL, 1ll << 24) == Key::RightControl,
            "Right control");
    Require(TranslateWin32Key(VK_SHIFT, 0x36ll << 16) == Key::RightShift,
            "Right shift");
    Require(TranslateWin32Key(VK_RETURN, 1ll << 24) == Key::KeypadEnter,
            "Keypad enter");
    Require(TranslateWin32Key(VK_HOME, 0) == Key::Keypad7, "Keypad navigation");
    Require(TranslateWin32Key(VK_HOME, 1ll << 24) == Key::Home, "Home");
    Require(TranslateWin32Key('Z', 0) == Key::Z, "Letter translation");
    Require(TranslateWin32Key(0xff, 0) == Key::Unknown, "Unknown key");
    Require(
        TranslateWin32MouseButton(WM_XBUTTONDOWN, MAKEWPARAM(0, XBUTTON2)) ==
            MouseButton::Button5,
        "Extra mouse button");
    auto factory = CreateWindow(
        {"Native factory", 320, 240, true, Velos::RHI::GraphicsAPI::D3D12});
    Require(
        factory->GetNativeHandleType() == Velos::RHI::WindowHandleType::Win32,
        "D3D12 factory handle type");
    Require(IsWindow(static_cast<HWND>(factory->GetNativeHandle())),
            "D3D12 factory HWND");
    Require(WindowProperties{}.graphicsAPI == GetBuildGraphicsBackend(),
            "Window default follows the build backend");
    auto vulkanWindow = CreateWindow(
        {"GLFW factory", 320, 240, true, Velos::RHI::GraphicsAPI::Vulkan});
    Require(vulkanWindow->GetNativeHandleType() ==
                    Velos::RHI::WindowHandleType::GLFW &&
                vulkanWindow->GetNativeHandle(),
            "Vulkan factory retains GLFW");
    Win32Window window(320, 240, "Iryven \xc3\xa4 window", true, true);
    const auto handle = static_cast<HWND>(window.GetNativeHandle());
    Require(IsWindow(handle) && !IsWindowVisible(handle),
            "Hidden valid window");
    Require(AreDpiAwarenessContextsEqual(
                GetWindowDpiAwarenessContext(handle),
                DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2),
            "Native window uses per-monitor DPI awareness like GLFW");
    Require(window.GetWidth() == 320 && window.GetFramebufferHeight() == 240,
            "Client dimensions");
    wchar_t title[64]{};
    GetWindowTextW(handle, title, 64);
    Require(std::wstring(title) == L"Iryven \u00e4 window", "UTF-8 title");
    window.SetVSync(false);
    Require(!window.IsVSync(), "Vsync flag");
    std::vector<EventType> events;
    int repeats = 0;
    float mouseX = 0, mouseY = 0, wheel = 0;
    window.SetEventCallback([&](Event &event) {
      events.push_back(event.GetEventType());
      if (event.GetEventType() == EventType::KeyPressed)
        repeats += static_cast<KeyPressedEvent &>(event).GetRepeatCount();
      if (event.GetEventType() == EventType::MouseMoved) {
        mouseX = static_cast<MouseMovedEvent &>(event).GetX();
        mouseY = static_cast<MouseMovedEvent &>(event).GetY();
      }
      if (event.GetEventType() == EventType::MouseScrolled)
        wheel = static_cast<MouseScrolledEvent &>(event).GetY();
    });
    SendMessageW(handle, WM_KEYDOWN, 'A', 0);
    SendMessageW(handle, WM_KEYDOWN, 'A', 1ll << 30);
    SendMessageW(handle, WM_KILLFOCUS, 0, 0);
    Require(repeats == 1 && events.back() == EventType::KeyReleased,
            "Repeat/focus key release");
    SendMessageW(handle, WM_MOUSEMOVE, 0, MAKELPARAM(-12, -7));
    Require(mouseX == -12 && mouseY == -7, "Signed mouse coordinates");
    SendMessageW(handle, WM_MOUSEWHEEL, MAKEWPARAM(0, 60), 0);
    Require(wheel == 0.5f, "Fractional mouse wheel");
    SendMessageW(handle, WM_LBUTTONDOWN, 0, 0);
    SendMessageW(handle, WM_CAPTURECHANGED, 0, 0);
    Require(events.back() == EventType::MouseButtonReleased,
            "Capture-loss release");
    if (GetCapture() == handle)
      ReleaseCapture();
    window.SetCursorCaptured(true);
    Require(GetCapture() == handle, "Free-look captures the native window");
    UINT rawDeviceCount = 0;
    GetRegisteredRawInputDevices(nullptr, &rawDeviceCount,
                                 sizeof(RAWINPUTDEVICE));
    std::vector<RAWINPUTDEVICE> rawDevices(rawDeviceCount);
    GetRegisteredRawInputDevices(rawDevices.data(), &rawDeviceCount,
                                 sizeof(RAWINPUTDEVICE));
    bool relativeMouseRegistered = false;
    for (const auto &device : rawDevices)
      relativeMouseRegistered |= device.usUsagePage == 1 &&
                                 device.usUsage == 2 &&
                                 device.hwndTarget == handle;
    Require(relativeMouseRegistered,
            "Free-look registers relative mouse input");
    SendMessageW(handle, WM_KILLFOCUS, 0, 0);
    Require(GetCapture() != handle, "Focus loss releases free-look capture");
    const auto [deltaX, deltaY] = window.GetCursorDelta();
    Require(deltaX == 0 && deltaY == 0, "Focus loss clears relative motion");
    window.SetCursorCaptured(false);
    RECT outer{0, 0, 640, 480};
    AdjustWindowRectEx(&outer, WS_OVERLAPPEDWINDOW, FALSE, 0);
    SetWindowPos(handle, nullptr, 0, 0, outer.right - outer.left,
                 outer.bottom - outer.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    Require(window.GetWidth() == 640 && window.GetHeight() == 480 &&
                window.WasFramebufferResized(),
            "Resize tracking");
    window.ResetFramebufferResizedFlag();
    Require(!window.WasFramebufferResized(), "Resize reset");
    window.SetEventCallback(
        [](Event &) { throw std::runtime_error("callback exception"); });
    SendMessageW(handle, WM_KEYDOWN, 'B', 0);
    bool caught = false;
    try {
      window.PollEvents();
    } catch (const std::runtime_error &) {
      caught = true;
    }
    Require(caught, "Callback exception deferred to PollEvents");
    window.SetEventCallback(
        [&](Event &event) { events.push_back(event.GetEventType()); });
    SendMessageW(handle, WM_CLOSE, 0, 0);
    Require(window.ShouldClose() && IsWindow(handle) &&
                events.back() == EventType::WindowClose,
            "Close retains HWND for swapchain teardown");
    Require(!factory->ShouldClose(), "Closing one window preserves the other");
    std::cout << "PASS Win32 factory, key translation, UTF-8, size, repeat, "
                 "focus/capture, mouse, callbacks and close lifetime\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
