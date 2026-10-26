# HLSL shaders

The engine's 24 shader stages live in 24 `internal/*.hlsl` entry-point files.
Each file contains one stage and is shared by Vulkan and DX12. Related stages
use names such as `meshlet.hlsl`, `meshlet_mesh.hlsl`, and `meshlet_task.hlsl`.
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

Resource bindings come from HLSL register slots and spaces, without duplicate
`vk::binding` attributes or compiler binding shifts. Other Vulkan attributes
are declared directly alongside the HLSL declarations. DXC uses
those attributes for SPIR-V and ignores them for DXIL. Push constants use one
`[[vk::push_constant]] ConstantBuffer<T>` at `b0, space2147483647`, the register
space recognized by the runtime compiler. Vertex attributes use `SHADER_INPUT<n>`
with explicit Vulkan locations. Scalar combined image samplers keep matching
texture/sampler bindings and `vk::combinedImageSampler`.

`shader_compat.hlsl` contains shared screen-coordinate conversions. Callers pass
the active projection's Y scale, which already carries the renderer's viewport
convention. Tonemapping derives UVs from pixel position and texture dimensions.
No shader selects behavior using a backend preprocessor macro.

Each entry-point shader declares its stage in a `// IRYVEN_SHADER_STAGES:` header.
The build and validation tools read that header to select the compiler profile.
Entry points remain `main`; stages have separate source files and need no
preprocessor stage-selection macros. For example, `cloth.hlsl` is the compute
stage and `cloth_vertex.hlsl` is the vertex stage. Stage binaries have distinct
names such as `cloth.comp.spv` and `cloth_vertex.vert.spv`.

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
