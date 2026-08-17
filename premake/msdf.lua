local root = _WORKING_DIR
local freetype = path.join(root, "external/freetype")
local atlas = path.join(root, "external/msdf-atlas-gen")
local msdfgen = path.join(atlas, "msdfgen")

local function configureStaticLibrary()
    kind "StaticLib"
    staticruntime "off"
    targetdir (path.join(root, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(root, "bin-int/" .. outputdir .. "/%{prj.name}"))

    filter "system:windows"
        systemversion "latest"
    filter "system:linux"
        pic "On"
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
end

project "FreeType"
    location (path.join(root, "build/FreeType"))
    language "C"
    cdialect "C17"
    configureStaticLibrary()
    files {
        freetype .. "/include/**.h",
        freetype .. "/src/autofit/autofit.c",
        freetype .. "/src/base/ftbase.c",
        freetype .. "/src/base/ftbbox.c",
        freetype .. "/src/base/ftbdf.c",
        freetype .. "/src/base/ftbitmap.c",
        freetype .. "/src/base/ftcid.c",
        freetype .. "/src/base/ftfstype.c",
        freetype .. "/src/base/ftgasp.c",
        freetype .. "/src/base/ftglyph.c",
        freetype .. "/src/base/ftgxval.c",
        freetype .. "/src/base/ftinit.c",
        freetype .. "/src/base/ftmm.c",
        freetype .. "/src/base/ftotval.c",
        freetype .. "/src/base/ftpatent.c",
        freetype .. "/src/base/ftpfr.c",
        freetype .. "/src/base/ftstroke.c",
        freetype .. "/src/base/ftsynth.c",
        freetype .. "/src/base/fttype1.c",
        freetype .. "/src/base/ftwinfnt.c",
        freetype .. "/src/bdf/bdf.c",
        freetype .. "/src/bzip2/ftbzip2.c",
        freetype .. "/src/cache/ftcache.c",
        freetype .. "/src/cff/cff.c",
        freetype .. "/src/cid/type1cid.c",
        freetype .. "/src/gzip/ftgzip.c",
        freetype .. "/src/hvf/hvf.c",
        freetype .. "/src/lzw/ftlzw.c",
        freetype .. "/src/pcf/pcf.c",
        freetype .. "/src/pfr/pfr.c",
        freetype .. "/src/psaux/psaux.c",
        freetype .. "/src/pshinter/pshinter.c",
        freetype .. "/src/psnames/psnames.c",
        freetype .. "/src/raster/raster.c",
        freetype .. "/src/sdf/sdf.c",
        freetype .. "/src/sfnt/sfnt.c",
        freetype .. "/src/smooth/smooth.c",
        freetype .. "/src/svg/svg.c",
        freetype .. "/src/truetype/truetype.c",
        freetype .. "/src/type1/type1.c",
        freetype .. "/src/type42/type42.c",
        freetype .. "/src/winfonts/winfnt.c"
    }
    includedirs { freetype .. "/include" }
    defines { "FT2_BUILD_LIBRARY" }

    filter "system:windows"
        files {
            freetype .. "/builds/windows/ftsystem.c",
            freetype .. "/builds/windows/ftdebug.c"
        }
        defines { "_CRT_SECURE_NO_WARNINGS", "_CRT_NONSTDC_NO_WARNINGS" }
    filter "system:not windows"
        files {
            freetype .. "/src/base/ftsystem.c",
            freetype .. "/src/base/ftdebug.c"
        }
    filter {}

project "MSDFGen"
    location (path.join(root, "build/MSDFGen"))
    language "C++"
    cppdialect "C++20"
    configureStaticLibrary()
    files {
        msdfgen .. "/msdfgen.h",
        msdfgen .. "/msdfgen-ext.h",
        msdfgen .. "/core/**.h",
        msdfgen .. "/core/**.hpp",
        msdfgen .. "/core/**.cpp",
        msdfgen .. "/ext/**.h",
        msdfgen .. "/ext/**.hpp",
        msdfgen .. "/ext/**.cpp",
        msdfgen .. "/lib/**.cpp"
    }
    includedirs {
        msdfgen,
        msdfgen .. "/include",
        freetype .. "/include"
    }
    defines {
        "MSDFGEN_USE_CPP11",
        "MSDFGEN_EXTENSIONS",
        "MSDFGEN_DISABLE_SVG",
        "MSDFGEN_DISABLE_PNG",
        "MSDFGEN_PUBLIC=",
        "MSDFGEN_EXT_PUBLIC="
    }
    links { "FreeType" }

project "MSDFAtlasGen"
    location (path.join(root, "build/MSDFAtlasGen"))
    language "C++"
    cppdialect "C++20"
    configureStaticLibrary()
    files {
        atlas .. "/msdf-atlas-gen/**.h",
        atlas .. "/msdf-atlas-gen/**.hpp",
        atlas .. "/msdf-atlas-gen/**.cpp"
    }
    includedirs {
        atlas,
        msdfgen,
        freetype .. "/include"
    }
    defines {
        "MSDF_ATLAS_NO_ARTERY_FONT",
        "MSDF_ATLAS_PUBLIC=",
        "MSDFGEN_USE_CPP11",
        "MSDFGEN_EXTENSIONS",
        "MSDFGEN_DISABLE_SVG",
        "MSDFGEN_DISABLE_PNG",
        "MSDFGEN_PUBLIC=",
        "MSDFGEN_EXT_PUBLIC="
    }
    links { "MSDFGen", "FreeType" }
