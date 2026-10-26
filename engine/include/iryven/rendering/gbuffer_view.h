#pragma once
#include <cstdint>
namespace Iryven {
// Values match shadingGBufferView in shading.hlsl.
enum class GBufferView : std::uint32_t {
  None,
  BaseColor,
  Metallic,
  Normal,
  Roughness,
  Emissive,
  AmbientOcclusion,
  Specular,
  MaterialWorkflow,
  Depth,
  Meshlets,
  SSAO
};
}  // namespace Iryven
