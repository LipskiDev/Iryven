#include <iryven/window.h>
#include "glfw/glfw_window.h"


#include <utility>
#include <iostream>

namespace Iryven {
	std::unique_ptr<Window> Iryven::CreateWindow()
	{
		return std::make_unique<GlfwWindow>(
			1600,
			900,
			"Sandbox",
			true
		);
	}
} // namespace Iryven
