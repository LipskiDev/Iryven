#pragma once

namespace Iryven {
	struct Camera {
		float verticalFov = 60.0f;
		float nearPlane = 0.1f;
		float farPlane = 1000.0f;
		bool primary = true;
	};
}
