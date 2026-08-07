local root = _WORKING_DIR
local spdlog = path.join(root, "external/spdlog")

project "spdlog"
    location (path.join(root, "build/spdlog"))
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"
    targetdir (path.join(root, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(root, "bin-int/" .. outputdir .. "/%{prj.name}"))

    files {
        spdlog .. "/include/**.h",
        spdlog .. "/src/**.cpp"
    }

    includedirs {
        spdlog .. "/include"
    }

    defines {
        "SPDLOG_COMPILED_LIB"
    }

    filter "system:windows"
        systemversion "latest"
        buildoptions { "/utf-8" }

    filter "system:linux"
        pic "On"
        links { "pthread" }

    filter "configurations:Debug or DebugLivePP"
        runtime "Debug"
        symbols "On"

    filter "configurations:Release"
        runtime "Release"
        optimize "Speed"

    filter "configurations:Profile"
        runtime "Release"
        optimize "Speed"
        symbols "On"

    filter {}
