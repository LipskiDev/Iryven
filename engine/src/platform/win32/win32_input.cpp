#include "win32_input.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace Iryven {
Key TranslateWin32Key(std::uintptr_t virtualKey,
                      std::intptr_t keyData) noexcept {
  const bool extended = (keyData & (1ll << 24)) != 0;
  const auto scanCode = static_cast<UINT>((keyData >> 16) & 0xff);
  if (virtualKey >= 'A' && virtualKey <= 'Z')
    return static_cast<Key>(static_cast<unsigned>(Key::A) + virtualKey - 'A');
  if (virtualKey >= '0' && virtualKey <= '9')
    return static_cast<Key>(static_cast<unsigned>(Key::D0) + virtualKey - '0');
  if (virtualKey >= VK_F1 && virtualKey <= VK_F12)
    return static_cast<Key>(static_cast<unsigned>(Key::F1) + virtualKey -
                            VK_F1);
  if (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD9)
    return static_cast<Key>(static_cast<unsigned>(Key::Keypad0) + virtualKey -
                            VK_NUMPAD0);
  if (!extended) {
    switch (virtualKey) {
      case VK_INSERT:
        return Key::Keypad0;
      case VK_END:
        return Key::Keypad1;
      case VK_DOWN:
        return Key::Keypad2;
      case VK_NEXT:
        return Key::Keypad3;
      case VK_LEFT:
        return Key::Keypad4;
      case VK_CLEAR:
        return Key::Keypad5;
      case VK_RIGHT:
        return Key::Keypad6;
      case VK_HOME:
        return Key::Keypad7;
      case VK_UP:
        return Key::Keypad8;
      case VK_PRIOR:
        return Key::Keypad9;
      case VK_DELETE:
        return Key::KeypadDecimal;
    }
  }
  switch (virtualKey) {
    case VK_SPACE:
      return Key::Space;
    case VK_OEM_7:
      return Key::Apostrophe;
    case VK_OEM_COMMA:
      return Key::Comma;
    case VK_OEM_MINUS:
      return Key::Minus;
    case VK_OEM_PERIOD:
      return Key::Period;
    case VK_OEM_2:
      return Key::Slash;
    case VK_OEM_1:
      return Key::Semicolon;
    case VK_OEM_PLUS:
      return Key::Equal;
    case VK_OEM_4:
      return Key::LeftBracket;
    case VK_OEM_5:
    case VK_OEM_102:
      return Key::Backslash;
    case VK_OEM_6:
      return Key::RightBracket;
    case VK_OEM_3:
      return Key::GraveAccent;
    case VK_ESCAPE:
      return Key::Escape;
    case VK_RETURN:
      return extended ? Key::KeypadEnter : Key::Enter;
    case VK_TAB:
      return Key::Tab;
    case VK_BACK:
      return Key::Backspace;
    case VK_INSERT:
      return Key::Insert;
    case VK_DELETE:
      return Key::Delete;
    case VK_RIGHT:
      return Key::Right;
    case VK_LEFT:
      return Key::Left;
    case VK_DOWN:
      return Key::Down;
    case VK_UP:
      return Key::Up;
    case VK_PRIOR:
      return Key::PageUp;
    case VK_NEXT:
      return Key::PageDown;
    case VK_HOME:
      return Key::Home;
    case VK_END:
      return Key::End;
    case VK_CAPITAL:
      return Key::CapsLock;
    case VK_SCROLL:
      return Key::ScrollLock;
    case VK_NUMLOCK:
      return Key::NumLock;
    case VK_SNAPSHOT:
      return Key::PrintScreen;
    case VK_PAUSE:
      return Key::Pause;
    case VK_DECIMAL:
      return Key::KeypadDecimal;
    case VK_DIVIDE:
      return Key::KeypadDivide;
    case VK_MULTIPLY:
      return Key::KeypadMultiply;
    case VK_SUBTRACT:
      return Key::KeypadSubtract;
    case VK_ADD:
      return Key::KeypadAdd;
    case 0x92:
      return Key::KeypadEqual;  // VK_OEM_NEC_EQUAL
    case VK_SHIFT:
      return MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX) == VK_RSHIFT
                 ? Key::RightShift
                 : Key::LeftShift;
    case VK_CONTROL:
      return extended ? Key::RightControl : Key::LeftControl;
    case VK_MENU:
      return extended ? Key::RightAlt : Key::LeftAlt;
    case VK_LSHIFT:
      return Key::LeftShift;
    case VK_RSHIFT:
      return Key::RightShift;
    case VK_LCONTROL:
      return Key::LeftControl;
    case VK_RCONTROL:
      return Key::RightControl;
    case VK_LMENU:
      return Key::LeftAlt;
    case VK_RMENU:
      return Key::RightAlt;
    case VK_LWIN:
      return Key::LeftSuper;
    case VK_RWIN:
      return Key::RightSuper;
    case VK_APPS:
      return Key::Menu;
    default:
      return Key::Unknown;
  }
}

MouseButton TranslateWin32MouseButton(unsigned int message,
                                      std::uintptr_t buttonData) noexcept {
  switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
      return MouseButton::Left;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
      return MouseButton::Right;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
      return MouseButton::Middle;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
      switch (HIWORD(buttonData)) {
        case XBUTTON1:
          return MouseButton::Button4;
        case XBUTTON2:
          return MouseButton::Button5;
        default:
          return MouseButton::Unknown;
      }
    default:
      return MouseButton::Unknown;
  }
}
}  // namespace Iryven
#endif
