#include "imgui_integration.h"
#include "../renderer/renderer.h"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#ifdef _WIN32
#include "../platform/win32/win32_window.h"
#include <backends/imgui_impl_win32.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM,
                                                             LPARAM);
#endif

#include <stdexcept>

namespace Iryven {
ImGuiIntegration::ImGuiIntegration(Window &window, Renderer &renderer)
    : window_(window), renderer_(renderer) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  bool initialized = false;
  try {
    if (window_.GetNativeHandleType() == Velos::RHI::WindowHandleType::GLFW) {
      initialized = ImGui_ImplGlfw_InitForOther(
          static_cast<GLFWwindow *>(window_.GetNativeHandle()), true);
    }
#ifdef _WIN32
    else if (window_.GetNativeHandleType() ==
             Velos::RHI::WindowHandleType::Win32) {
      auto &nativeWindow = dynamic_cast<Win32Window &>(window_);
      initialized = ImGui_ImplWin32_Init(window_.GetNativeHandle());
      if (initialized)
        nativeWindow.SetMessageCallback(ImGui_ImplWin32_WndProcHandler);
    }
#endif
    if (!initialized)
      throw std::runtime_error("Could not initialize ImGui platform backend");
    renderer_.InitializeImGui();
  } catch (...) {
    if (initialized)
      ShutdownPlatform();
    ImGui::DestroyContext();
    throw;
  }
}

void ImGuiIntegration::ShutdownPlatform() {
  if (window_.GetNativeHandleType() == Velos::RHI::WindowHandleType::GLFW) {
    ImGui_ImplGlfw_Shutdown();
  }
#ifdef _WIN32
  else {
    static_cast<Win32Window &>(window_).SetMessageCallback({});
    ImGui_ImplWin32_Shutdown();
  }
#endif
}

ImGuiIntegration::~ImGuiIntegration() {
  renderer_.ShutdownImGui();
  ShutdownPlatform();
  ImGui::DestroyContext();
}

void ImGuiIntegration::BeginFrame() {
  renderer_.BeginImGuiFrame();
  if (window_.GetNativeHandleType() == Velos::RHI::WindowHandleType::GLFW) {
    ImGui_ImplGlfw_NewFrame();
  }
#ifdef _WIN32
  else {
    ImGui_ImplWin32_NewFrame();
  }
#endif
  ImGui::NewFrame();
}

void ImGuiIntegration::EndFrame() {
  ImGui::Render();
}
}  // namespace Iryven
