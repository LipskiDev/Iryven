#pragma once

#include <cstdint>
#include <string>

#include <iryven/world.h>
#include <rhi/device.h>
#include <iryven/events/event.h>
#include <iryven/events/application_event.h>
#include <iryven/window.h>
#include <iryven/input/input.h>

namespace Iryven {

struct EngineConfig {
    std::string title = "Iryven";
    uint32_t width = 1920;
    uint32_t height = 1080;
};

class Engine {
public:
    explicit Engine(EngineConfig config);
    ~Engine();
    [[nodiscard]] World CreateWorld() const;
    [[nodiscard]] const EngineConfig& GetConfig() const noexcept;
    InputHandler& GetInput();

    void Run();

private:
    void OnEvent(Event& event);
    bool OnWindowClose(WindowCloseEvent& event);

private:
    EngineConfig config_;
    std::unique_ptr<Window> window_;
    Velos::RHI::IDevice* device_ = nullptr;
    bool running_ = true;
    InputHandler input_;
};

} // namespace Iryven
