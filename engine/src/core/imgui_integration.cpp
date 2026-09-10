#include "imgui_integration.h"
#include "../renderer/renderer.h"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <stdexcept>

namespace Iryven {
ImGuiIntegration::ImGuiIntegration(Window& window, Renderer& renderer) : renderer_(renderer)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    // The platform backend needs no graphics API state.
    if (!ImGui_ImplGlfw_InitForOther(static_cast<GLFWwindow*>(window.GetNativeHandle()), true)) {
        ImGui::DestroyContext();
        throw std::runtime_error("Could not initialize ImGui input");
    }
    try {
        renderer_.InitializeImGui();
    } catch (...) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        throw;
    }
}

ImGuiIntegration::~ImGuiIntegration()
{
    renderer_.ShutdownImGui();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiIntegration::BeginFrame()
{
    renderer_.BeginImGuiFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiIntegration::EndFrame() { ImGui::Render(); }
}
