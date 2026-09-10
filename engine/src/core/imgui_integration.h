#pragma once

namespace Iryven {
class Window;
class Renderer;

// Owns the UI context and platform lifecycle, independent of the graphics API.
class ImGuiIntegration final {
public:
    ImGuiIntegration(Window& window, Renderer& renderer);
    ~ImGuiIntegration();
    ImGuiIntegration(const ImGuiIntegration&) = delete;
    ImGuiIntegration& operator=(const ImGuiIntegration&) = delete;
    void BeginFrame();
    void EndFrame();
private:
    Renderer& renderer_;
};
}
