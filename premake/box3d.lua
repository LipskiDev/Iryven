local root = _WORKING_DIR
local box3d = path.join(root, "external/box3d")

project "Box3D"
    location (path.join(root, "build/Box3D"))
    kind "StaticLib"
    language "C"
    cdialect "C17"
    staticruntime "off"
    targetdir (path.join(root, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(root, "bin-int/" .. outputdir .. "/%{prj.name}"))
    files {
        box3d .. "/include/box3d/**.h",
        box3d .. "/src/**.h",
        box3d .. "/src/**.inl",
        box3d .. "/src/**.c",
        box3d .. "/src/box3d.natvis"
    }
    includedirs {
        box3d .. "/include",
        box3d .. "/src"
    }

    filter "system:windows"
        systemversion "latest"
        defines { "_CRT_SECURE_NO_WARNINGS" }
    filter "system:linux"
        pic "On"
        links { "m" }
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
        defines { "B3_ENABLE_ASSERT" }
    filter {}
