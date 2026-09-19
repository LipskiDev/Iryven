#include "mesh_optimizer.h"

#include <limits>
#include <stdexcept>

#include <meshoptimizer.h>

namespace Iryven {
	namespace MeshOptimizer {
		std::vector<Meshlet> ConvertToMeshlets(
			std::span<const Vertex> vertices,
			std::span<const std::uint32_t> indices)
		{
			if (vertices.empty() || indices.empty()) return {};
			if (indices.size() % 3 != 0)
				throw std::invalid_argument("Meshlet input must contain complete triangles");
			if (vertices.size() > std::numeric_limits<std::uint32_t>::max())
				throw std::invalid_argument("Meshlet input exceeds 32-bit vertex limits");
			for (const std::uint32_t index : indices)
				if (index >= vertices.size())
					throw std::invalid_argument("Meshlet input index is out of range");

			const size_t maxVertices = 64; // Maximum number of vertices per meshlet
			const size_t maxTriangles = 128; // Maximum number of triangles per meshlet
			const float coneWeight = 0.0f; // Weight for the cone angle metric

			const size_t maxMeshlets = meshopt_buildMeshletsBound(indices.size(), maxVertices, maxTriangles);
			std::vector<meshopt_Meshlet> meshlets(maxMeshlets);

			std::vector<unsigned int> meshletVertices(indices.size());
			std::vector<unsigned char> meshletTriangles(indices.size());

			const size_t meshletCount = meshopt_buildMeshlets(
				meshlets.data(), meshletVertices.data(), meshletTriangles.data(),
				indices.data(), indices.size(), &vertices.front().position.x,
				vertices.size(), sizeof(Vertex), maxVertices, maxTriangles, coneWeight);

			std::vector<Meshlet> result(meshletCount);
			for (size_t i = 0; i < meshletCount; ++i) {
				const meshopt_Meshlet& source = meshlets[i];

				meshopt_Bounds meshletBounds = meshopt_computeMeshletBounds(
					meshletVertices.data() + source.vertex_offset,
					meshletTriangles.data() + source.triangle_offset,
					source.triangle_count,
					&vertices[0].position.x,
					vertices.size(),
					sizeof(Vertex)
				);

				result[i].vertexIndices.assign(
					meshletVertices.begin() + source.vertex_offset,
					meshletVertices.begin() + source.vertex_offset + source.vertex_count);
				result[i].triangleIndices.assign(
					meshletTriangles.begin() + source.triangle_offset,
					meshletTriangles.begin() + source.triangle_offset + source.triangle_count * 3);

				result[i].center = glm::vec3(
					meshletBounds.center[0],
					meshletBounds.center[1],
					meshletBounds.center[2]
				);

				result[i].radius = meshletBounds.radius;

				result[i].coneApex = glm::vec3(
					meshletBounds.cone_apex[0],
					meshletBounds.cone_apex[1],
					meshletBounds.cone_apex[2]
				);

				result[i].coneAxis = glm::vec3(
					meshletBounds.cone_axis[0],
					meshletBounds.cone_axis[1],
					meshletBounds.cone_axis[2]
				);

				result[i].coneCutoff = meshletBounds.cone_cutoff;
			}
			return result;
		}

		void ConvertToMeshlets(MeshData& meshData) {
			meshData.meshlets = ConvertToMeshlets(meshData.vertices, meshData.indices);
		}
	}
}
