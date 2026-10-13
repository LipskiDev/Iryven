#include <rhi/device.h>
#include <rhi/command_list.h>
#include <rhi/resources.h>
#include <rhi/types.h>
#include <shader/shader_compiler.h>

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace {

using namespace Velos;
using namespace Velos::RHI;

struct DeviceDeleter {
    void operator()(IDevice* device) const noexcept {
        if (device) {
            DestroyDevice(device);
        }
    }
};

using DevicePtr = std::unique_ptr<IDevice, DeviceDeleter>;

class Win32Window final {
public:
    Win32Window(const wchar_t* title, int width, int height)
        : instance_(GetModuleHandleW(nullptr)) {
        WNDCLASSEXW windowClass{
            .cbSize = sizeof(WNDCLASSEXW),
            .style = CS_HREDRAW | CS_VREDRAW,
            .lpfnWndProc = WindowProcedure,
            .hInstance = instance_,
            .hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)),
            .lpszClassName = kWindowClassName,
        };

        if (!RegisterClassExW(&windowClass)) {
            throw std::runtime_error("Failed to register the Sandbox window class");
        }
        classRegistered_ = true;

        RECT windowRect{0, 0, width, height};
        if (!AdjustWindowRectEx(
                &windowRect, WS_OVERLAPPEDWINDOW, FALSE, 0)) {
            throw std::runtime_error("Failed to calculate the Sandbox window size");
        }

        handle_ = CreateWindowExW(
            0,
            kWindowClassName,
            title,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            nullptr,
            nullptr,
            instance_,
            this);
        if (!handle_) {
            throw std::runtime_error("Failed to create the Sandbox window");
        }

        ShowWindow(handle_, SW_SHOW);
    }

    ~Win32Window() {
        if (handle_) {
            DestroyWindow(handle_);
        }
        if (classRegistered_) {
            UnregisterClassW(kWindowClassName, instance_);
        }
    }

    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;

    bool ProcessMessages() {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                return false;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return true;
    }

    HWND GetHandle() const noexcept {
        return handle_;
    }

    Extent2D GetClientExtent() const noexcept {
        RECT clientRect{};
        if (!handle_ || !GetClientRect(handle_, &clientRect)) {
            return {};
        }

        return {
            .width = static_cast<u32>(clientRect.right - clientRect.left),
            .height = static_cast<u32>(clientRect.bottom - clientRect.top),
        };
    }

private:
    static LRESULT CALLBACK WindowProcedure(
        HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<Win32Window*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));

        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            self = static_cast<Win32Window*>(create->lpCreateParams);
            SetWindowLongPtrW(
                window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }

        switch (message) {
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            if (self) {
                self->handle_ = nullptr;
            }
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }

    static constexpr const wchar_t* kWindowClassName = L"VelosSandboxWindow";

    HINSTANCE instance_ = nullptr;
    HWND handle_ = nullptr;
    bool classRegistered_ = false;
};

class Sandbox final {
public:
    Sandbox()
        : window_(L"Velos D3D12 Sandbox", 1920, 1080),
          device_(CreateDevice({
              .graphicsAPI = GraphicsAPI::D3D12,
              .enableValidation = true,
              .applicationName = "Velos D3D12 Sandbox",
          })) {
        if (!device_) {
            throw std::runtime_error("Failed to create the Velos D3D12 device");
        }

        const Extent2D extent = window_.GetClientExtent();
        if (extent.width == 0 || extent.height == 0) {
            throw std::runtime_error(
                "Cannot create a swapchain for a zero-sized window");
        }

        framebufferWidth_ = extent.width;
        framebufferHeight_ = extent.height;
        swapchain_ = device_->CreateSwapchain({
            .window = {
                .type = WindowHandleType::Win32,
                .value = window_.GetHandle(),
            },
            .width = framebufferWidth_,
            .height = framebufferHeight_,
            .format = Format::BGRA8_UNORM,
            .bufferCount = 2,
            .vsync = true,
            .debugName = "Velos D3D12 Sandbox swapchain",
        });

        if (!swapchain_.IsValid()) {
            throw std::runtime_error("Failed to create the Velos D3D12 swapchain");
        }
        CompileShaders();
        CreatePipeline();
        CreateDepthResources();
    }

    ~Sandbox() {
        if (!device_) {
            return;
        }

        device_->WaitIdle();
        DestroyDepthResources();
        DestroyPipeline();
        DestroyShaders();
        if (swapchain_.IsValid()) {
            device_->DestroySwapchain(swapchain_);
        }
    }

    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;

    void Run() {
        while (window_.ProcessMessages()) {
            if (!PrepareFramebuffer()) {
                WaitMessage();
                continue;
            }

            DrawFrame();
        }
    }

private:
    void CompileShaders() {
        const auto createShader = [this](ShaderStage stage,
                                         const char* entryPoint,
                                         const char* debugName) {
            const ShaderCompileOutput compiled = ShaderCompiler::CompileFile({
                .path = "sandbox/basic.hlsl",
                .stage = stage,
                .entryPoint = entryPoint,
                .language = ShaderSourceLanguage::HLSL,
                .outputFormat = ShaderBinaryFormat::Dxil,
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

        vertexShader_ = createShader(
            ShaderStage::Vertex, "VSmain", "Sandbox vertex shader");
        try {
            fragmentShader_ = createShader(
                ShaderStage::Fragment, "PSmain", "Sandbox pixel shader");
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
        pipeline_ = device_->CreateGraphicsPipeline({
            .vertexShader = vertexShader_,
            .fragmentShader = fragmentShader_,
            .topology = PrimitiveTopology::TriangleList,
            .raster = {
                .cullBackFaces = false,
                .frontFaceCCW = true,
                .wireframe = false,
            },
            .depth = {
                .depthTestEnable = true,
                .depthWriteEnable = true,
                .depthFormat = Format::D32_FLOAT,
            },
            .colorFormat = Format::BGRA8_UNORM,
            .debugName = "Velos D3D12 Sandbox pipeline",
        });
    }

    void DestroyPipeline() {
        if (pipeline_.IsValid()) {
            device_->DestroyPipeline(pipeline_);
            pipeline_ = {};
        }
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

        ICommandList& commandList = device_->GetCommandList(QueueType::Graphics);
        commandList.Begin();
        commandList.Barrier({
            .image = frame.backbufferImage,
            .oldLayout = ImageLayout::Present,
            .newLayout = ImageLayout::ColorAttachment,
        });
        commandList.Barrier({
            .image = depthImage_,
            .oldLayout = device_->GetImageLayout(depthImage_, 0),
            .newLayout = ImageLayout::DepthAttachment,
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
            .clearValue = {0.52f, 0.04f, 0.08f, 1.0f},
        };
        const DepthAttachmentDesc depthAttachment{
            .view = depthView_,
            .loadOp = LoadOp::Clear,
            .storeOp = StoreOp::DontCare,
            .clearDepth = 1.0f,
        };
        commandList.BeginRendering({
            .renderArea = {
                .extent = {framebufferWidth_, framebufferHeight_},
            },
            .colorAttachments = &colorAttachment,
            .colorAttachmentCount = 1,
            .depthAttachment = &depthAttachment,
        });
        commandList.BindPipeline(pipeline_);
        commandList.Draw(3);
        commandList.EndRendering();
        commandList.Barrier({
            .image = frame.backbufferImage,
            .oldLayout = ImageLayout::ColorAttachment,
            .newLayout = ImageLayout::Present,
        });
        commandList.End();
        device_->SubmitAndPresent(swapchain_);
    }

    void CreateDepthResources() {
        depthImage_ = device_->CreateImage({
            .width = framebufferWidth_,
            .height = framebufferHeight_,
            .format = Format::D32_FLOAT,
            .usage = ImageUsage::DepthStencil,
            .debugName = "Velos D3D12 Sandbox depth image",
        });
        depthView_ = device_->CreateImageView({
            .image = depthImage_,
            .format = Format::D32_FLOAT,
            .aspect = ImageAspect::Depth,
            .debugName = "Velos D3D12 Sandbox depth view",
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

    Win32Window window_;
    DevicePtr device_;
    SwapchainHandle swapchain_{};
    ImageHandle depthImage_{};
    ImageViewHandle depthView_{};
    ShaderHandle vertexShader_{};
    ShaderHandle fragmentShader_{};
    PipelineHandle pipeline_{};
    u32 framebufferWidth_ = 0;
    u32 framebufferHeight_ = 0;
};

} // namespace

int main() {
    try {
        Sandbox sandbox;
        sandbox.Run();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Velos Sandbox failed: " << exception.what() << '\n';
        return 1;
    }
}

#else

int main() {
    std::cerr << "The D3D12 Sandbox is only available on Windows.\n";
    return 1;
}

#endif
