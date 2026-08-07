#include "renderer.h"

#include <stdexcept>

namespace Iryven {

	Renderer::Renderer(Window& window) : window_(window)
	{
		device_.reset(Velos::RHI::CreateDevice({
			.graphicsAPI = Velos::RHI::GraphicsAPI::Vulkan,
			.enableValidation = true,
			.applicationName = window_.GetTitle().c_str(),
		}));

		if (!device_) {
			throw std::runtime_error("Failed to create rendering device");
		}

		const int width = window_.GetFramebufferWidth();
		const int height = window_.GetFramebufferHeight();
		if (width <= 0 || height <= 0) {
			throw std::runtime_error("Cannot create a swapchain for a zero-sized framebuffer");
		}

		swapchain_ = device_->CreateSwapchain({
			.windowHandle = window_.GetNativeHandle(),
			.width = static_cast<Velos::u32>(width),
			.height = static_cast<Velos::u32>(height),
			.bufferCount = 2,
			.vsync = window_.IsVSync(),
			.debugName = "Iryven main swapchain",
		});

		if (!swapchain_) {
			throw std::runtime_error("Failed to create rendering swapchain");
		}
	}

	Renderer::~Renderer()
	{
		if (!device_) {
			return;
		}

		device_->WaitIdle();

		if (swapchain_) {
			device_->DestroySwapchain(swapchain_);
		}
	}

	bool Renderer::BeginFrame()
	{
		const int width = window_.GetFramebufferWidth();
		const int height = window_.GetFramebufferHeight();

		if (width <= 0 || height <= 0) {
			return false;
		}

		const auto dimensions = device_->GetSwapchainDimensions();
		if (swapchainDirty_ || dimensions.width != static_cast<Velos::u32>(width) ||
			dimensions.height != static_cast<Velos::u32>(height)) {
			device_->ResizeSwapchain(
				swapchain_,
				static_cast<Velos::u32>(width),
				static_cast<Velos::u32>(height));
			swapchainDirty_ = false;
		}

		frame_ = device_->BeginFrame(swapchain_);
		if (!frame_.success) {
			swapchainDirty_ = true;
			return false;
		}

		auto& commands = device_->GetCommandList();
		commands.Begin();
		commands.Barrier({
			.image = frame_.backbufferImage,
			.newLayout = Velos::RHI::ImageLayout::ColorAttachment,
			.aspect = Velos::RHI::ImageAspect::Color,
		});

		frameActive_ = true;
		return true;
	}

	void Renderer::Clear(const Color& color)
	{
		if (!frameActive_) {
			throw std::logic_error("Renderer::Clear called outside an active frame");
		}

		const auto dimensions = device_->GetSwapchainDimensions();
		const Velos::RHI::ColorAttachmentDesc attachment{
			.view = frame_.backbuffer,
			.loadOp = Velos::RHI::LoadOp::Clear,
			.storeOp = Velos::RHI::StoreOp::Store,
			.clearValue = {color.R(), color.G(), color.B(), color.A()},
		};

		auto& commands = device_->GetCommandList();
		commands.BeginRendering({
			.renderArea = {{0, 0}, dimensions},
			.colorAttachments = &attachment,
			.colorAttachmentCount = 1,
			.depthAttachment = nullptr,
		});
		commands.EndRendering();
	}

	void Renderer::EndFrame()
	{
		if (!frameActive_) {
			return;
		}

		auto& commands = device_->GetCommandList();
		commands.Barrier({
			.image = frame_.backbufferImage,
			.newLayout = Velos::RHI::ImageLayout::Present,
			.aspect = Velos::RHI::ImageAspect::Color,
		});
		commands.End();

		device_->SubmitAndPresent(swapchain_);
		frameActive_ = false;
	}

}
