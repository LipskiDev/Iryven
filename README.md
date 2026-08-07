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

## Optional Live++ workflow

Live++ is optional and is not required to build or run Iryven. To enable it locally, extract the Live++ distribution so that its API is located at `external/LivePP/API`, then regenerate the Visual Studio solution:

```sh
premake5 vs2026
```

Premake detects the local installation and adds a `DebugLivePP` configuration. Build and run `Sandbox` using that configuration, edit a source file, then press `Ctrl+Alt+F11` to hot-reload it. The `external/LivePP` directory is ignored by Git and is never required by ordinary `Debug`, `Release`, or `Profile` builds.
