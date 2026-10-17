#pragma once

#if defined(_WIN32)
#include <cstdint>
#include <iryven/input/key.h>
#include <iryven/input/mouse_button.h>

namespace Iryven {
Key TranslateWin32Key(std::uintptr_t virtualKey,
                      std::intptr_t keyData) noexcept;
MouseButton TranslateWin32MouseButton(unsigned int message,
                                      std::uintptr_t buttonData = 0) noexcept;
}  // namespace Iryven
#endif
