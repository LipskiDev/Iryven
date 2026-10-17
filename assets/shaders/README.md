# HLSL shaders

The engine's 23 shader stages live in 15 unified `internal/*.hlsl` files, such as
`cloth.hlsl`, `meshlet.hlsl`, and `shading.hlsl`. The obsolete GLSL sources
and their matching compiled SPIR-V files have been removed.
The Sandbox also uses one HLSL source for Vulkan and DX12.

DXC compiles Shader Model 6.6 entry points to DXIL for DX12 or SPIR-V for Vulkan.
Vulkan compilation uses `-spirv -fspv-target-env=vulkan1.3 -fvk-use-gl-layout`.
Visual Studio validates the selected target during the engine build and writes
binaries under `cache/shaders/<configuration>`. Runtime compilation uses these
same HLSL sources and returns backend-specific bytecode and reflection.

The port preserves the original algorithms, explicit buffer byte offsets,
matrix interpretation and varying locations. ByteAddressBuffer accesses retain
the original std430 storage layout, including meshlet padding. Row-major matrix
declarations and row-vector multiplication in translated code are intentional.
The initial translation used SPIRV-Cross against freshly compiled GLSL; maintain
the HLSL sources directly from here. SPIRV-Cross is not a build dependency.

`shader_compat.hlsl` isolates Vulkan attributes from DXIL. Push constants map to
Vulkan push blocks or `b0, space2147483647` on DX12. Vertex attributes use
`SHADER_INPUT<n>`, with explicit Vulkan locations. The bindless material set has
textures at binding 0 and samplers at binding 1; both use the same slot index.
Scalar combined image samplers use matching texture/sampler bindings and
`vk::combinedImageSampler` when targeting Vulkan.

Each shader declares its supported stages in a `// IRYVEN_SHADER_STAGES:` header.
The build and validation tools read that header and compile each stage separately.
`ShaderCompiler::CompileFile` supplies `IRYVEN_STAGE_VERTEX`, `IRYVEN_STAGE_COMPUTE`,
etc. automatically from its `stage` argument. Entry points remain `main`; callers
can compile `cloth.hlsl` as either Vertex or Compute. Direct DXC invocations must
pass the corresponding `-DIRYVEN_STAGE_*=1` option. Stage binaries have distinct
names such as `cloth.vert.spv` and `cloth.comp.spv`.

Shared HLSL headers contain frame data, material/light types, BRDF helpers and
shadow utilities and have no stage metadata. Existing empty VRS placeholders have no original
GLSL source and are not shader entry points.

Shader locals and parameters use descriptive names. `LoadMaterial` decodes the
128-byte CPU material into named fields; `LoadLight` reads the 80-byte light
records after their buffer header. Keep those explicit byte offsets in the
loaders when changing the CPU layout. The decoded shader material struct is
not an upload layout.

From the repository root:

```powershell
./tools/validate_shaders.ps1
```

This compiles every stage for both targets and runs spirv-val. `ShaderPortTests`
additionally exercises the runtime compiler and compares descriptor and push
constant reflection. GPU coverage comes from the Vulkan renderer tests and
DX12 parity tests; compiling all shader stages does not implement missing DX12
mesh pipelines, synchronization, or editor integration.
