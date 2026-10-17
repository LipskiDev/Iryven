#include "imgui_renderer.h"
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>
#include <rhi/vulkan/device.h>
#ifdef _WIN32
#include <backends/imgui_impl_dx12.h>
#include <rhi/d3d12/device.h>
#include <rhi/d3d12/command_list.h>
#endif
#include <stdexcept>

namespace Iryven {
struct ImGuiRenderer::D3D12State {
#ifdef _WIN32
  Velos::D3D12::ShaderDescriptorArena descriptors;
#endif
};

ImGuiRenderer::ImGuiRenderer(Velos::RHI::IDevice &device) : device_(device) {
  if (!ImGui::GetCurrentContext())
    throw std::logic_error(
        "Create an ImGui context before initializing its renderer");

  if (device.GetBackend() == Velos::RHI::GraphicsAPI::Vulkan) {
    auto &vulkan = static_cast<Velos::Vulkan::Device &>(device);
    if (!ImGui_ImplVulkan_LoadFunctions(
            VK_API_VERSION_1_3,
            [](const char *name, void *instance) {
              return reinterpret_cast<PFN_vkVoidFunction>(
                  glfwGetInstanceProcAddress(static_cast<VkInstance>(instance),
                                             name));
            },
            vulkan.GetVkInstance()))
      throw std::runtime_error("Could not load ImGui Vulkan functions");
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = vulkan.GetVkInstance();
    info.PhysicalDevice = vulkan.GetVkPhysicalDevice();
    info.Device = vulkan.GetVkDevice();
    info.QueueFamily = vulkan.GetGraphicsQueueFamily();
    info.Queue = vulkan.GetGraphicsQueue();
    info.DescriptorPoolSize = 64;
    info.MinImageCount = 2;
    info.ImageCount = 2;
    info.UseDynamicRendering = true;
    const VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &format};
    if (!ImGui_ImplVulkan_Init(&info)) {
      if (ImGui::GetIO().BackendRendererUserData)
        ImGui_ImplVulkan_Shutdown();
      throw std::runtime_error("Could not initialize ImGui Vulkan rendering");
    }
  }
#ifdef _WIN32
  else if (device.GetBackend() == Velos::RHI::GraphicsAPI::D3D12) {
    auto &d3d12 = static_cast<Velos::D3D12::Device &>(device);
    d3d12_ = std::make_unique<D3D12State>();
    d3d12_->descriptors.Initialize(
        *d3d12.GetNativeDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1024);

    ImGui_ImplDX12_InitInfo info{};
    info.Device = d3d12.GetNativeDevice();
    info.CommandQueue = d3d12.GetGraphicsQueue().Get();
    info.NumFramesInFlight = 2;
    info.RTVFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    info.SrvDescriptorHeap = d3d12_->descriptors.Heap();
    info.UserData = d3d12_.get();
    info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo *init,
                                   D3D12_CPU_DESCRIPTOR_HANDLE *cpu,
                                   D3D12_GPU_DESCRIPTOR_HANDLE *gpu) {
      auto &arena = static_cast<D3D12State *>(init->UserData)->descriptors;
      const auto index = arena.Allocate(1);
      *cpu = arena.Cpu(index);
      *gpu = arena.Gpu(index);
    };
    info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo *init,
                                  D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                                  D3D12_GPU_DESCRIPTOR_HANDLE) {
      auto &arena = static_cast<D3D12State *>(init->UserData)->descriptors;
      const auto first = arena.Cpu(0).ptr;
      const auto stride = init->Device->GetDescriptorHandleIncrementSize(
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      arena.Free(static_cast<UINT>((cpu.ptr - first) / stride), 1);
    };
    if (!ImGui_ImplDX12_Init(&info)) {
      if (ImGui::GetIO().BackendRendererUserData)
        ImGui_ImplDX12_Shutdown();
      throw std::runtime_error("Could not initialize ImGui D3D12 rendering");
    }
  }
#endif
  else {
    throw std::runtime_error("No ImGui renderer for the active graphics API");
  }
}

ImGuiRenderer::~ImGuiRenderer() {
  device_.WaitIdle();
  if (d3d12_) {
#ifdef _WIN32
    ImGui_ImplDX12_Shutdown();
#endif
  } else {
    ImGui_ImplVulkan_Shutdown();
  }
}

void ImGuiRenderer::BeginFrame() {
#ifdef _WIN32
  if (d3d12_) {
    ImGui_ImplDX12_NewFrame();
    return;
  }
#endif
  ImGui_ImplVulkan_NewFrame();
}

void ImGuiRenderer::Draw() {
#ifdef _WIN32
  if (d3d12_) {
    auto &commands =
        static_cast<Velos::D3D12::CommandList &>(device_.GetCommandList());
    ID3D12DescriptorHeap *heaps[]{d3d12_->descriptors.Heap()};
    commands.GetNative()->SetDescriptorHeaps(1, heaps);
    // UI is the final draw pass; the RHI resets its cached state next frame.
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commands.GetNative());
    return;
  }
#endif
  ImGui_ImplVulkan_RenderDrawData(
      ImGui::GetDrawData(),
      static_cast<Velos::Vulkan::Device &>(device_).GetCommandBuffer());
}
}  // namespace Iryven
