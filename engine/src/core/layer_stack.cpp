#include <iryven/layer_stack.h>

#include <algorithm>
#include <stdexcept>

namespace Iryven {

LayerStack::~LayerStack()
{
    for (auto iterator = layers_.rbegin(); iterator != layers_.rend(); ++iterator) {
        (*iterator)->OnDetach();
    }
}

Layer& LayerStack::PushLayer(std::unique_ptr<Layer> layer)
{
    if (!layer) {
        throw std::invalid_argument("Cannot push a null layer");
    }

    auto position = layers_.begin() + static_cast<std::ptrdiff_t>(overlayBegin_);
    Layer& result = *layer;
    layers_.insert(position, std::move(layer));
    ++overlayBegin_;
    result.OnAttach();
    return result;
}

Layer& LayerStack::PushOverlay(std::unique_ptr<Layer> overlay)
{
    if (!overlay) {
        throw std::invalid_argument("Cannot push a null overlay");
    }

    Layer& result = *overlay;
    layers_.push_back(std::move(overlay));
    result.OnAttach();
    return result;
}

std::unique_ptr<Layer> LayerStack::PopLayer(Layer& layer)
{
    const auto end = layers_.begin() + static_cast<std::ptrdiff_t>(overlayBegin_);
    const auto iterator = std::find_if(layers_.begin(), end,
        [&layer](const auto& candidate) { return candidate.get() == &layer; });
    if (iterator == end) {
        return nullptr;
    }

    layer.OnDetach();
    auto result = std::move(*iterator);
    layers_.erase(iterator);
    --overlayBegin_;
    return result;
}

std::unique_ptr<Layer> LayerStack::PopOverlay(Layer& overlay)
{
    const auto begin = layers_.begin() + static_cast<std::ptrdiff_t>(overlayBegin_);
    const auto iterator = std::find_if(begin, layers_.end(),
        [&overlay](const auto& candidate) { return candidate.get() == &overlay; });
    if (iterator == layers_.end()) {
        return nullptr;
    }

    overlay.OnDetach();
    auto result = std::move(*iterator);
    layers_.erase(iterator);
    return result;
}

void LayerStack::Update(float deltaTime)
{
    for (const auto& layer : layers_) {
        layer->OnUpdate(deltaTime);
    }
}

void LayerStack::Render(RenderContext& context)
{
    for (const auto& layer : layers_) {
        layer->OnRender(context);
    }
}

void LayerStack::PropagateEvent(Event& event)
{
    for (auto iterator = layers_.rbegin(); iterator != layers_.rend(); ++iterator) {
        if ((*iterator)->OnEvent(event)) {
            event.SetHandled();
            return;
        }
    }
}

} // namespace Iryven
