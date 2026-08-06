#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Iryven {
    enum class Key : uint16_t {
        Unknown = 0,

        // Printable keys
        Space,
        Apostrophe,
        Comma,
        Minus,
        Period,
        Slash,

        D0,
        D1,
        D2,
        D3,
        D4,
        D5,
        D6,
        D7,
        D8,
        D9,

        Semicolon,
        Equal,

        A,
        B,
        C,
        D,
        E,
        F,
        G,
        H,
        I,
        J,
        K,
        L,
        M,
        N,
        O,
        P,
        Q,
        R,
        S,
        T,
        U,
        V,
        W,
        X,
        Y,
        Z,

        LeftBracket,
        Backslash,
        RightBracket,
        GraveAccent,

        // Control keys
        Escape,
        Enter,
        Tab,
        Backspace,
        Insert,
        Delete,
        Right,
        Left,
        Down,
        Up,
        PageUp,
        PageDown,
        Home,
        End,
        CapsLock,
        ScrollLock,
        NumLock,
        PrintScreen,
        Pause,

        // Function keys
        F1,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,

        // Keypad
        Keypad0,
        Keypad1,
        Keypad2,
        Keypad3,
        Keypad4,
        Keypad5,
        Keypad6,
        Keypad7,
        Keypad8,
        Keypad9,
        KeypadDecimal,
        KeypadDivide,
        KeypadMultiply,
        KeypadSubtract,
        KeypadAdd,
        KeypadEnter,
        KeypadEqual,

        // Modifiers
        LeftShift,
        LeftControl,
        LeftAlt,
        LeftSuper,
        RightShift,
        RightControl,
        RightAlt,
        RightSuper,
        Menu,

        Count
    };

    [[nodiscard]] constexpr size_t ToIndex(Key key) noexcept
    {
        return static_cast<size_t>(key);
    }

    inline constexpr size_t KeyCount = ToIndex(Key::Count);

    [[nodiscard]] constexpr bool IsValidKey(Key key) noexcept
    {
        return key > Key::Unknown && key < Key::Count;
    }

    [[nodiscard]] constexpr std::string_view ToString(Key key) noexcept
    {
        switch (key) {
        case Key::Space:          return "Space";
        case Key::Apostrophe:     return "Apostrophe";
        case Key::Comma:          return "Comma";
        case Key::Minus:          return "Minus";
        case Key::Period:         return "Period";
        case Key::Slash:          return "Slash";
        case Key::D0:             return "0";
        case Key::D1:             return "1";
        case Key::D2:             return "2";
        case Key::D3:             return "3";
        case Key::D4:             return "4";
        case Key::D5:             return "5";
        case Key::D6:             return "6";
        case Key::D7:             return "7";
        case Key::D8:             return "8";
        case Key::D9:             return "9";
        case Key::Semicolon:      return "Semicolon";
        case Key::Equal:          return "Equal";
        case Key::A:              return "A";
        case Key::B:              return "B";
        case Key::C:              return "C";
        case Key::D:              return "D";
        case Key::E:              return "E";
        case Key::F:              return "F";
        case Key::G:              return "G";
        case Key::H:              return "H";
        case Key::I:              return "I";
        case Key::J:              return "J";
        case Key::K:              return "K";
        case Key::L:              return "L";
        case Key::M:              return "M";
        case Key::N:              return "N";
        case Key::O:              return "O";
        case Key::P:              return "P";
        case Key::Q:              return "Q";
        case Key::R:              return "R";
        case Key::S:              return "S";
        case Key::T:              return "T";
        case Key::U:              return "U";
        case Key::V:              return "V";
        case Key::W:              return "W";
        case Key::X:              return "X";
        case Key::Y:              return "Y";
        case Key::Z:              return "Z";
        case Key::LeftBracket:    return "LeftBracket";
        case Key::Backslash:      return "Backslash";
        case Key::RightBracket:   return "RightBracket";
        case Key::GraveAccent:    return "GraveAccent";
        case Key::Escape:         return "Escape";
        case Key::Enter:          return "Enter";
        case Key::Tab:            return "Tab";
        case Key::Backspace:      return "Backspace";
        case Key::Insert:         return "Insert";
        case Key::Delete:         return "Delete";
        case Key::Right:          return "Right";
        case Key::Left:           return "Left";
        case Key::Down:           return "Down";
        case Key::Up:             return "Up";
        case Key::PageUp:         return "PageUp";
        case Key::PageDown:       return "PageDown";
        case Key::Home:           return "Home";
        case Key::End:            return "End";
        case Key::CapsLock:       return "CapsLock";
        case Key::ScrollLock:     return "ScrollLock";
        case Key::NumLock:        return "NumLock";
        case Key::PrintScreen:    return "PrintScreen";
        case Key::Pause:          return "Pause";
        case Key::F1:             return "F1";
        case Key::F2:             return "F2";
        case Key::F3:             return "F3";
        case Key::F4:             return "F4";
        case Key::F5:             return "F5";
        case Key::F6:             return "F6";
        case Key::F7:             return "F7";
        case Key::F8:             return "F8";
        case Key::F9:             return "F9";
        case Key::F10:            return "F10";
        case Key::F11:            return "F11";
        case Key::F12:            return "F12";
        case Key::Keypad0:        return "Keypad0";
        case Key::Keypad1:        return "Keypad1";
        case Key::Keypad2:        return "Keypad2";
        case Key::Keypad3:        return "Keypad3";
        case Key::Keypad4:        return "Keypad4";
        case Key::Keypad5:        return "Keypad5";
        case Key::Keypad6:        return "Keypad6";
        case Key::Keypad7:        return "Keypad7";
        case Key::Keypad8:        return "Keypad8";
        case Key::Keypad9:        return "Keypad9";
        case Key::KeypadDecimal:  return "KeypadDecimal";
        case Key::KeypadDivide:   return "KeypadDivide";
        case Key::KeypadMultiply: return "KeypadMultiply";
        case Key::KeypadSubtract: return "KeypadSubtract";
        case Key::KeypadAdd:      return "KeypadAdd";
        case Key::KeypadEnter:    return "KeypadEnter";
        case Key::KeypadEqual:    return "KeypadEqual";
        case Key::LeftShift:      return "LeftShift";
        case Key::LeftControl:    return "LeftControl";
        case Key::LeftAlt:        return "LeftAlt";
        case Key::LeftSuper:      return "LeftSuper";
        case Key::RightShift:     return "RightShift";
        case Key::RightControl:   return "RightControl";
        case Key::RightAlt:       return "RightAlt";
        case Key::RightSuper:     return "RightSuper";
        case Key::Menu:           return "Menu";
        default:                  return "Unknown";
        }
    }
}
