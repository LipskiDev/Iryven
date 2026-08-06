#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Iryven {

enum class MouseButton : std::uint8_t {
    Unknown = 0,
    Left,
    Right,
    Middle,
    Button4,
    Button5,
    Button6,
    Button7,
    Button8,
    Count
};

[[nodiscard]] constexpr std::size_t ToIndex(MouseButton button) noexcept
{
    return static_cast<std::size_t>(button);
}

inline constexpr std::size_t MouseButtonCount = ToIndex(MouseButton::Count);

[[nodiscard]] constexpr bool IsValidMouseButton(MouseButton button) noexcept
{
    return button > MouseButton::Unknown && button < MouseButton::Count;
}

[[nodiscard]] constexpr std::string_view ToString(MouseButton button) noexcept
{
    switch (button) {
    case MouseButton::Left:    return "Left";
    case MouseButton::Right:   return "Right";
    case MouseButton::Middle:  return "Middle";
    case MouseButton::Button4: return "Button4";
    case MouseButton::Button5: return "Button5";
    case MouseButton::Button6: return "Button6";
    case MouseButton::Button7: return "Button7";
    case MouseButton::Button8: return "Button8";
    default:                   return "Unknown";
    }
}

} // namespace Iryven
