#include <iryven/layers/game_layer.h>

#include <iryven/rendering/render_context.h>

namespace Iryven {

GameLayer::GameLayer()
    : Layer("Game"), world_(std::make_unique<World>()) {}

World& GameLayer::CreateWorld()
{
    world_ = std::make_unique<World>();
    return *world_;
}

World& GameLayer::GetWorld() noexcept
{
    return *world_;
}

const World& GameLayer::GetWorld() const noexcept
{
    return *world_;
}

void GameLayer::OnUpdate(float deltaTime)
{
    world_->Progress(deltaTime);
}

void GameLayer::OnRender(RenderContext& context)
{
    context.DrawScene(world_->ExtractRenderScene());
}

} // namespace Iryven
