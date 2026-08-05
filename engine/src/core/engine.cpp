#include <iryven/engine.h>
#include <rhi/device.h>
#include <iryven/log.h>


#include <utility>

namespace Iryven {

Engine::Engine(EngineConfig config)
    : m_config(std::move(config)) {
    device_ = Velos::RHI::CreateDevice({
        .graphicsAPI = Velos::RHI::GraphicsAPI::Vulkan,
        .enableValidation = true,
        .applicationName = "Iryven Engine",
    });

    Log::Init();

}

Engine::~Engine()
{
    Velos::RHI::DestroyDevice(device_);
}

World Engine::CreateWorld() const {
    return {};
}

const EngineConfig& Engine::GetConfig() const noexcept {
    return m_config;
}

} // namespace Iryven
