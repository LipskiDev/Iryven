#pragma once

#include <iryven/layer.h>

namespace Iryven {

class DebugLayer final : public Layer {
public:
    DebugLayer() : Layer("Debug") {}
};

} // namespace Iryven
