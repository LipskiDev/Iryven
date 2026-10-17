#pragma once

#include <rhi/types.h>
#include <shader/shader_compiler.h>

namespace Iryven {

constexpr Velos::RHI::GraphicsAPI GetBuildGraphicsBackend() noexcept {
#if defined(IRYVEN_GRAPHICS_DX12) && defined(IRYVEN_GRAPHICS_VULKAN)
#error Select exactly one Iryven graphics backend
#elif defined(IRYVEN_GRAPHICS_DX12)
  return Velos::RHI::GraphicsAPI::D3D12;
#else
  return Velos::RHI::GraphicsAPI::Vulkan;
#endif
}

constexpr Velos::ShaderBinaryFormat GetBuildShaderBinaryFormat() noexcept {
  return GetBuildGraphicsBackend() == Velos::RHI::GraphicsAPI::D3D12
             ? Velos::ShaderBinaryFormat::Dxil
             : Velos::ShaderBinaryFormat::Spirv;
}

}  // namespace Iryven
