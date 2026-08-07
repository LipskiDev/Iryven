local root = _WORKING_DIR
local flecs = path.join(root, "external/flecs")

project "Flecs"
    location (path.join(root, "build/Flecs"))
    kind "StaticLib"
    language "C"
    cdialect "C17"
    staticruntime "off"
    targetdir (path.join(root, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(root, "bin-int/" .. outputdir .. "/%{prj.name}"))
    files {
        flecs .. "/distr/flecs.h",
        flecs .. "/distr/flecs.c"
    }
    includedirs { flecs .. "/distr" }

    filter "system:windows"
        systemversion "latest"
        defines { "_CRT_SECURE_NO_WARNINGS" }
    filter "system:linux"
        pic "On"
        links { "pthread" }
    filter "configurations:Debug or DebugLivePP"
        runtime "Debug"
        symbols "On"
    filter { "configurations:Release or Profile" }
        runtime "Release"
        optimize "Speed"
    filter {}
