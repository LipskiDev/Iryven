#pragma once

#include <iryven/events/event.h>

#include <string>
#include <utility>

namespace Iryven {

class RenderContext;

class Layer {
public:
    explicit Layer(std::string name = "Layer")
        : name_(std::move(name)) {}
    virtual ~Layer() = default;

    Layer(const Layer&) = delete;
    Layer& operator=(const Layer&) = delete;

    virtual void OnAttach() {}
    virtual void OnDetach() {}
    virtual void OnUpdate(float deltaTime) {}
    virtual void OnRender(RenderContext& context) {}

    // Returning true consumes the event and stops propagation to lower layers.
    virtual bool OnEvent(Event& event) { return false; }

    [[nodiscard]] const std::string& GetName() const noexcept { return name_; }

private:
    std::string name_;
};

} // namespace Iryven
