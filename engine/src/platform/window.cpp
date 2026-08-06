#include <iryven/window.h>
#include "glfw/glfw_window.h"


#include <utility>
#include <iostream>

namespace Iryven {
	std::unique_ptr<Window> Iryven::CreateWindow(const WindowProperties& properties)
	{
		return std::make_unique<GlfwWindow>(
			properties.width,
			properties.height,
			properties.title,
			true
		);
	}
} // namespace Iryven
