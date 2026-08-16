#pragma once

#include <iryven/layer.h>
#include <iryven/world.h>

#include <memory>

namespace Iryven {

class GameLayer final : public Layer {
public:
    GameLayer();

    World& CreateWorld();
    [[nodiscard]] World& GetWorld() noexcept;
    [[nodiscard]] const World& GetWorld() const noexcept;

    void OnUpdate(float deltaTime) override;
    void OnRender(RenderContext& context) override;

private:
    std::unique_ptr<World> world_;
};

} // namespace Iryven
