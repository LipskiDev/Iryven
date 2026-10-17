# Iryven Engine

Iryven is a 3D game engine for building interactive worlds and real-time rendering experiences.

## Getting started

Clone dependencies and generate a project with [Premake 5](https://premake.github.io/):

```sh
git submodule update --init --recursive
premake5 vs2026
```

On Windows, set `VULKAN_SDK` before generating the project. Linux users can generate GNU Makefiles with `premake5 gmake2`.

The public API is exposed from `engine/include/iryven`, with `<iryven/iryven.h>` as its umbrella header.

### Choosing the graphics backend in Visual Studio

Regenerate with `premake5 vs2026` (or `vs2022`) and reload the solution.
Use the **Solution Configurations** dropdown beside **Local Windows Debugger**:
choose **Debug-Vulkan** or **Debug-DX12**. Release, Profile, and the optional
DebugLivePP configurations provide the same backend choices; the platform stays
**x64**. Each choice has separate binary and intermediate directories.
The engine and Sandbox select their device backend from this build setting.
Sandbox uses one HLSL source compiled to DXIL or SPIR-V; its window title shows
the selected backend. D3D12ParityTests remains a D3D12-specific test executable.
The editor's deferred renderer runs on both backends; remaining RHI feature gaps
are documented in `external/velos/README.md`.

All engine shader stages now use HLSL too. See [shader conventions and validation](assets/shaders/README.md).

The engine window factory uses `Win32Window` for D3D12 and `GlfwWindow` for
Vulkan. The Win32 implementation lives in `engine/src/platform/win32`, returns
an `HWND`, and forwards native keyboard, mouse, resize, and close messages into
the existing engine event system. It creates windows hidden until `Show()`.
ImGui uses its Win32 platform backend and D3D12 renderer for DX12 builds, with
an owned shader-visible SRV heap for font textures. The integration owns one
ImGui context and forwards native messages until shutdown. Vulkan retains its
GLFW/Vulkan ImGui backends. UI draws run after scene rendering.
Build and run `Win32WindowTests` to check native input/event handling, and
`ImGuiBackendTests` in **Debug-DX12** to check ImGui pixels, font upload,
validation, input forwarding, and repeated initialization/shutdown. The DX12
editor uses fence-backed timeline synchronization for frame-graph queue dependencies.

## Textured duck Sandbox

Sandbox renders `assets/models/Duck.glb` with its embedded base-color texture
through the selected Vulkan or DX12 backend on Windows.
Generate with `premake5 vs2026` (Visual Studio 2026) or `premake5 vs2022`
(Visual Studio 2022), then build the **Sandbox** project. The Windows SDK supplies
DXC; the build copies `dxcompiler.dll` and `dxil.dll` beside Sandbox.exe.
Run from the repository root so the model and `sandbox/basic.*` shaders can be
loaded. Pass `--smoke-test` to render three frames and exit cleanly.

## Tiny editor

### Renderer requirements

The current Vulkan backend requires Vulkan 1.3 and `VK_EXT_mesh_shader` with
both mesh and task shader support. Vulkan 1.2 compatibility alone is insufficient.
Initialize dependencies with `git submodule update --init --recursive` and install
the Vulkan SDK (`VULKAN_SDK` must be set) before generating the solution.

Meshes and model primitives are converted to meshlets using meshoptimizer.
Hi-Z reduction runs on the compute queue with frame-graph timeline synchronization;
depth and pyramid images permit concurrent queue-family access. Press P to freeze
the culling camera and its depth-pyramid history for inspection.

Regenerate the solution with `premake5 vs2026`, then build and run the **Editor** project.
For Visual Studio 2022, generate with `premake5 vs2022` instead. Run from the repository root
(the generated project's debugging directory already does this) so shader assets can be found.

The editor opens a cube, floor, sun, and its own camera. Select entities in the Scene panel;
drag inspector values to edit position, rotation in degrees, scale, and light settings.
Hold right mouse over the scene to look and use WASD to move, Q/E to move down/up,
and Shift to move faster. Arrow keys also look while right mouse is held.
Release right mouse to return to the panels. Press F or use **Focus selected** to frame
the selected entity's origin. Camera speed, sensitivity, smoothing response, and FOV
can be adjusted in the inspector.

This first version uses ImGui panels over the main viewport. Press Ctrl+S to save the
scene to `scenes/editor.json`. Play mode snapshots the current scene and restores it
when stopped. The asset browser supports model import and assignment. Undo and transform gizmos
are not implemented yet.
The engine owns ImGui integration, enabled with `EngineConfig::enableImGui` (off by
default, including in Sandbox). Application layers build UI in `OnImGuiRender()`.
Context/input lifetime and frame scheduling belong to the engine; graphics initialization
and draw submission stay in its private renderer backends. Editor only includes the
ImGui widget API and contains no graphics backend setup or device access.

## Exporting Sandbox

On Windows, build and package Sandbox, its required assets, and the MSVC runtime into
one self-extracting executable:

```powershell
.\export-sandbox.ps1
```

The result is `dist\Sandbox.exe`. It extracts its versioned payload beneath
`%LOCALAPPDATA%\Iryven\Sandbox` when launched. A Vulkan 1.3 driver and a GPU with
mesh/task shader support are still required. Use `-SkipBuild` to package an existing
Release build, `-IncludeSymbols` to include its PDB, or `-OutputPath` to choose another
output file.

## Optional Live++ workflow

Live++ is optional and is not required to build or run Iryven. To enable it locally, extract the Live++ distribution so that its API is located at `external/LivePP/API`, then regenerate the Visual Studio solution:

```sh
premake5 vs2026
```

Premake detects the local installation and adds a `DebugLivePP` configuration. Build and run `Sandbox` using that configuration, edit a source file, then press `Ctrl+Alt+F11` to hot-reload it. The `external/LivePP` directory is ignored by Git and is never required by ordinary `Debug`, `Release`, or `Profile` builds.

### Bindless texture and sampler references

The renderer retains 4,096 texture slots and shares a separate table of up to
128 live sampler configurations. Identical sampler settings share a slot;
retired slots are reused only after the corresponding GPU submission completes.
`BindlessTextureIndex` packs the texture slot into the low 12 bits and the sampler
slot into the upper bits. All material shaders decode both indices, preserving
the existing 128-byte GPU material layout and asset-file format.

D3D12 descriptor snapshots are reused for unchanged binding sets and compatible
table layouts within one command-list recording. Writes increment the binding
set version; subsequent binds capture a new snapshot. Deferred update-after-bind
snapshots remain local to their submission, and their descriptor allocations
are retained until the GPU fence completes.

## Validation before merging

The formatting workflows check first-party C++ with clang-format 19.1.7 in both
repositories. Run `python tools/format_cpp.py` locally with that version.

Generate with `premake5 vs2026` (or `vs2022`), then validate:

- Build Editor and Sandbox in `Release-DX12` and `Release-Vulkan`.
- Run Sandbox with `--smoke-test` for each backend.
- Run D3D12ParityTests, Win32WindowTests, ImGuiBackendTests, and ShaderPortTests
  from the repository root. GPU validation tests require Windows Graphics Tools.
- Run IryvenTests with `--deferred-renderer` for both backend configurations.
- Run `./tools/validate_shaders.ps1 -Backend All` to validate all 46 shader targets.

The San Miguel benchmark is optional local data and is not required to build or
test. Saved scenes may reference generated `.iryasset` files; import the source
model locally before opening such scenes.

Merge the Velos branch before the Iryven branch so the recorded Velos submodule
commit remains available. Push Velos first, then Iryven. Do not replace the
submodule pointer with a different merge commit without rebuilding both backends.
