#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <iryven/world.h>
#include <iryven/events/event.h>
#include <iryven/events/application_event.h>
#include <iryven/window.h>
#include <iryven/input/input.h>
#include <iryven/asset_manager.h>
#include <iryven/layer_stack.h>
#include <iryven/layers/game_layer.h>
#include <iryven/layers/ui_layer.h>
#include <iryven/layers/debug_layer.h>


namespace Iryven {

class Renderer;

struct EngineConfig {
    std::string title = "Iryven";
    uint32_t width = 1920;
    uint32_t height = 1080;
};

class Engine {
public:
    explicit Engine(EngineConfig config);
    ~Engine();
    World& CreateWorld();
    [[nodiscard]] World& GetWorld() noexcept;
    [[nodiscard]] const World& GetWorld() const noexcept;
    [[nodiscard]] const EngineConfig& GetConfig() const noexcept;
    InputHandler& GetInput();
    [[nodiscard]] AssetManager& GetAssets() noexcept;
    [[nodiscard]] const AssetManager& GetAssets() const noexcept;
    [[nodiscard]] GameLayer& GetGameLayer() noexcept;
    [[nodiscard]] const GameLayer& GetGameLayer() const noexcept;
    [[nodiscard]] UILayer& GetUILayer() noexcept;
    [[nodiscard]] DebugLayer& GetDebugLayer() noexcept;

    Layer& PushLayer(std::unique_ptr<Layer> layer);
    Layer& PushOverlay(std::unique_ptr<Layer> overlay);
    // Engine-owned Game, UI, and Debug layers cannot be removed.
    std::unique_ptr<Layer> PopLayer(Layer& layer);
    std::unique_ptr<Layer> PopOverlay(Layer& overlay);

    void Run();

private:
    void OnEvent(Event& event);
    bool OnWindowClose(WindowCloseEvent& event);
    void Update(float deltaTime);
    void Render();

private:
    EngineConfig config_;
    std::unique_ptr<Window> window_;
    std::unique_ptr<Renderer> renderer_;
    bool running_ = true;
    InputHandler input_;
    AssetManager assets_;
    LayerStack layers_;
    GameLayer* gameLayer_ = nullptr;
    UILayer* uiLayer_ = nullptr;
    DebugLayer* debugLayer_ = nullptr;
};

} // namespace Iryven
