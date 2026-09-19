local livePPPath = "external/LivePP"
local hasLivePP = os.isdir(livePPPath)
local workspaceConfigurations = { "Debug", "Release", "Profile" }

if hasLivePP then
    table.insert(workspaceConfigurations, "DebugLivePP")
    print("Live++ detected: enabling the DebugLivePP configuration")
else
    print("Live++ not found: DebugLivePP will not be generated")
end

workspace "Iryven"
    architecture "x86_64"
    startproject "Sandbox"
    configurations (workspaceConfigurations)
    multiprocessorcompile "On"

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

IryvenPublicIncludeDirs = {
    "engine/include",
    "external/box3d/include",
    "external/velos/velos",
    "external/velos/external/SPIRV-Reflect",
    "external/spdlog/include",
    "external/glm",
    "external/flecs/distr",
    "external/enkiTS/src",
    "external/meshoptimizer/src",
    "external/msdf-atlas-gen",
    "external/msdf-atlas-gen/msdfgen"
}

IryvenPublicDefines = {
    "SPDLOG_COMPILED_LIB"
}

local vulkanSdk = os.getenv("VULKAN_SDK")
if not vulkanSdk then
    error("VULKAN_SDK must be set so Premake can locate glslc for shader compilation")
end
local glslc = path.join(vulkanSdk, "Bin", "glslc.exe")

include "premake/velos.lua"
include "premake/spdlog.lua"
include "premake/flecs.lua"
include "premake/box3d.lua"
include "premake/msdf.lua"
include "premake/fastgltf.lua"
include "premake/enkits.lua"
include "premake/meshoptimizer.lua"

project "Iryven"
    location "build/Iryven"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")
    files {
        "engine/include/**.h",
        "engine/src/**.h",
        "engine/src/**.cpp",
        "external/velos/external/imgui/imgui.cpp",
        "external/velos/external/imgui/imgui_draw.cpp",
        "external/velos/external/imgui/imgui_tables.cpp",
        "external/velos/external/imgui/imgui_widgets.cpp",
        "external/velos/external/imgui/backends/imgui_impl_glfw.cpp",
        "external/velos/external/imgui/backends/imgui_impl_vulkan.cpp",
        "assets/shaders/internal/**.vert",
        "assets/shaders/internal/**.frag",
        "assets/shaders/internal/**.comp",
        "assets/shaders/internal/**.hlsl",
        "assets/shaders/internal/**.glsl",
        "assets/shaders/internal/**.mesh",
        "assets/shaders/internal/**.task"
    }
    includedirs (IryvenPublicIncludeDirs)
    includedirs {
        "external/glfw/include",
        "external/velos/external/stb",
        "external/fastgltf/include",
        "engine/src/third_party",
        "external/velos/external/imgui", "external/velos/velos/core",
        "external/velos/external/volk", "external/velos/external/vma/include",
        "external/velos/external/tracy/public", vulkanSdk .. "/Include"
    }
    defines (IryvenPublicDefines)
    defines { "GLFW_INCLUDE_NONE", "IMGUI_IMPL_VULKAN_NO_PROTOTYPES" }
    links { "Velos", "spdlog", "Flecs", "Box3D", "MSDFAtlasGen", "fastgltf", "enkiTS", "meshoptimizer" }

    -- Compile GLSL to SPIR-V at build time. Runtime shader loading still
    -- performs reflection, but no longer invokes the GLSL compiler.
    filter "files:assets/shaders/internal/**.vert"
        buildmessage "Compiling %{file.name} to SPIR-V"
        buildcommands { '"' .. glslc .. '" --target-env=vulkan1.3 -fshader-stage=vert -o "%{file.abspath}.spv" "%{file.abspath}"' }
        buildoutputs { "%{file.abspath}.spv" }
    filter "files:assets/shaders/internal/**.frag"
        buildinputs { "assets/shaders/internal/gltf_material.glsl" }
        buildmessage "Compiling %{file.name} to SPIR-V"
        buildcommands { '"' .. glslc .. '" --target-env=vulkan1.3 -fshader-stage=frag -o "%{file.abspath}.spv" "%{file.abspath}"' }
        buildoutputs { "%{file.abspath}.spv" }
    filter "files:assets/shaders/internal/**.comp"
        buildmessage "Compiling %{file.name} to SPIR-V"
        buildcommands { '"' .. glslc .. '" --target-env=vulkan1.3 -fshader-stage=comp -o "%{file.abspath}.spv" "%{file.abspath}"' }
        buildoutputs { "%{file.abspath}.spv" }
    filter "files:assets/shaders/internal/**.mesh"
        buildmessage "Compiling %{file.name} to SPIR-V"
        buildcommands { '"' .. glslc .. '" --target-env=vulkan1.3 -fshader-stage=mesh -o "%{file.abspath}.spv" "%{file.abspath}"' }
        buildoutputs { "%{file.abspath}.spv" }
    filter "files:assets/shaders/internal/**.task"
        buildmessage "Compiling %{file.name} to SPIR-V"
        buildcommands { '"' .. glslc .. '" --target-env=vulkan1.3 -fshader-stage=task -o "%{file.abspath}.spv" "%{file.abspath}"' }
        buildoutputs { "%{file.abspath}.spv" }

    filter "files:**.hlsl"
        excludefrombuild "On"

    filter "system:windows"
        systemversion "latest"
        buildoptions { "/utf-8" }
        defines { "IRYVEN_PLATFORM_WINDOWS" }
    filter "system:linux"
        pic "On"
        defines { "IRYVEN_PLATFORM_LINUX" }
    filter "configurations:Debug or DebugLivePP"
        runtime "Debug"
        symbols "On"
        defines { "IRYVEN_DEBUG" }
    filter { "system:windows", "configurations:DebugLivePP" }
        debugformat "C7"
        buildoptions { "/Gm-", "/Gy", "/Gw" }
    filter "configurations:Release"
        runtime "Release"
        optimize "Speed"
        defines { "IRYVEN_RELEASE" }
    filter "configurations:Profile"
        runtime "Release"
        optimize "Speed"
        symbols "On"
        defines { "IRYVEN_PROFILE", "TRACY_ENABLE" }
    filter {}

project "Sandbox"
    location "build/Sandbox"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")
    debugdir (_WORKING_DIR)
    files { "sandbox/**.h", "sandbox/**.cpp" }
    includedirs (IryvenPublicIncludeDirs)
    includedirs { "external/velos/external/imgui" }
    defines (IryvenPublicDefines)
    links { "Iryven" }

    filter "system:windows"
        systemversion "latest"
        buildoptions { "/utf-8" }

    filter "configurations:Debug or DebugLivePP"
        runtime "Debug"
        symbols "On"
    filter { "system:windows", "configurations:DebugLivePP" }
        debugformat "C7"
        defines { "IRYVEN_WITH_LIVEPP" }
        includedirs { "external" }
        buildoptions { "/Gm-", "/Gy", "/Gw" }
        linkoptions { "/FUNCTIONPADMIN", "/OPT:NOREF", "/OPT:NOICF", "/DEBUG:FULL" }
    filter { "configurations:Release or Profile" }
        runtime "Release"
        optimize "Speed"
    filter {}

project "Editor"
    location "build/Editor"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")
    debugdir (_WORKING_DIR)
    files { "editor/**.h", "editor/**.cpp" }
    includedirs (IryvenPublicIncludeDirs)
    includedirs { "external/velos/external/imgui", "external/glfw/include" }
    defines (IryvenPublicDefines)
    defines { "GLFW_INCLUDE_NONE" }
    links { "Iryven" }
    filter "system:windows"
        systemversion "latest"
        buildoptions { "/utf-8" }
    filter "configurations:Debug or DebugLivePP"
        runtime "Debug"
        symbols "On"
    filter { "configurations:Release or Profile" }
        runtime "Release"
        optimize "Speed"
    filter "configurations:Profile"

        symbols "On"
    filter {}

project "IryvenTests"
    location "build/IryvenTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")
    files { "tests/**.cpp" }
    includedirs (IryvenPublicIncludeDirs)
    defines (IryvenPublicDefines)
    links { "Iryven" }
    filter "system:windows"
        systemversion "latest"
        buildoptions { "/utf-8" }
    filter "configurations:Debug or DebugLivePP"
        runtime "Debug"
        symbols "On"
    filter { "configurations:Release or Profile" }
        runtime "Release"
        optimize "Speed"
    filter {}
