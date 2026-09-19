local root = _WORKING_DIR
local meshoptimizer = path.join(root, "external/meshoptimizer")

project "meshoptimizer"
    location (path.join(root, "build/meshoptimizer"))
    kind "StaticLib"
    language "C++"
    cppdialect "C++11"
    staticruntime "off"
    targetdir (path.join(root, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(root, "bin-int/" .. outputdir .. "/%{prj.name}"))
    files {
        meshoptimizer .. "/src/meshoptimizer.h",
        meshoptimizer .. "/src/**.cpp"
    }
    includedirs { meshoptimizer .. "/src" }

    filter "system:windows"
        systemversion "latest"
    filter "system:linux"
        pic "On"
    filter "configurations:Debug or DebugLivePP"
        runtime "Debug"
        symbols "On"
    filter "configurations:Release or Profile"
        runtime "Release"
        optimize "Speed"
    filter "configurations:Profile"
        symbols "On"
    filter {}
