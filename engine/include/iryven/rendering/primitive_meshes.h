#pragma once

#include <cstdint>
#include <memory>

#include <iryven/rendering/mesh_data.h>

namespace Iryven::PrimitiveMeshes {

// All primitives are centered at the origin and have unit dimensions.
[[nodiscard]] std::shared_ptr<const MeshData> Cube();
[[nodiscard]] std::shared_ptr<const MeshData> Sphere(
    std::uint32_t segments = 32, std::uint32_t rings = 16);
[[nodiscard]] std::shared_ptr<const MeshData> Cylinder(
    std::uint32_t segments = 32);
[[nodiscard]] std::shared_ptr<const MeshData> Cone(
    std::uint32_t segments = 32);
// A unit quad on the XZ plane, facing +Y.
[[nodiscard]] std::shared_ptr<const MeshData> Plane();

} // namespace Iryven::PrimitiveMeshes
