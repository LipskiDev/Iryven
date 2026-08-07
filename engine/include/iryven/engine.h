#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <iryven/world.h>
#include <iryven/events/event.h>
#include <iryven/events/application_event.h>
#include <iryven/window.h>
#include <iryven/input/input.h>


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

    void Run();

private:
    void OnEvent(Event& event);
    bool OnWindowClose(WindowCloseEvent& event);
    void Update();
    void Render();

private:
    EngineConfig config_;
    std::unique_ptr<Window> window_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<World> world_;
    bool running_ = true;
    InputHandler input_;
};

} // namespace Iryven
