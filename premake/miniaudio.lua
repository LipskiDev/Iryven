local root = _WORKING_DIR
local miniaudio = path.join(root, "external/miniaudio")

project "miniaudio"
    location (path.join(root, "build/miniaudio"))
    kind "StaticLib"
    language "C"
    staticruntime "off"
    targetdir (path.join(root, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(root, "bin-int/" .. outputdir .. "/%{prj.name}"))
    files {
        miniaudio .. "/miniaudio.h",
        miniaudio .. "/miniaudio.c"
    }
    includedirs { miniaudio }

    filter "system:windows"
        systemversion "latest"
    filter "system:linux"
        pic "On"
        links { "pthread", "m", "dl" }
    filter "configurations:Debug or DebugLivePP"
        runtime "Debug"
        symbols "On"
    filter "configurations:Release or Profile"
        runtime "Release"
        optimize "Speed"
    filter "configurations:Profile"
        symbols "On"
    filter {}
