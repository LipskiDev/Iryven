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
when stopped. Undo, gizmos, and asset importing are not implemented yet.
The engine owns ImGui integration, enabled with `EngineConfig::enableImGui` (off by
default, including in Sandbox). Application layers build UI in `OnImGuiRender()`.
Context/input lifetime and frame scheduling belong to the engine; Vulkan initialization
and draw submission stay in its private renderer backend. Editor only includes the
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
