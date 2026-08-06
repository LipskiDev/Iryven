#pragma once

#include <iryven/input/key.h>
#include <iryven/input/mouse_button.h>

#include <string>
#include <variant>
#include <vector>

namespace Iryven {

    using InputBinding = std::variant<Key, MouseButton>;

    struct InputAction {
        std::string name;
        std::vector<InputBinding> bindings;

        bool down = false;
        bool pressed = false;
        bool released = false;
    };

} // namespace Iryven