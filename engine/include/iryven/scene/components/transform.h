#pragma once

#include <limits>

#define GLM_GTX_component_wise
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

namespace Iryven {
	struct Transform {
		glm::vec3 position{ 0.0f };
		glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 scale{ 1.0f };

		[[nodiscard]] static Transform FromMatrix(const glm::mat4& matrix)
		{
			Transform out;
			glm::vec3 skew{};
			glm::vec4 perspective{};

			if (!glm::decompose(
				matrix,
				out.scale,
				out.rotation,
				out.position,
				skew,
				perspective))
			{
				return {};
			}

			out.rotation = glm::normalize(out.rotation);
			return out;
		}

		[[nodiscard]] glm::mat4 ToMatrix() const
		{
			return glm::translate(glm::mat4{ 1.0f }, position)
				* glm::mat4_cast(rotation)
				* glm::scale(glm::mat4{ 1.0f }, scale);
		}
	};
}
