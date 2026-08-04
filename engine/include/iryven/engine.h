#pragma once

#include <cstdint>
#include <string>

#include <iryven/world.h>
#include <rhi/device.h>

namespace Iryven {

struct EngineConfig {
    std::string title = "Iryven";
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
};

class Engine {
public:
    explicit Engine(EngineConfig config);
    ~Engine();
    [[nodiscard]] World CreateWorld() const;
    [[nodiscard]] const EngineConfig& GetConfig() const noexcept;

private:
    EngineConfig m_config;
    Velos::RHI::IDevice* device_ = nullptr;
};

} // namespace Iryven
