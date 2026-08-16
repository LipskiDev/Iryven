#pragma once

#include <iryven/layer.h>

namespace Iryven {

class UILayer final : public Layer {
public:
    UILayer() : Layer("UI") {}
};

} // namespace Iryven
