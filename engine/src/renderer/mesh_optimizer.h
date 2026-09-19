#pragma once

#include <span>
#include <vector>
#include <iryven/rendering/mesh_data.h>

namespace Iryven {
	// A wrapper for meshoptimizer functions
	namespace MeshOptimizer {
		[[nodiscard]] std::vector<Meshlet> ConvertToMeshlets(
			std::span<const Vertex> vertices,
			std::span<const std::uint32_t> indices);
		void ConvertToMeshlets(MeshData& meshData);

	}
}
