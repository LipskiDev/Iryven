# Iryven Engine

Iryven is a 3D game engine for building interactive worlds and real-time rendering experiences.

## Getting started

Clone dependencies and generate a project with [Premake 5](https://premake.github.io/):

```sh
git submodule update --init --recursive
premake5 vs2022
```

On Windows, set `VULKAN_SDK` before generating the project. Linux users can generate GNU Makefiles with `premake5 gmake2`.

The public API is exposed from `engine/include/iryven`, with `<iryven/iryven.h>` as its umbrella header.
