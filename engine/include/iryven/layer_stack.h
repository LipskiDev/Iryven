#pragma once

#include <iryven/layer.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace Iryven {

class LayerStack {
public:
    ~LayerStack();

    Layer& PushLayer(std::unique_ptr<Layer> layer);
    Layer& PushOverlay(std::unique_ptr<Layer> overlay);
    std::unique_ptr<Layer> PopLayer(Layer& layer);
    std::unique_ptr<Layer> PopOverlay(Layer& overlay);

    void Update(float deltaTime);
    void Render(RenderContext& context);
    void PropagateEvent(Event& event);

    [[nodiscard]] std::size_t Size() const noexcept { return layers_.size(); }

private:
    std::vector<std::unique_ptr<Layer>> layers_;
    std::size_t overlayBegin_ = 0;
};

} // namespace Iryven
