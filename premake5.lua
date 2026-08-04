workspace "Iryven"
    architecture "x86_64"
    startproject "Sandbox"
    configurations { "Debug", "Release", "Profile" }
    multiprocessorcompile "On"

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

IryvenPublicIncludeDirs = {
    "engine/include",
    "external/velos/velos",
    "external/velos/external/SPIRV-Reflect"
}

include "premake/velos.lua"

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
    links { "Velos" }

    filter "system:windows"
        systemversion "latest"
        defines { "IRYVEN_PLATFORM_WINDOWS" }
    filter "system:linux"
        pic "On"
        defines { "IRYVEN_PLATFORM_LINUX" }
    filter "configurations:Debug"
        runtime "Debug"
        symbols "On"
        defines { "IRYVEN_DEBUG" }
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
    files { "sandbox/**.h", "sandbox/**.cpp" }
    includedirs (IryvenPublicIncludeDirs)
    links { "Iryven" }

    filter "configurations:Debug"
        runtime "Debug"
        symbols "On"
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
    links { "Iryven" }
    filter "configurations:Debug"
        runtime "Debug"
        symbols "On"
    filter { "configurations:Release or Profile" }
        runtime "Release"
        optimize "Speed"
    filter {}
