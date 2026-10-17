-- Compile every stage declared in a unified HLSL source's metadata header.
function configureEngineShaders(vulkanSdk)
    local dxc = path.join(vulkanSdk, "Bin", "dxc.exe")
    local stages = {
        VERTEX = { "vs_6_6", "vert" }, FRAGMENT = { "ps_6_6", "frag" },
        COMPUTE = { "cs_6_6", "comp" }, MESH = { "ms_6_6", "mesh" }, TASK = { "as_6_6", "task" }
    }
    for _, source in ipairs(os.matchfiles("assets/shaders/internal/*.hlsl")) do
        local text = io.readfile(source)
        local declaredStages = text:match("IRYVEN_SHADER_STAGES:%s*([^\r\n]+)")
        if declaredStages then
            for _, backend in ipairs({ "Vulkan", "DX12" }) do
                local binaryExtension = backend == "Vulkan" and ".spv" or ".dxil"
                local options = backend == "Vulkan" and " -spirv -fspv-target-env=vulkan1.3 -fvk-use-gl-layout" or ""
                local directory = path.join(_WORKING_DIR, "cache/shaders/%{cfg.buildcfg}")
                local commands = { '{MKDIR} "' .. directory .. '"' }
                local outputs = {}
                for stage in declaredStages:gmatch("%S+") do
                    local settings = assert(stages[stage], "Unknown shader stage: " .. stage)
                    local destination = path.join(directory, path.getbasename(source) .. "." .. settings[2] .. binaryExtension)
                    table.insert(commands, '"' .. dxc .. '" -T ' .. settings[1] .. ' -E main -DIRYVEN_STAGE_' .. stage .. '=1' .. options .. ' "%{file.abspath}" -Fo "' .. destination .. '"')
                    table.insert(outputs, destination)
                end
                filter { "files:" .. source, "configurations:*-" .. backend }
                    buildmessage ("Compiling %{file.name} stages to " .. backend)
                    buildcommands (commands)
                    buildinputs { "assets/shaders/internal/**.hlsl" }
                    buildoutputs (outputs)
            end
        end
    end
    filter {}
end
