#pragma once

#include <iryven/input/key.h>
#include <iryven/input/mouse_button.h>

namespace Iryven {
	Key TranslateKey(int key);
	MouseButton TranslateMouseButton(int button);
}
