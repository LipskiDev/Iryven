#pragma once

#include <iryven/layer.h>
#include <iryven/world.h>

#include <memory>

namespace Iryven {

class AsynchronousLoader;

class GameLayer final : public Layer {
public:
    GameLayer();

    World& CreateWorld();
    [[nodiscard]] World& GetWorld() noexcept;
    [[nodiscard]] const World& GetWorld() const noexcept;
    void SetSimulationEnabled(bool enabled) noexcept { simulationEnabled_ = enabled; }
    [[nodiscard]] bool IsSimulationEnabled() const noexcept { return simulationEnabled_; }

    void OnUpdate(float deltaTime) override;
    void OnRender(RenderContext& context) override;
    void ResolveAssetReferences(const AsynchronousLoader& loader);

private:
    std::unique_ptr<World> world_;
    bool simulationEnabled_ = true;
};

} // namespace Iryven
