#include "glfw_window.h"
#include <GLFW/glfw3.h>
#include <stdexcept>

namespace Iryven {
    GlfwWindow::GlfwWindow(int width, int height, const std::string& title, bool resizable)
        : windowWidth_(width), windowHeight_(height), title_(title)
    {
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);

        window_ = glfwCreateWindow(windowWidth_, windowHeight_, title_.c_str(),
            nullptr, nullptr);

        glfwSetWindowUserPointer(window_, this);

        glfwGetWindowSize(window_, &windowWidth_, &windowHeight_);
        glfwGetFramebufferSize(window_, &framebufferWidth_, &framebufferHeight_);
    }
    GlfwWindow::~GlfwWindow()
    {
    }
    void GlfwWindow::PollEvents()
    {
    }
    bool GlfwWindow::ShouldClose() const
    {
        return false;
    }
    int GlfwWindow::GetWidth() const
    {
        return 0;
    }
    int GlfwWindow::GetHeight() const
    {
        return 0;
    }
    int GlfwWindow::GetFramebufferWidth() const
    {
        return 0;
    }
    int GlfwWindow::GetFramebufferHeight() const
    {
        return 0;
    }
    bool GlfwWindow::WasFramebufferResized() const
    {
        return false;
    }
    void GlfwWindow::ResetFramebufferResizedFlag()
    {
    }
    const std::string& GlfwWindow::GetTitle() const
    {
        return title_;
    }
    void* GlfwWindow::GetNativeHandle() const
    {
        return nullptr;
    }
}