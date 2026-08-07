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
    "external/velos/velos",
    "external/velos/external/SPIRV-Reflect",
    "external/spdlog/include",
    "external/glm"
}

IryvenPublicDefines = {
    "SPDLOG_COMPILED_LIB"
}

include "premake/velos.lua"
include "premake/spdlog.lua"

project "Iryven"
    location "build/Iryven"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")
    files { "engine/include/**.h", "engine/src/**.h", "engine/src/**.cpp" }
    includedirs (IryvenPublicIncludeDirs)
    includedirs { "external/glfw/include" }
    defines (IryvenPublicDefines)
    links { "Velos", "spdlog" }

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
        defines { "IRYVEN_PROFILE" }
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
