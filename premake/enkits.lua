local root = _WORKING_DIR
local enkiTS = path.join(root, "external/enkiTS")

project "enkiTS"
    location (path.join(root, "build/enkiTS"))
    kind "StaticLib"
    language "C++"
    cppdialect "C++11"
    staticruntime "off"
    targetdir (path.join(root, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(root, "bin-int/" .. outputdir .. "/%{prj.name}"))
    files {
        enkiTS .. "/src/TaskScheduler.cpp",
        enkiTS .. "/src/TaskScheduler.h",
        enkiTS .. "/src/LockLessMultiReadPipe.h"
    }
    includedirs { enkiTS .. "/src" }
    defines { "ENKITS_TASK_PRIORITIES_NUM=3" }

    filter "system:windows"
        systemversion "latest"
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
