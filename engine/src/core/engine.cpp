#include <iryven/engine.h>
#include <iryven/log.h>
#include "../renderer/renderer.h"

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

    renderer_ = std::make_unique<Renderer>(*window_);

    window_->SetEventCallback(
        [this](Event& event) {
            OnEvent(event);
        });

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

void Engine::Run()
{
    auto previousTime = std::chrono::steady_clock::now();

    while (running_) {
        const auto now = std::chrono::steady_clock::now();
        const float deltaTime =
            std::chrono::duration<float>(now - previousTime).count();
        previousTime = now;

        input_.BeginFrame();
        window_->PollEvents();
        input_.EvaluateActions();

        Update(deltaTime);
        Render();
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

void Engine::Render()
{
    if (!renderer_->BeginFrame()) {
        return;
    }

    layers_.Render(*renderer_);
    renderer_->EndFrame();
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
