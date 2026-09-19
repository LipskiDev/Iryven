#include <iryven/engine.h>
#include <iryven/log.h>
#include "../renderer/renderer.h"
#include "imgui_integration.h"

#include <chrono>
#include <stdexcept>
#include <utility>
#include <iryven/events/application_event.h>
#include <iryven/events/keyboard_event.h>

namespace Iryven {

#define BIND_EVENT_FN(x) std::bind(&Engine::x, this, std::placeholders::_1)

Engine::Engine(EngineConfig config)
    : config_(std::move(config)) {

    Log::Init();

    window_ = Iryven::CreateWindow(WindowProperties(config_.title, config_.width, config_.height));

    asynchronousLoader_ = std::make_unique<AsynchronousLoader>(assets_, assetUploads_);
    renderer_ = std::make_unique<Renderer>(*window_, assetUploads_);

    window_->SetEventCallback(
        [this](Event& event) {
            OnEvent(event);
        });

    if (config_.enableImGui) imGui_ = std::make_unique<ImGuiIntegration>(*window_, *renderer_);

    gameLayer_ = &static_cast<GameLayer&>(
        layers_.PushLayer(std::make_unique<GameLayer>()));
    uiLayer_ = &static_cast<UILayer&>(
        layers_.PushOverlay(std::make_unique<UILayer>()));
    debugLayer_ = &static_cast<DebugLayer&>(
        layers_.PushOverlay(std::make_unique<DebugLayer>()));
}

Engine::~Engine() = default;

World& Engine::CreateWorld()
{
    return gameLayer_->CreateWorld();
}

World& Engine::GetWorld() noexcept {
    return gameLayer_->GetWorld();
}

const World& Engine::GetWorld() const noexcept {
    return gameLayer_->GetWorld();
}

const EngineConfig& Engine::GetConfig() const noexcept {
    return config_;
}

const CpuFrameTimings& Engine::GetCpuFrameTimings() const noexcept {
    return cpuFrameTimings_;
}

void Engine::Run()
{
    using Clock = std::chrono::steady_clock;
    const auto elapsedMilliseconds = [](Clock::time_point start) {
        return std::chrono::duration<float, std::milli>(Clock::now() - start)
            .count();
    };
    auto previousTime = std::chrono::steady_clock::now();

    while (running_) {
        const auto frameStart = Clock::now();
        const auto now = frameStart;
        const float deltaTime =
            std::chrono::duration<float>(now - previousTime).count();
        previousTime = now;

        CpuFrameTimings currentTimings{};

        const auto eventsStart = Clock::now();
        input_.BeginFrame();
        window_->PollEvents();
        input_.EvaluateActions();
        if (input_.WasKeyPressed(Key::P)) {
            renderer_->ToggleCullingCameraFreeze();
        }
        currentTimings.eventsMs = elapsedMilliseconds(eventsStart);

        const auto updateStart = Clock::now();
        Update(deltaTime);
        currentTimings.updateMs = elapsedMilliseconds(updateStart);

        Render(currentTimings);
        currentTimings.frameMs = elapsedMilliseconds(frameStart);
        cpuFrameTimings_ = currentTimings;
    }
}

void Engine::OnEvent(Event& event)
{
	input_.OnEvent(event);

	EventDispatcher dispatcher(event);

    dispatcher.Dispatch<WindowCloseEvent>(
        [this](WindowCloseEvent& e) {
            return OnWindowClose(e);
        }
    );

    if (!event.IsHandled()) {
        layers_.PropagateEvent(event);
    }
}

bool Engine::OnWindowClose(WindowCloseEvent& event)
{
    running_ = false;
    return true;
}

void Engine::Update(float deltaTime)
{
    layers_.Update(deltaTime);
}

void Engine::Render(CpuFrameTimings& timings)
{
    using Clock = std::chrono::steady_clock;
    const auto elapsedMilliseconds = [](Clock::time_point start) {
        return std::chrono::duration<float, std::milli>(Clock::now() - start)
            .count();
    };

    const auto beginFrameStart = Clock::now();
    if (!renderer_->BeginFrame()) {
        timings.beginFrameMs = elapsedMilliseconds(beginFrameStart);
        return;
    }
    timings.beginFrameMs = elapsedMilliseconds(beginFrameStart);
    timings.frameFenceWaitMs = renderer_->GetFrameFenceWaitMs();
    timings.acquireImageMs = renderer_->GetAcquireImageMs();

    if (imGui_) {
        const auto uiBuildStart = Clock::now();
        imGui_->BeginFrame();
        layers_.RenderImGui();
        imGui_->EndFrame();
        timings.uiBuildMs = elapsedMilliseconds(uiBuildStart);
    }

    const auto assetResolveStart = Clock::now();
    gameLayer_->ResolveAssetReferences(*asynchronousLoader_);
    timings.assetResolveMs = elapsedMilliseconds(assetResolveStart);

    const auto sceneRenderStart = Clock::now();
    layers_.Render(*renderer_);
    timings.sceneRenderMs = elapsedMilliseconds(sceneRenderStart);
	timings.sceneExtractionMs = gameLayer_->GetSceneExtractionMs();
	timings.frameGraphMs = gameLayer_->GetDrawSceneMs();
	const RendererCpuTimings& rendererTimings = renderer_->GetCpuTimings();
	const FrameGraphCpuTimings& frameGraphTimings = rendererTimings.frameGraph;
	timings.frameGraphSchedulingMs = frameGraphTimings.schedulingMs;
	timings.commandAcquireMs = frameGraphTimings.acquireCommandListMs;
	timings.commandBeginMs = frameGraphTimings.commandBeginMs;
	timings.resourceSetupMs = frameGraphTimings.resourceSetupMs;
	timings.preRenderMs = frameGraphTimings.preRenderMs;
	timings.uploadLightsMs = rendererTimings.uploadLightsMs;
	timings.uploadMaterialsMs = rendererTimings.uploadMaterialsMs;
	timings.uploadFrameDataMs = rendererTimings.uploadFrameDataMs;
	timings.renderingSetupMs = frameGraphTimings.renderingSetupMs;
	timings.drawRecordMs = frameGraphTimings.drawRecordMs;
	timings.commandEndMs = frameGraphTimings.commandEndMs;
	timings.queueSubmitMs = frameGraphTimings.queueSubmitMs;

    if (imGui_) {
        const auto uiDrawStart = Clock::now();
        renderer_->DrawImGui();
        timings.uiDrawMs = elapsedMilliseconds(uiDrawStart);
    }

    const auto presentStart = Clock::now();
    renderer_->EndFrame();
    timings.presentMs = elapsedMilliseconds(presentStart);

    if (!firstFramePresented_) {
        window_->Show();
        firstFramePresented_ = true;
    }
}

InputHandler& Engine::GetInput()
{
    return input_;
}

AssetManager& Engine::GetAssets() noexcept
{
    return assets_;
}

const AssetManager& Engine::GetAssets() const noexcept
{
    return assets_;
}

AsynchronousLoader& Engine::GetAsyncLoader() noexcept
{
    return *asynchronousLoader_;
}

GameLayer& Engine::GetGameLayer() noexcept
{
    return *gameLayer_;
}

const GameLayer& Engine::GetGameLayer() const noexcept
{
    return *gameLayer_;
}

UILayer& Engine::GetUILayer() noexcept
{
    return *uiLayer_;
}

DebugLayer& Engine::GetDebugLayer() noexcept
{
    return *debugLayer_;
}

Layer& Engine::PushLayer(std::unique_ptr<Layer> layer)
{
    return layers_.PushLayer(std::move(layer));
}

Layer& Engine::PushOverlay(std::unique_ptr<Layer> overlay)
{
    return layers_.PushOverlay(std::move(overlay));
}

std::unique_ptr<Layer> Engine::PopLayer(Layer& layer)
{
    if (&layer == gameLayer_) {
        throw std::invalid_argument("The engine-owned GameLayer cannot be removed");
    }
    return layers_.PopLayer(layer);
}

std::unique_ptr<Layer> Engine::PopOverlay(Layer& overlay)
{
    if (&overlay == uiLayer_ || &overlay == debugLayer_) {
        throw std::invalid_argument("Engine-owned UI and Debug layers cannot be removed");
    }
    return layers_.PopOverlay(overlay);
}

} // namespace Iryven
