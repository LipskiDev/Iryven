#include "platform/win32/win32_window.h"
#include "renderer/imgui_renderer.h"
#include "renderer/renderer.h"
#include "renderer/bindless_texture_manager.h"
#include "core/imgui_integration.h"
#include <iryven/log.h>
#include <imgui.h>
#include <rhi/d3d12/device.h>
#include <rhi/d3d12/command_list.h>
#include <d3d12sdklayers.h>
#include <cstring>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace Velos::RHI;
namespace DX = Velos::D3D12;

static void Require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

static void Validate(ID3D12InfoQueue &queue) {
  for (UINT64 index = 0; index < queue.GetNumStoredMessages(); ++index) {
    SIZE_T size = 0;
    queue.GetMessage(index, nullptr, &size);
    std::vector<std::byte> storage(size);
    auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
    DX::ThrowIfFailed(queue.GetMessage(index, message, &size),
                      "Read validation message");
    if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
      throw std::runtime_error(message->pDescription);
  }
  queue.ClearStoredMessages();
}

static void DrawReadback() {
  Iryven::Win32Window window(256, 128, "ImGui renderer test", true, false);
  DX::Device device(
      {.graphicsAPI = GraphicsAPI::D3D12, .enableValidation = true});
  const auto swapchain = device.CreateSwapchain(
      {.window = {WindowHandleType::Win32, window.GetNativeHandle()},
       .width = 256,
       .height = 128});
  DX::ComPtr<ID3D12InfoQueue> validation;
  DX::ThrowIfFailed(
      device.GetNativeDevice()->QueryInterface(IID_PPV_ARGS(&validation)),
      "Get validation queue");
  validation->ClearStoredMessages();

  ImGuiContext *context = ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  {
    Iryven::ImGuiRenderer renderer(device);
    Require(ImGui::GetCurrentContext() == context,
            "GPU renderer replaced its owner's ImGui context");
    Require(
        std::strcmp(ImGui::GetIO().BackendRendererName, "imgui_impl_dx12") == 0,
        "Incorrect ImGui renderer backend");
    const auto target = device.CreateImage(
        {.width = 256,
         .height = 128,
         .format = Format::BGRA8_UNORM,
         .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc});
    const auto view = device.CreateImageView({.image = target});
    const auto nativeDesc = device.GetImage(target).resourcePtr->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 readbackSize = 0;
    device.GetNativeDevice()->GetCopyableFootprints(
        &nativeDesc, 0, 1, 0, &footprint, nullptr, nullptr, &readbackSize);
    const auto readback =
        device.CreateBuffer({.size = readbackSize,
                             .usage = BufferUsage::TransferDst,
                             .memoryUsage = MemoryUsage::GPUToCPU});

    for (int frame = 0; frame < 5; ++frame) {
      Require(device.BeginFrame(swapchain).success, "Begin ImGui test frame");
      renderer.BeginFrame();
      ImGui::GetIO().DisplaySize = ImVec2(256, 128);
      ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
      ImGui::NewFrame();
      const ImU32 color =
          frame % 2 ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 0, 0, 255);
      ImGui::GetBackgroundDrawList()->PushClipRect(ImVec2(16, 16),
                                                   ImVec2(48, 48));
      ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(16, 16),
                                                    ImVec2(64, 64), color);
      ImGui::GetBackgroundDrawList()->PopClipRect();
      ImGui::GetBackgroundDrawList()->AddText(ImVec2(90, 20), IM_COL32_WHITE,
                                              "D3D12 ImGui");
      ImGui::Render();
      Require(ImGui::GetDrawData()->TotalVtxCount > 0,
              "ImGui produced no draw geometry");

      auto &commands = static_cast<DX::CommandList &>(device.GetCommandList());
      commands.Begin();
      auto &sceneBatch = device.AcquireCommandList(QueueType::Graphics);
      sceneBatch.Begin();
      sceneBatch.Barrier(ImageBarrier{
          .image = target, .newLayout = ImageLayout::ColorAttachment});
      const ColorAttachmentDesc sceneAttachment{.view = view,
                                                .clearValue = {0, 0, 0, 1}};
      sceneBatch.BeginRendering({.renderArea = {.extent = {256, 128}},
                                 .colorAttachments = &sceneAttachment,
                                 .colorAttachmentCount = 1});
      sceneBatch.EndRendering();
      sceneBatch.End();
      device.Submit(QueueType::Graphics, sceneBatch);
      commands.Barrier(ImageBarrier{.image = target,
                                    .newLayout = ImageLayout::ColorAttachment});
      const ColorAttachmentDesc attachment{.view = view,
                                           .loadOp = LoadOp::Load};
      commands.BeginRendering({.renderArea = {.extent = {256, 128}},
                               .colorAttachments = &attachment,
                               .colorAttachmentCount = 1});
      renderer.Draw();
      commands.EndRendering();
      commands.Barrier(
          ImageBarrier{.image = target, .newLayout = ImageLayout::TransferSrc});
      D3D12_TEXTURE_COPY_LOCATION source{};
      source.pResource = device.GetImage(target).resourcePtr;
      source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      D3D12_TEXTURE_COPY_LOCATION destination{};
      destination.pResource = device.GetBuffer(readback).resourcePtr;
      destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      destination.PlacedFootprint = footprint;
      commands.GetNative()->CopyTextureRegion(&destination, 0, 0, 0, &source,
                                              nullptr);
      commands.End();
      device.SubmitAndPresent(swapchain);
      device.WaitIdle();

      const auto *pixels =
          static_cast<const unsigned char *>(device.MapBuffer(readback));
      const auto *inside = pixels + 32 * footprint.Footprint.RowPitch + 32 * 4;
      Require(inside[0] == 0 && inside[1] == (frame % 2 ? 255 : 0) &&
                  inside[2] == (frame % 2 ? 0 : 255),
              "ImGui rectangle pixel mismatch");
      const auto *clipped = pixels + 32 * footprint.Footprint.RowPitch + 56 * 4;
      Require(clipped[0] == 0 && clipped[1] == 0 && clipped[2] == 0,
              "ImGui scissor rectangle did not clip the draw");
      bool foundFontPixel = false;
      for (int y = 20; y < 50; ++y) {
        for (int x = 90; x < 230; ++x) {
          const auto *pixel = pixels + y * footprint.Footprint.RowPitch + x * 4;
          foundFontPixel |= pixel[0] > 32 && pixel[1] > 32 && pixel[2] > 32;
        }
      }
      Require(foundFontPixel, "Font atlas upload or text rendering failed");
      device.UnmapBuffer(readback);
      Validate(*validation.Get());
    }
    device.DestroyBuffer(readback);
    device.DestroyImageView(view);
    device.DestroyImage(target);
  }
  Require(ImGui::GetCurrentContext() == context &&
              !ImGui::GetIO().BackendRendererUserData,
          "GPU shutdown destroyed the context or left backend data alive");
  ImGui::DestroyContext();
  Validate(*validation.Get());
  device.DestroySwapchain(swapchain);
}

static void Win32Integration() {
  Iryven::Win32Window window(256, 128, "ImGui input test", true, false);
  Iryven::AssetUploadQueue uploads;
  Iryven::Renderer renderer(window, uploads, true);
  for (int iteration = 0; iteration < 2; ++iteration) {
    {
      Iryven::ImGuiIntegration integration(window, renderer);
      auto &io = ImGui::GetIO();
      io.IniFilename = nullptr;
      io.ConfigInputTrickleEventQueue = false;
      Require(std::strcmp(io.BackendPlatformName, "imgui_impl_win32") == 0,
              "Incorrect ImGui platform backend");
      const auto handle = static_cast<HWND>(window.GetNativeHandle());
      SendMessageW(handle, WM_KEYDOWN, 'A', 0x001e0001);
      SendMessageW(handle, WM_CHAR, 'X', 0);
      integration.BeginFrame();
      Require(ImGui::IsKeyDown(ImGuiKey_A),
              "Win32 key event did not reach ImGui");
      Require(io.InputQueueCharacters.contains('X'),
              "Win32 character event did not reach ImGui");
      Require(io.DisplaySize.x == 256 && io.DisplaySize.y == 128,
              "Win32 client size not supplied to ImGui");
      ImGui::Begin("Integration test");
      ImGui::TextUnformatted("Win32 input and D3D12 renderer");
      ImGui::End();
      integration.EndFrame();
    }
    Require(ImGui::GetCurrentContext() == nullptr,
            "ImGui context was not destroyed");
    SendMessageW(static_cast<HWND>(window.GetNativeHandle()), WM_CHAR, 'Y', 0);
  }
}

static void BindlessSamplers() {
  DX::Device device(
      {.graphicsAPI = GraphicsAPI::D3D12, .enableValidation = true});
  DX::ComPtr<ID3D12InfoQueue> validation;
  DX::ThrowIfFailed(
      device.GetNativeDevice()->QueryInterface(IID_PPV_ARGS(&validation)),
      "Get bindless sampler validation queue");
  validation->ClearStoredMessages();
  const auto image = device.CreateImage({.width = 1,
                                         .height = 1,
                                         .format = Format::RGBA8_UNORM,
                                         .usage = ImageUsage::Sampled});
  const auto view = device.CreateImageView({.image = image});
  {
    Iryven::BindlessTextureManager manager(device, 4096);
    std::vector<Iryven::BindlessTextureIndex> references;
    for (uint32_t index = 0; index < 4096; ++index) {
      const auto reference = manager.Register(
          view, {.addressU = index % 2 ? SamplerAddressMode::ClampToEdge
                                       : SamplerAddressMode::Repeat});
      Require(manager.TextureSlot(reference) == index,
              "Packed bindless reference lost its texture slot");
      Require(manager.SamplerSlot(reference) == index % 2,
              "Identical sampler settings were not deduplicated");
      references.push_back(reference);
    }
    const auto &set = device.GetBindingSet(manager.BindingSet());
    Require(set.values.at(0).size() == 4096 && set.values.at(1).size() == 128,
            "Texture and sampler capacities are not independent");
    for (uint32_t index = 1; index < references.size(); ++index)
      manager.Release(references[index], 5);
    manager.CollectGarbage(4);
    bool rejected = false;
    try {
      manager.Register(view, {});
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    Require(rejected, "Bindless slots reused before GPU retirement");
    manager.CollectGarbage(5);
    const auto recycled =
        manager.Register(view, {.addressV = SamplerAddressMode::ClampToEdge});
    Require(manager.TextureSlot(recycled) == 4095 &&
                manager.SamplerSlot(recycled) == 1,
            "Retired texture and sampler slots were not reused independently");
    // Slot zero pins the default sampler; every other entry is reclaimable.
    manager.Release(recycled, 6);
    manager.CollectGarbage(6);
  }
  device.DestroyImageView(view);
  device.DestroyImage(image);
  Validate(*validation.Get());
  std::cout << "PASS 4096 bindless references, shared sampler settings, packed "
               "indices and fence-delayed texture/sampler reuse\n";
}

static void CoordinateConventions() {
  Iryven::Win32Window window(32, 32, "Coordinate test", true, false);
  DX::Device device(
      {.graphicsAPI = GraphicsAPI::D3D12, .enableValidation = true});
  const auto swapchain = device.CreateSwapchain(
      {.window = {WindowHandleType::Win32, window.GetNativeHandle()},
       .width = 32,
       .height = 32});
  DX::ComPtr<ID3D12InfoQueue> validation;
  DX::ThrowIfFailed(
      device.GetNativeDevice()->QueryInterface(IID_PPV_ARGS(&validation)),
      "Get coordinate validation queue");
  validation->ClearStoredMessages();
  const auto compiled = Velos::ShaderCompiler::CompileFile(
      {.path = "tests/platform/coordinate_conventions.hlsl",
       .stage = ShaderStage::Compute,
       .language = Velos::ShaderSourceLanguage::HLSL,
       .outputFormat = Velos::ShaderBinaryFormat::Dxil});
  const auto shader = device.CreateShader({.stage = ShaderStage::Compute,
                                           .bytecode = compiled.dxil.data(),
                                           .bytecodeSize = compiled.dxil.size(),
                                           .reflection = compiled.reflection});
  const auto layout =
      device.BuildPipelineLayout(Velos::ShaderCompiler::MergeShaderReflection(
          std::span(&compiled.reflection, 1)));
  const auto pipeline = device.CreateComputePipeline(
      {.computeShader = shader,
       .layout = {layout.setLayouts.data(),
                  static_cast<Velos::u32>(layout.setLayouts.size())}});
  const BindingPoolSize poolSize{BindingType::StorageBuffer, 1};
  const auto pool = device.CreateBindingPool({&poolSize, 1, 1});
  const auto set = device.AllocateBindingSet({pool, layout.setLayouts[0]});
  const auto output = device.CreateBuffer(
      {.size = 32, .usage = BufferUsage::Storage | BufferUsage::TransferSrc});
  const auto readback =
      device.CreateBuffer({.size = 32,
                           .usage = BufferUsage::TransferDst,
                           .memoryUsage = MemoryUsage::GPUToCPU});
  const BindingBufferInfo bufferInfo{output, 0, 32};
  device.UpdateBindingSet({.dstSet = set,
                           .binding = 0,
                           .type = BindingType::StorageBuffer,
                           .bufferInfo = &bufferInfo});
  const glm::vec3 worldPoint(0.3f, 0.7f, -4.0f);
  for (int rotation = 0; rotation < 5; ++rotation) {
    const glm::vec3 eye(0.1f * rotation, 0.2f, 1.0f);
    const auto view =
        glm::rotate(glm::mat4(1), float(rotation - 2) * 0.15f,
                    glm::vec3(0, 1, 0)) *
        glm::lookAtRH(eye, eye + glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    const auto projection =
        glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    const glm::vec4 clip = projection * view * glm::vec4(worldPoint, 1);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    const glm::vec2 uv(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
    struct Constants {
      glm::mat4 inverseViewProjection;
      glm::vec4 screenUvAndDepth;
      glm::vec4 projectedNdc;
    } constants{glm::inverse(projection * view), glm::vec4(uv, ndc.z, 0),
                glm::vec4(ndc, 0)};
    Require(device.BeginFrame(swapchain).success, "Begin coordinate frame");
    auto &commands = device.GetCommandList();
    commands.Begin();
    commands.Barrier(BufferBarrier{.buffer = output,
                                   .newState = ResourceState::ShaderReadWrite});
    commands.BindComputePipeline(pipeline);
    commands.SetComputeBindings(pipeline, 0, set);
    commands.PushConstants(ShaderStage::Compute, 0, sizeof(constants),
                           &constants);
    commands.Dispatch(1, 1, 1);
    commands.Barrier(BufferBarrier{.buffer = output,
                                   .newState = ResourceState::TransferSrc});
    commands.CopyBuffer(output, readback, {0, 0, 32});
    commands.End();
    device.SubmitAndPresent(swapchain);
    device.WaitIdle();
    const auto *values = static_cast<const float *>(device.MapBuffer(readback));
    const float expected[]{ndc.x,        ndc.y,        uv.x,         uv.y,
                           worldPoint.x, worldPoint.y, worldPoint.z, 1};
    for (int component = 0; component < 8; ++component)
      Require(std::abs(values[component] - expected[component]) < 0.0002f,
              "Screen/world coordinates changed with camera rotation");
    device.UnmapBuffer(readback);
    Validate(*validation.Get());
  }
  device.DestroyBuffer(readback);
  device.DestroyBuffer(output);
  device.DestroyBindingPool(pool);
  device.DestroyPipeline(pipeline);
  for (const auto owned : layout.ownedSetLayouts)
    device.DestroyBindingLayout(owned);
  device.DestroyShader(shader);
  device.DestroySwapchain(swapchain);
  std::cout << "PASS D3D12 screen-Y, light projection and world-position "
               "reconstruction over five camera rotations\n";
}

int main() {
  try {
    Iryven::Log::Init();
    CoordinateConventions();
    BindlessSamplers();
    DrawReadback();
    std::cout << "PASS D3D12 ImGui geometry, font upload, pixel readback and "
                 "validation over five frames\n";
#ifdef IRYVEN_GRAPHICS_DX12
    Win32Integration();
    std::cout << "PASS Win32 ImGui input forwarding, context ownership and "
                 "repeated lifecycle\n";
#endif
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
