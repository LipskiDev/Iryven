#include <rhi/device.h>
#include <rhi/command_list.h>
#include <rhi/resources.h>
#include <rhi/types.h>
#include <shader/shader_compiler.h>
#include <iryven/graphics_backend.h>

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <cstring>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include "duck_model.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace {

using namespace Velos;
using namespace Velos::RHI;

struct DeviceDeleter {
  void operator()(IDevice *device) const noexcept {
    if (device) {
      DestroyDevice(device);
    }
  }
};

using DevicePtr = std::unique_ptr<IDevice, DeviceDeleter>;

class SandboxWindow final {
 public:
  SandboxWindow(const char *title, int width, int height) {
    if (!glfwInit())
      throw std::runtime_error("Failed to initialize GLFW");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    handle_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!handle_) {
      glfwTerminate();
      throw std::runtime_error("Failed to create the Sandbox window");
    }
  }
  ~SandboxWindow() {
    glfwDestroyWindow(handle_);
    glfwTerminate();
  }
  SandboxWindow(const SandboxWindow &) = delete;
  SandboxWindow &operator=(const SandboxWindow &) = delete;
  bool ProcessMessages() {
    glfwPollEvents();
    return !glfwWindowShouldClose(handle_);
  }
  WindowHandle GetHandle(GraphicsAPI api) const {
    if (api == GraphicsAPI::Vulkan)
      return {WindowHandleType::GLFW, handle_};
    return {WindowHandleType::Win32, glfwGetWin32Window(handle_)};
  }
  Extent2D GetClientExtent() const {
    int width = 0, height = 0;
    glfwGetFramebufferSize(handle_, &width, &height);
    return {static_cast<u32>(width), static_cast<u32>(height)};
  }

 private:
  GLFWwindow *handle_ = nullptr;
};
class Sandbox final {
 public:
  Sandbox()
      : window_(Iryven::GetBuildGraphicsBackend() == GraphicsAPI::D3D12
                    ? "Velos Sandbox - DX12"
                    : "Velos Sandbox - Vulkan",
                1920, 1080),
        device_(CreateDevice({
            .graphicsAPI = Iryven::GetBuildGraphicsBackend(),
            .enableValidation = true,
            .applicationName = "Velos Sandbox",
        })) {
    if (!device_) {
      throw std::runtime_error("Failed to create the Velos device");
    }

    const Extent2D extent = window_.GetClientExtent();
    if (extent.width == 0 || extent.height == 0) {
      throw std::runtime_error(
          "Cannot create a swapchain for a zero-sized window");
    }

    framebufferWidth_ = extent.width;
    framebufferHeight_ = extent.height;
    swapchain_ = device_->CreateSwapchain({
        .window = window_.GetHandle(device_->GetBackend()),
        .width = framebufferWidth_,
        .height = framebufferHeight_,
        .format = Format::BGRA8_UNORM,
        .bufferCount = 2,
        .vsync = true,
        .debugName = "Velos Sandbox swapchain",
    });

    if (!swapchain_.IsValid()) {
      throw std::runtime_error("Failed to create the Velos D3D12 swapchain");
    }
    CompileShaders();
    CreatePipeline();
    CreateDepthResources();
    CreateResources();
  }

  ~Sandbox() {
    if (!device_) {
      return;
    }

    device_->WaitIdle();
    DestroyResources();
    DestroyDepthResources();
    DestroyPipeline();
    DestroyShaders();
    if (swapchain_.IsValid()) {
      device_->DestroySwapchain(swapchain_);
    }
  }

  Sandbox(const Sandbox &) = delete;
  Sandbox &operator=(const Sandbox &) = delete;

  void Run(bool smokeTest = false) {
    u32 renderedFrames = 0;
    while (window_.ProcessMessages()) {
      if (!PrepareFramebuffer()) {
        glfwWaitEvents();
        continue;
      }

      DrawFrame();
      if (smokeTest && ++renderedFrames == 3)
        break;
    }
  }

 private:
  void CompileShaders() {
    const auto createShader = [this](ShaderStage stage, const char *entryPoint,
                                     const char *debugName) {
      const bool dx12 = device_->GetBackend() == GraphicsAPI::D3D12;
      const ShaderCompileOutput compiled = ShaderCompiler::CompileFile({
          .path = "sandbox/basic.hlsl",
          .stage = stage,
          .entryPoint = entryPoint,
          .language = ShaderSourceLanguage::HLSL,
          .outputFormat =
              dx12 ? ShaderBinaryFormat::Dxil : ShaderBinaryFormat::Spirv,
      });
      const std::span<const std::byte> bytecode = compiled.GetBytecode();
      return device_->CreateShader({
          .stage = stage,
          .bytecode = bytecode.data(),
          .bytecodeSize = bytecode.size(),
          .entryPoint = entryPoint,
          .reflection = compiled.reflection,
          .debugName = debugName,
      });
    };

    vertexShader_ =
        createShader(ShaderStage::Vertex, "VSmain", "Sandbox vertex shader");
    try {
      fragmentShader_ =
          createShader(ShaderStage::Fragment, "PSmain", "Sandbox pixel shader");
    } catch (...) {
      device_->DestroyShader(vertexShader_);
      vertexShader_ = {};
      throw;
    }
  }

  void DestroyShaders() {
    if (fragmentShader_.IsValid()) {
      device_->DestroyShader(fragmentShader_);
      fragmentShader_ = {};
    }
    if (vertexShader_.IsValid()) {
      device_->DestroyShader(vertexShader_);
      vertexShader_ = {};
    }
  }

  void CreatePipeline() {
    const BindingDesc textureBinding{.binding = 0,
                                     .type = BindingType::CombinedImageSampler,
                                     .visibility = ShaderStage::Fragment};
    textureLayout_ = device_->CreateBindingLayout({&textureBinding, 1});
    pipeline_ = device_->CreateGraphicsPipeline({
        .vertexShader = vertexShader_,
        .fragmentShader = fragmentShader_,
        .vertexLayouts = {{
            .stride = sizeof(DuckVertex),
            .attributes = {{
                               .location = 0,
                               .binding = 0,
                               .format = VertexFormat::Float32x3,
                               .offset = offsetof(DuckVertex, position),
                           },
                           {
                               .location = 1,
                               .binding = 0,
                               .format = VertexFormat::Float32x2,
                               .offset = offsetof(DuckVertex, uv),
                               .semanticIndex = 1,
                           }},
        }},
        .layout = {&textureLayout_, 1},
        .topology = PrimitiveTopology::TriangleList,
        .raster =
            {
                .cullBackFaces = false,
                .frontFaceCCW = true,
                .wireframe = false,
            },
        .depth =
            {
                .depthTestEnable = true,
                .depthWriteEnable = true,
                .depthFormat = Format::D32_FLOAT,
            },
        .colorFormat = Format::BGRA8_UNORM,
        .debugName = "Velos Sandbox pipeline",
    });
  }

  void DestroyPipeline() {
    if (pipeline_.IsValid()) {
      device_->DestroyPipeline(pipeline_);
      pipeline_ = {};
    }
    if (textureLayout_.IsValid())
      device_->DestroyBindingLayout(textureLayout_);
    textureLayout_ = {};
  }

  bool PrepareFramebuffer() {
    const Extent2D extent = window_.GetClientExtent();
    if (extent.width == 0 || extent.height == 0) {
      return false;
    }

    if (extent.width != framebufferWidth_ ||
        extent.height != framebufferHeight_) {
      device_->ResizeSwapchain(swapchain_, extent.width, extent.height);
      framebufferWidth_ = extent.width;
      framebufferHeight_ = extent.height;
      DestroyDepthResources();
      CreateDepthResources();
    }

    return true;
  }

  void DrawFrame() {
    const FrameBeginResult frame = device_->BeginFrame(swapchain_);
    if (!frame.success) {
      return;
    }

    ICommandList &commandList = device_->GetCommandList(QueueType::Graphics);
    commandList.Begin();
    if (textureUpload_.IsValid()) {
      commandList.Barrier(ImageBarrier{.image = baseColorImage_,
                                       .newLayout = ImageLayout::TransferDst});
      commandList.CopyBufferToImage(
          textureUpload_, baseColorImage_,
          {
              .bufferRowLength = textureRowPixels_,
              .imageExtent = {textureWidth_, textureHeight_, 1},
          });
      commandList.Barrier(
          ImageBarrier{.image = baseColorImage_,
                       .oldLayout = ImageLayout::TransferDst,
                       .newLayout = ImageLayout::ShaderReadOnly});
    }
    commandList.Barrier({
        .image = frame.backbufferImage,
        .oldLayout = device_->GetImageLayout(frame.backbufferImage, 0),
        .newLayout = ImageLayout::ColorAttachment,
    });
    commandList.Barrier({
        .image = depthImage_,
        .oldLayout = device_->GetImageLayout(depthImage_, 0),
        .newLayout = ImageLayout::DepthAttachment,
        .aspect = ImageAspect::Depth,
    });
    commandList.SetViewport({
        .width = static_cast<float>(framebufferWidth_),
        .height = static_cast<float>(framebufferHeight_),
    });
    commandList.SetScissor({
        .extent = {framebufferWidth_, framebufferHeight_},
    });

    const ColorAttachmentDesc colorAttachment{
        .view = frame.backbuffer,
        .loadOp = LoadOp::Clear,
        .storeOp = StoreOp::Store,
        .clearValue = {0.055f, 0.075f, 0.11f, 1.0f},
    };
    const DepthAttachmentDesc depthAttachment{
        .view = depthView_,
        .loadOp = LoadOp::Clear,
        .storeOp = StoreOp::DontCare,
        .clearDepth = 1.0f,
    };
    commandList.BeginRendering({
        .renderArea =
            {
                .extent = {framebufferWidth_, framebufferHeight_},
            },
        .colorAttachments = &colorAttachment,
        .colorAttachmentCount = 1,
        .depthAttachment = &depthAttachment,
    });
    commandList.BindPipeline(pipeline_);
    commandList.SetBindings(pipeline_, 0, textureSet_);
    glm::mat4 projection = glm::perspectiveRH_ZO(
        glm::radians(45.0f),
        static_cast<float>(framebufferWidth_) / framebufferHeight_, 0.1f,
        100.0f);
    if (device_->GetBackend() == GraphicsAPI::Vulkan)
      projection[1][1] *= -1.0f;
    const glm::mat4 view =
        glm::lookAtRH(glm::vec3(2.4f, 1.25f, 3.0f), glm::vec3(0.0f),
                      glm::vec3(0.0f, 1.0f, 0.0f));
    const struct DrawData {
      glm::mat4 viewProjection;
      glm::vec4 baseColorFactor;
    } drawData{projection * view, baseColorFactor_};
    static_assert(sizeof(DrawData) == 80);
    commandList.PushConstants(ShaderStage::Vertex | ShaderStage::Fragment, 0,
                              sizeof(drawData), &drawData);
    commandList.BindVertexBuffer(0, vertexBuffer_);
    commandList.BindIndexBuffer(indexBuffer_, IndexType::U32);
    commandList.DrawIndexed(indexCount_);
    commandList.EndRendering();
    commandList.Barrier({
        .image = frame.backbufferImage,
        .oldLayout = ImageLayout::ColorAttachment,
        .newLayout = ImageLayout::Present,
    });
    commandList.End();
    device_->SubmitAndPresent(swapchain_);
    if (textureUpload_.IsValid()) {
      device_->WaitIdle();
      device_->DestroyBuffer(textureUpload_);
      textureUpload_ = {};
    }
  }

  void CreateDepthResources() {
    depthImage_ = device_->CreateImage({
        .width = framebufferWidth_,
        .height = framebufferHeight_,
        .format = Format::D32_FLOAT,
        .usage = ImageUsage::DepthStencil,
        .debugName = "Velos Sandbox depth image",
    });
    depthView_ = device_->CreateImageView({
        .image = depthImage_,
        .format = Format::D32_FLOAT,
        .aspect = ImageAspect::Depth,
        .debugName = "Velos Sandbox depth view",
    });
  }

  void DestroyDepthResources() {
    if (depthView_.IsValid()) {
      device_->DestroyImageView(depthView_);
      depthView_ = {};
    }
    if (depthImage_.IsValid()) {
      device_->DestroyImage(depthImage_);
      depthImage_ = {};
    }
  }

  void CreateResources() {
    const DuckModel model = LoadDuckModel();
    const auto &vertices = model.vertices;
    const auto &indices = model.indices;
    indexCount_ = static_cast<u32>(indices.size());
    baseColorFactor_ = model.baseColor;

    vertexBuffer_ = device_->CreateBuffer({
        .size = sizeof(DuckVertex) * vertices.size(),
        .usage = BufferUsage::Vertex,
        .memoryUsage = MemoryUsage::CPUToGPU,
        .initialData = vertices.data(),
        .debugName = "Velos Sandbox vertex buffer",
    });

    indexBuffer_ = device_->CreateBuffer({
        .size = sizeof(uint32_t) * indices.size(),
        .usage = BufferUsage::Index,
        .memoryUsage = MemoryUsage::CPUToGPU,
        .initialData = indices.data(),
        .debugName = "Velos Sandbox index buffer",
    });
    textureWidth_ = model.width;
    textureHeight_ = model.height;
    const u32 rowPitch = (model.width * 4 + 255) & ~255u;
    textureRowPixels_ = rowPitch / 4;
    std::vector<unsigned char> padded(static_cast<size_t>(rowPitch) *
                                      model.height);
    for (u32 row = 0; row < model.height; ++row)
      std::memcpy(
          padded.data() + static_cast<size_t>(row) * rowPitch,
          model.pixels.data() + static_cast<size_t>(row) * model.width * 4,
          model.width * 4);
    textureUpload_ =
        device_->CreateBuffer({.size = padded.size(),
                               .usage = BufferUsage::TransferSrc,
                               .memoryUsage = MemoryUsage::CPUToGPU,
                               .initialData = padded.data(),
                               .debugName = "Duck texture upload"});
    baseColorImage_ = device_->CreateImage(
        {.width = model.width,
         .height = model.height,
         .format = Format::RGBA8_SRGB,
         .usage = ImageUsage::TransferDst | ImageUsage::Sampled,
         .debugName = "Duck base-color texture"});
    baseColorView_ = device_->CreateImageView(
        {.image = baseColorImage_, .format = Format::RGBA8_SRGB});
    baseColorSampler_ =
        device_->CreateSampler({.debugName = "Duck base-color sampler"});
    const BindingPoolSize budget{BindingType::CombinedImageSampler, 1};
    texturePool_ = device_->CreateBindingPool({&budget, 1, 1});
    textureSet_ = device_->AllocateBindingSet({texturePool_, textureLayout_});
    const BindingImageInfo imageInfo{baseColorSampler_, baseColorView_};
    device_->UpdateBindingSet({.dstSet = textureSet_,
                               .binding = 0,
                               .type = BindingType::CombinedImageSampler,
                               .imageInfo = &imageInfo});
    std::cout << "Loaded Duck.glb: " << vertices.size() << " vertices, "
              << indices.size() << " indices, " << model.width << 'x'
              << model.height << " base-color texture\n";
  }

  void DestroyResources() {
    if (texturePool_.IsValid())
      device_->DestroyBindingPool(texturePool_);
    if (baseColorSampler_.IsValid())
      device_->DestroySampler(baseColorSampler_);
    if (baseColorView_.IsValid())
      device_->DestroyImageView(baseColorView_);
    if (baseColorImage_.IsValid())
      device_->DestroyImage(baseColorImage_);
    if (textureUpload_.IsValid())
      device_->DestroyBuffer(textureUpload_);
    if (indexBuffer_.IsValid())
      device_->DestroyBuffer(indexBuffer_);
    if (vertexBuffer_.IsValid())
      device_->DestroyBuffer(vertexBuffer_);
    indexBuffer_ = {};
    vertexBuffer_ = {};
  }

  SandboxWindow window_;
  DevicePtr device_;
  SwapchainHandle swapchain_{};
  ImageHandle depthImage_{};
  ImageViewHandle depthView_{};
  ShaderHandle vertexShader_{};
  ShaderHandle fragmentShader_{};
  PipelineHandle pipeline_{};
  u32 framebufferWidth_ = 0;
  u32 framebufferHeight_ = 0;

  BufferHandle vertexBuffer_{};
  BufferHandle indexBuffer_{};
  BufferHandle textureUpload_{};
  ImageHandle baseColorImage_{};
  ImageViewHandle baseColorView_{};
  SamplerHandle baseColorSampler_{};
  BindingLayoutHandle textureLayout_{};
  BindingPoolHandle texturePool_{};
  BindingSetHandle textureSet_{};
  glm::vec4 baseColorFactor_{1.0f};
  u32 indexCount_ = 0;
  u32 textureWidth_ = 0, textureHeight_ = 0, textureRowPixels_ = 0;
};

}  // namespace

int main(int argc, char **argv) {
  try {
    Sandbox sandbox;
    sandbox.Run(argc > 1 && std::strcmp(argv[1], "--smoke-test") == 0);
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << "Velos Sandbox failed: " << exception.what() << '\n';
    return 1;
  }
}

#else

int main() {
  std::cerr << "The Sandbox is only available on Windows.\n";
  return 1;
}

#endif
