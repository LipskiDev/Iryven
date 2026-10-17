local livePPPath = "external/LivePP"
local hasLivePP = os.isdir(livePPPath)
local workspaceConfigurations = { "Debug", "Release", "Profile" }
include "premake/backends.lua"
include "premake/shaders.lua"

if hasLivePP then
    table.insert(workspaceConfigurations, "DebugLivePP")
    print("Live++ detected: enabling the DebugLivePP configuration")
else
    print("Live++ not found: DebugLivePP will not be generated")
end

workspace "Iryven"
    architecture "x86_64"
    startproject "Sandbox"
    multiprocessorcompile "On"
    configureGraphicsBackends(workspaceConfigurations)

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
    "external/miniaudio",
    "external/msdf-atlas-gen",
    "external/msdf-atlas-gen/msdfgen"
}

IryvenPublicDefines = {
    "SPDLOG_COMPILED_LIB"
}

local vulkanSdk = os.getenv("VULKAN_SDK")
if not vulkanSdk then
    error("VULKAN_SDK must be set so Premake can locate DXC for HLSL compilation")
end


local dxcRuntimeCopyCommands = {
    '{COPYFILE} "' .. path.join(vulkanSdk, 'Bin', 'dxcompiler.dll') .. '" "%{cfg.targetdir}"',
    '{COPYFILE} "$(WindowsSdkDir)bin\\$(TargetPlatformVersion)\\x64\\dxil.dll" "%{cfg.targetdir}"'
}

include "premake/velos.lua"
include "premake/spdlog.lua"
include "premake/flecs.lua"
include "premake/box3d.lua"
include "premake/msdf.lua"
include "premake/fastgltf.lua"
include "premake/enkits.lua"
include "premake/meshoptimizer.lua"
include "premake/miniaudio.lua"

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
        "assets/shaders/internal/**.hlsl"
    }
    includedirs (IryvenPublicIncludeDirs)
    includedirs {
        "external/glfw/include",
        "external/velos/external/stb",
        "external/fastgltf/include",
        "external/simdjson/singleheader",
        "engine/src/third_party",
        "external/velos/external/imgui", "external/velos/velos/core",
        "external/velos/external/volk", "external/velos/external/vma/include",
        "external/velos/external/tracy/public", vulkanSdk .. "/Include"
    }
    defines (IryvenPublicDefines)
    defines { "GLFW_INCLUDE_NONE", "IMGUI_IMPL_VULKAN_NO_PROTOTYPES" }
    links { "Velos", "spdlog", "Flecs", "Box3D", "MSDFAtlasGen", "fastgltf", "enkiTS", "meshoptimizer", "miniaudio" }

    filter "files:**.hlsl"
        buildaction "None"
    filter {}
    configureEngineShaders(vulkanSdk)

    filter "system:windows"
        systemversion "latest"
        buildoptions { "/utf-8" }
        defines { "IRYVEN_PLATFORM_WINDOWS" }
        files {
            "external/velos/external/imgui/backends/imgui_impl_win32.cpp",
            "external/velos/external/imgui/backends/imgui_impl_dx12.cpp"
        }
        links { "d3d12", "dxgi", "dxguid", "d3dcompiler", "dwmapi", "imm32" }
    filter "system:linux"
        pic "On"
        defines { "IRYVEN_PLATFORM_LINUX" }
    filter "configurations:Debug*"
        runtime "Debug"
        symbols "On"
        defines { "IRYVEN_DEBUG" }
    filter { "system:windows", "configurations:DebugLivePP*" }
        debugformat "C7"
        buildoptions { "/Gm-", "/Gy", "/Gw" }
    filter "configurations:Release*"
        runtime "Release"
        optimize "Speed"
        defines { "IRYVEN_RELEASE" }
    filter "configurations:Profile*"
        runtime "Release"
        optimize "Speed"
        symbols "On"
        defines { "IRYVEN_PROFILE", "TRACY_ENABLE" }
    filter {}

project "Sandbox"
    location "build/Sandbox"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++23"
    staticruntime "off"
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")
    debugdir (_WORKING_DIR)
    files { "sandbox/main.cpp", "sandbox/**.h", "sandbox/**.hlsl" }
    filter "files:sandbox/**.hlsl"
        buildaction "None"
    filter {}
    includedirs {
        "engine/include",
        "external/glfw/include",
        "external/velos/velos",
        "external/velos/velos/core",
        "external/velos/external/glm",
        "external/velos/external/SPIRV-Reflect"
    }
    links { "Velos" }

    filter "system:windows"
        systemversion "latest"
        buildoptions { "/utf-8" }
        links { "d3d12", "dxgi", "dxguid", "user32" }
        postbuildcommands (dxcRuntimeCopyCommands)

    filter "configurations:Debug*"
        runtime "Debug"
        symbols "On"
    filter { "system:windows", "configurations:DebugLivePP*" }
        debugformat "C7"
        buildoptions { "/Gm-", "/Gy", "/Gw" }
        linkoptions { "/FUNCTIONPADMIN", "/OPT:NOREF", "/OPT:NOICF", "/DEBUG:FULL" }
    filter { "configurations:Release* or Profile*" }
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
        postbuildcommands (dxcRuntimeCopyCommands)
    filter "configurations:Debug*"
        runtime "Debug"
        symbols "On"
    filter { "configurations:Release* or Profile*" }
        runtime "Release"
        optimize "Speed"
    filter "configurations:Profile*"

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
    removefiles { "tests/platform/win32_window_tests.cpp" }
    removefiles { "tests/platform/shader_port_tests.cpp" }
    removefiles { "tests/platform/imgui_backend_tests.cpp" }
    includedirs (IryvenPublicIncludeDirs)
    defines (IryvenPublicDefines)
    links { "Iryven" }
    filter "system:windows"
        systemversion "latest"
        buildoptions { "/utf-8" }
        postbuildcommands (dxcRuntimeCopyCommands)
    filter "configurations:Debug*"
        runtime "Debug"
        symbols "On"
    filter { "configurations:Release* or Profile*" }
        runtime "Release"
        optimize "Speed"
    filter {}

if os.host() == "windows" then
project "ImGuiBackendTests"
    location "build/ImGuiBackendTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")
    debugdir (_WORKING_DIR)
    files { "tests/platform/imgui_backend_tests.cpp" }
    includedirs (IryvenPublicIncludeDirs)
    includedirs { "engine/src", "external/velos/external/imgui", "external/velos/velos/core" }
    defines (IryvenPublicDefines)
    links { "Iryven" }
    filter "system:windows"
        systemversion "latest"
        buildoptions { "/utf-8" }
        postbuildcommands (dxcRuntimeCopyCommands)
    filter "configurations:Debug*"
        runtime "Debug"
        symbols "On"
    filter "configurations:Release* or Profile*"
        runtime "Release"
        optimize "Speed"
    filter {}

project "ShaderPortTests"
    location "build/ShaderPortTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++23"
    staticruntime "off"
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")
    debugdir (_WORKING_DIR)
    files { "tests/platform/shader_port_tests.cpp" }
    includedirs (IryvenPublicIncludeDirs)
    links { "Velos" }
    filter "system:windows"
        systemversion "latest"
        links { "d3d12", "dxgi", "dxguid", "user32" }
        postbuildcommands (dxcRuntimeCopyCommands)
    filter "configurations:Debug*"
        runtime "Debug"
        symbols "On"
    filter "configurations:Release* or Profile*"
        runtime "Release"
        optimize "Speed"
    filter {}

project "Win32WindowTests"
    location "build/Win32WindowTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")
    files { "tests/platform/win32_window_tests.cpp", "engine/src/platform/win32/**.cpp",
        "engine/src/platform/window.cpp", "engine/src/platform/glfw/**.cpp", "engine/src/core/log.cpp" }
    includedirs (IryvenPublicIncludeDirs)
    includedirs { "engine/src", "external/glfw/include" }
    defines (IryvenPublicDefines)
    defines { "GLFW_INCLUDE_NONE" }
    links { "GLFW", "spdlog", "user32" }
    filter "system:windows"
        systemversion "latest"
        buildoptions { "/utf-8" }
    filter "configurations:Debug*"
        runtime "Debug"
        symbols "On"
    filter "configurations:Release* or Profile*"
        runtime "Release"
        optimize "Speed"
    filter {}

project "D3D12ParityTests"
    location "build/D3D12ParityTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++23"
    staticruntime "off"
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")
    debugdir (_WORKING_DIR)
    files { "external/velos/tests/d3d12/**.cpp", "external/velos/tests/d3d12/**.hlsl" }
    includedirs { "external/velos/velos", "external/velos/velos/core", "external/velos/external/glm", "external/velos/external/SPIRV-Reflect" }
    links { "Velos" }
    filter "files:**.hlsl"
        buildaction "None"
    filter "system:windows"
        systemversion "latest"
        defines { "VL_PLATFORM_WINDOWS", "VL_HAS_D3D12" }
        links { "d3d12", "dxgi", "dxguid", "user32" }
        postbuildcommands (dxcRuntimeCopyCommands)
    filter "configurations:Debug*"
        runtime "Debug"
        symbols "On"
    filter "configurations:Release* or Profile*"
        runtime "Release"
        optimize "Speed"
    filter {}
end
