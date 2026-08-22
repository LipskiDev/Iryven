local root = _WORKING_DIR
local fastgltf = path.join(root, "external/fastgltf")
local simdjson = path.join(root, "external/simdjson")

project "simdjson"
    location (path.join(root, "build/simdjson"))
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"
    targetdir (path.join(root, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(root, "bin-int/" .. outputdir .. "/%{prj.name}"))
    files {
        simdjson .. "/singleheader/simdjson.h",
        simdjson .. "/singleheader/simdjson.cpp"
    }
    includedirs { simdjson .. "/singleheader" }
    defines { "SIMDJSON_THREADS_ENABLED=1" }

    filter "system:windows"
        systemversion "latest"
        defines { "_CRT_SECURE_NO_WARNINGS" }
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

project "fastgltf"
    location (path.join(root, "build/fastgltf"))
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"
    targetdir (path.join(root, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(root, "bin-int/" .. outputdir .. "/%{prj.name}"))
    files {
        fastgltf .. "/include/fastgltf/**.hpp",
        fastgltf .. "/src/fastgltf.cpp",
        fastgltf .. "/src/base64.cpp",
        fastgltf .. "/src/io.cpp"
    }
    includedirs {
        fastgltf .. "/include",
        simdjson .. "/singleheader"
    }
    defines {
        "FASTGLTF_USE_CUSTOM_SMALLVECTOR=0",
        "FASTGLTF_DISABLE_CUSTOM_MEMORY_POOL=0",
        "FASTGLTF_USE_64BIT_FLOAT=0",
        "FASTGLTF_ENABLE_KHR_IMPLICIT_SHAPES=0",
        "FASTGLTF_ENABLE_KHR_PHYSICS_RIGID_BODIES=0"
    }
    links { "simdjson" }

    filter "system:windows"
        systemversion "latest"
        defines { "_CRT_SECURE_NO_WARNINGS" }
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
