#include <iryven/layers/game_layer.h>

#include <iryven/rendering/render_context.h>

#include <chrono>

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
    if (simulationEnabled_) world_->Progress(deltaTime);
}

void GameLayer::OnRender(RenderContext& context)
{
    using Clock = std::chrono::steady_clock;
    const auto elapsedMilliseconds = [](Clock::time_point start) {
        return std::chrono::duration<float, std::milli>(Clock::now() - start)
            .count();
    };

    const auto extractionStart = Clock::now();
    RenderScene scene = world_->ExtractRenderScene();
    sceneExtractionMs_ = elapsedMilliseconds(extractionStart);

    const auto drawStart = Clock::now();
    context.DrawScene(scene);
    drawSceneMs_ = elapsedMilliseconds(drawStart);
}

void GameLayer::ResolveAssetReferences(const AsynchronousLoader& loader)
{
    world_->ResolveAssetReferences(loader);
}

} // namespace Iryven
