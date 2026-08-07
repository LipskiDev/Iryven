#include <iryven/engine.h>
#include <iryven/log.h>
#include "../renderer/renderer.h"


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

}

Engine::~Engine() = default;

World Engine::CreateWorld() const {
    return {};
}

const EngineConfig& Engine::GetConfig() const noexcept {
    return config_;
}

void Engine::Run()
{
	while (running_) {
		input_.BeginFrame();
		window_->PollEvents();
        input_.EvaluateActions();

        Update();
        Render();
    }
}

void Engine::OnEvent(Event& event)
{
	input_.OnEvent(event);

	EventDispatcher dispatcher(event);
	//IRYVEN_CORE_TRACE("{0}", event);

    dispatcher.Dispatch<WindowCloseEvent>(
        [this](WindowCloseEvent& e) {
            return OnWindowClose(e);
        }
    );
}

bool Engine::OnWindowClose(WindowCloseEvent& event)
{
    running_ = false;
    return true;
}

void Engine::Update()
{
}

void Engine::Render()
{
    if (!renderer_->BeginFrame()) {
        return;
    }

    renderer_->Clear(Color::White);
    renderer_->EndFrame();
}

InputHandler& Engine::GetInput()
{
    return input_;
}

} // namespace Iryven
