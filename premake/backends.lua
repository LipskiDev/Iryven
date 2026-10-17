-- Expose backend selection in Visual Studio's solution-configuration dropdown.
function configureGraphicsBackends(buildConfigurations)
    local backendConfigurations = {}
    for _, buildConfiguration in ipairs(buildConfigurations) do
        table.insert(backendConfigurations, buildConfiguration .. "-Vulkan")
        if os.host() == "windows" then
            table.insert(backendConfigurations, buildConfiguration .. "-DX12")
        end
    end
    configurations(backendConfigurations)

    filter "configurations:*-Vulkan"
        defines { "IRYVEN_GRAPHICS_VULKAN" }
    filter "configurations:*-DX12"
        defines { "IRYVEN_GRAPHICS_DX12" }
    filter {}
end
