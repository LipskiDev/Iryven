#include "imgui_renderer.h"
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>
#include <rhi/vulkan/device.h>
#include <stdexcept>

namespace Iryven {
ImGuiRenderer::ImGuiRenderer(Velos::RHI::IDevice& device) : device_(device)
{
    if (device.GetBackend() != Velos::RHI::GraphicsAPI::Vulkan)
        throw std::runtime_error("No ImGui renderer for the active graphics API");
    auto& vk = static_cast<Velos::Vulkan::Device&>(device);
    if (!ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3,
        [](const char* name, void* instance) {
            return reinterpret_cast<PFN_vkVoidFunction>(glfwGetInstanceProcAddress(static_cast<VkInstance>(instance), name));
        }, vk.GetVkInstance()))
        throw std::runtime_error("Could not load ImGui Vulkan functions");
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = vk.GetVkInstance();
    info.PhysicalDevice = vk.GetVkPhysicalDevice();
    info.Device = vk.GetVkDevice();
    info.QueueFamily = vk.GetGraphicsQueueFamily();
    info.Queue = vk.GetGraphicsQueue();
    info.DescriptorPoolSize = 64;
    info.MinImageCount = 2;
    info.ImageCount = 2; // Matches the renderer's frames in flight.
    info.UseDynamicRendering = true;
    const VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1, .pColorAttachmentFormats = &format };
    if (!ImGui_ImplVulkan_Init(&info)) {
        if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplVulkan_Shutdown();
        throw std::runtime_error("Could not initialize ImGui rendering");
    }
}

ImGuiRenderer::~ImGuiRenderer()
{
    device_.WaitIdle();
    ImGui_ImplVulkan_Shutdown();
}

void ImGuiRenderer::BeginFrame() { ImGui_ImplVulkan_NewFrame(); }

void ImGuiRenderer::Draw()
{
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),
        static_cast<Velos::Vulkan::Device&>(device_).GetCommandBuffer());
}
}
