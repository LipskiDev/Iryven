local root = _WORKING_DIR
local velos = path.join(root, "external/velos")
local glfw = path.join(root, "external/glfw")
local VulkanSDK = os.getenv("VULKAN_SDK")

project "SPIRVReflect"
    location (path.join(root, "build/SPIRVReflect"))
    kind "StaticLib"
    language "C"
    staticruntime "off"
    targetdir (path.join(root, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(root, "bin-int/" .. outputdir .. "/%{prj.name}"))
    files { velos .. "/external/SPIRV-Reflect/spirv_reflect.h", velos .. "/external/SPIRV-Reflect/spirv_reflect.c" }
    includedirs { velos .. "/external/SPIRV-Reflect" }
    filter "configurations:Debug or DebugLivePP"
        runtime "Debug"
        symbols "On"
    filter { "configurations:Release or Profile" }
        runtime "Release"
        optimize "Speed"
    filter {}

project "GLFW"
    location (path.join(root, "build/GLFW"))
    kind "StaticLib"
    language "C"
    staticruntime "off"
    targetdir (path.join(root, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(root, "bin-int/" .. outputdir .. "/%{prj.name}"))
    files {
        glfw .. "/include/**.h",
        glfw .. "/src/**.h",
        glfw .. "/src/context.c",
        glfw .. "/src/egl_context.c",
        glfw .. "/src/init.c",
        glfw .. "/src/input.c",
        glfw .. "/src/monitor.c",
        glfw .. "/src/null_*.*",
        glfw .. "/src/osmesa_context.c",
        glfw .. "/src/platform.c",
        glfw .. "/src/vulkan.c",
        glfw .. "/src/window.c"
    }
    includedirs { glfw .. "/include", glfw .. "/src" }
    filter "system:windows"
        systemversion "latest"
        defines { "_GLFW_WIN32", "_CRT_SECURE_NO_WARNINGS" }
        files { glfw .. "/src/win32_*.*", glfw .. "/src/wgl_context.c" }
    filter "system:linux"
        pic "On"
        defines { "_GLFW_X11" }
        files { glfw .. "/src/x11_*.*", glfw .. "/src/xkb_*.*", glfw .. "/src/glx_context.c", glfw .. "/src/linux_*.*", glfw .. "/src/posix_*.*" }
    filter "configurations:Debug or DebugLivePP"
        runtime "Debug"
        symbols "On"
    filter { "configurations:Release or Profile" }
        runtime "Release"
        optimize "Speed"
    filter {}

project "Velos"
    location (path.join(root, "build/Velos"))
    kind "StaticLib"
    language "C++"
    cppdialect "C++23"
    staticruntime "off"
    targetdir (path.join(root, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(root, "bin-int/" .. outputdir .. "/%{prj.name}"))
    files { velos .. "/velos/**.h", velos .. "/velos/**.hpp", velos .. "/velos/**.cpp" }
    includedirs {
        velos .. "/velos", velos .. "/velos/core", glfw .. "/include",
        velos .. "/external/glm", velos .. "/external/volk", velos .. "/external/vma/include",
        velos .. "/external/stb", velos .. "/external/SPIRV-Reflect", velos .. "/external/tracy/public"
    }
    links { "GLFW", "SPIRVReflect" }
    defines { "_CRT_SECURE_NO_WARNINGS", "GLFW_INCLUDE_NONE" }
    filter "system:windows"
        systemversion "latest"
        defines { "VL_PLATFORM_WINDOWS" }
        includedirs { (VulkanSDK or "") .. "/Include" }
        libdirs { (VulkanSDK or "") .. "/Lib" }
        links { "vulkan-1", "user32", "gdi32", "shell32", "ole32" }
    filter "system:linux"
        pic "On"
        defines { "VL_PLATFORM_LINUX" }
        links { "vulkan", "dl", "pthread", "X11", "Xrandr", "Xi", "Xxf86vm", "Xinerama", "Xcursor", "shaderc" }
    filter "configurations:Debug or DebugLivePP"
        runtime "Debug"
        symbols "On"
        defines { "VL_DEBUG" }
    filter { "system:windows", "configurations:Debug or DebugLivePP" }
        links { "shaderc_combinedd" }
    filter "configurations:Release"
        runtime "Release"
        optimize "Speed"
        defines { "VL_RELEASE" }
    filter { "system:windows", "configurations:Release or Profile" }
        links { "shaderc_combined" }
    filter "configurations:Profile"
        runtime "Release"
        optimize "Speed"
        symbols "On"
        defines { "VL_RELEASE" }
    filter {}
