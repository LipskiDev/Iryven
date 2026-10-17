#include <iryven/window.h>
#include "glfw/glfw_window.h"
#if defined(_WIN32)
#include "win32/win32_window.h"
#endif
#include <stdexcept>

#include <utility>
#include <iostream>

namespace Iryven {
std::unique_ptr<Window> CreateWindow(const WindowProperties &properties) {
  if (properties.graphicsAPI == Velos::RHI::GraphicsAPI::Vulkan) {
    return std::make_unique<GlfwWindow>(properties.width, properties.height,
                                        properties.title, true,
                                        properties.vsync);
  } else if (properties.graphicsAPI == Velos::RHI::GraphicsAPI::D3D12) {
#if defined(_WIN32)
    return std::make_unique<Win32Window>(properties.width, properties.height,
                                         properties.title, true,
                                         properties.vsync);
#else
    throw std::runtime_error("D3D12 windows require Windows");
#endif
  }
  throw std::invalid_argument("Unsupported window graphics API");
}
}  // namespace Iryven
