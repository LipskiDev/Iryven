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
#include <iryven/assets/asynchronous_loader.h>
#include <iryven/renderer/asset_upload_queue.h>
#include <iryven/layer_stack.h>
#include <iryven/layers/game_layer.h>
#include <iryven/layers/ui_layer.h>
#include <iryven/layers/debug_layer.h>


namespace Iryven {

class Renderer;
class ImGuiIntegration;

struct EngineConfig {
    std::string title = "Iryven";
    uint32_t width = 1920;
    uint32_t height = 1080;
    bool enableImGui = false;
};

struct CpuFrameTimings {
    float frameMs = 0.0f;
    float eventsMs = 0.0f;
    float updateMs = 0.0f;
    float beginFrameMs = 0.0f;
    float frameFenceWaitMs = 0.0f;
    float acquireImageMs = 0.0f;
    float uiBuildMs = 0.0f;
    float assetResolveMs = 0.0f;
    float sceneRenderMs = 0.0f;
    float sceneExtractionMs = 0.0f;
    float frameGraphMs = 0.0f;
    float frameGraphSchedulingMs = 0.0f;
    float commandAcquireMs = 0.0f;
    float commandBeginMs = 0.0f;
    float resourceSetupMs = 0.0f;
    float preRenderMs = 0.0f;
    float uploadLightsMs = 0.0f;
    float uploadMaterialsMs = 0.0f;
    float uploadFrameDataMs = 0.0f;
    float renderingSetupMs = 0.0f;
    float drawRecordMs = 0.0f;
    float commandEndMs = 0.0f;
    float queueSubmitMs = 0.0f;
    float uiDrawMs = 0.0f;
    float presentMs = 0.0f;
};

class Engine {
public:
    explicit Engine(EngineConfig config);
    ~Engine();
    World& CreateWorld();
    [[nodiscard]] World& GetWorld() noexcept;
    [[nodiscard]] const World& GetWorld() const noexcept;
    [[nodiscard]] const EngineConfig& GetConfig() const noexcept;
    [[nodiscard]] const CpuFrameTimings& GetCpuFrameTimings() const noexcept;
    InputHandler& GetInput();
    [[nodiscard]] Window& GetWindow() noexcept { return *window_; }
    [[nodiscard]] AssetManager& GetAssets() noexcept;
    [[nodiscard]] const AssetManager& GetAssets() const noexcept;
    [[nodiscard]] AsynchronousLoader& GetAsyncLoader() noexcept;
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
    void Render(CpuFrameTimings& timings);

private:
    EngineConfig config_;
    std::unique_ptr<Window> window_;
    bool running_ = true;
    InputHandler input_;
    AssetManager assets_;
    AssetUploadQueue assetUploads_;
    std::unique_ptr<AsynchronousLoader> asynchronousLoader_;
    std::unique_ptr<Renderer> renderer_;
    // Destroy after application layers and before the renderer/window.
    std::unique_ptr<ImGuiIntegration> imGui_;
    LayerStack layers_;
    GameLayer* gameLayer_ = nullptr;
    UILayer* uiLayer_ = nullptr;
    DebugLayer* debugLayer_ = nullptr;
    bool firstFramePresented_ = false;
    CpuFrameTimings cpuFrameTimings_{};
};

} // namespace Iryven
