#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include <iryven/rendering/render_scene.h>
#include <rhi/command_list.h>
#include <rhi/device.h>
#include <rhi/handles.h>
#include <rhi/types.h>

namespace Iryven {

using FrameGraphHandle = std::uint32_t;
inline constexpr FrameGraphHandle InvalidFrameGraphHandle =
    std::numeric_limits<FrameGraphHandle>::max();

struct FrameGraphResourceHandle {
    FrameGraphHandle handle = InvalidFrameGraphHandle;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return handle != InvalidFrameGraphHandle;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return IsValid();
    }

    friend constexpr bool operator==(const FrameGraphResourceHandle&,
                                     const FrameGraphResourceHandle&) = default;
};

struct FrameGraphNodeHandle {
    FrameGraphHandle handle = InvalidFrameGraphHandle;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return handle != InvalidFrameGraphHandle;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return IsValid();
    }

    friend constexpr bool operator==(const FrameGraphNodeHandle&,
                                     const FrameGraphNodeHandle&) = default;
};

enum class RenderPassOperation {
    DontCare,
    Load,
    Clear,
};

enum class FrameGraphResourceType {
    Texture,
    Buffer,
    Attachment,
    Reference,
};

struct FrameGraphBufferInfo {
    std::size_t size = 0;
    Velos::RHI::BufferUsage usage{};
    Velos::RHI::BufferHandle handle{};
};

struct FrameGraphTextureInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 1;
    Velos::RHI::Format format{};
    Velos::RHI::ImageUsage usage{};
    RenderPassOperation loadOp = RenderPassOperation::DontCare;
    Velos::RHI::ClearColor clearColor{};
    float clearDepth = 1.0f;
    std::uint32_t clearStencil = 0;
    Velos::RHI::ImageHandle handle{};
    Velos::RHI::ImageViewHandle view{};
    bool resizeWithSwapchain = false;
};

using FrameGraphResourceInfo =
    std::variant<std::monostate, FrameGraphBufferInfo, FrameGraphTextureInfo>;

struct FrameGraphResource {
    FrameGraphResourceType type = FrameGraphResourceType::Texture;
    FrameGraphResourceInfo info{};
    bool external = false;
    FrameGraphNodeHandle producer{};
    FrameGraphResourceHandle outputHandle{};
    std::uint32_t referenceCount = 0;
    std::string name;
};

struct FrameGraphResourceInputCreation {
    FrameGraphResourceType type = FrameGraphResourceType::Texture;
    FrameGraphResourceInfo info{};
    bool external = false;
    std::string name;
};

struct FrameGraphResourceOutputCreation {
    FrameGraphResourceType type = FrameGraphResourceType::Texture;
    FrameGraphResourceInfo info{};
    bool external = false;
    std::string name;
};

struct FrameGraphNodeCreation {
    std::string name;
    std::vector<FrameGraphResourceInputCreation> inputs;
    std::vector<FrameGraphResourceOutputCreation> outputs;
    bool enabled = true;
};

struct FrameGraphRenderPass {
    virtual ~FrameGraphRenderPass() = default;
    virtual void AddUI() = 0;
    virtual void PreRender(Velos::RHI::ICommandList& commandList,
                           const RenderScene& scene) = 0;
    virtual void Render(Velos::RHI::ICommandList& commandList,
                        const RenderScene& scene) = 0;
    virtual void OnResize(Velos::RHI::IDevice& device,
                          std::uint32_t width,
                          std::uint32_t height) = 0;
};

struct FrameGraphNode {
    std::uint32_t referenceCount = 0;
    FrameGraphRenderPass* graphRenderPass = nullptr; // Non-owning.
    std::vector<FrameGraphResourceHandle> inputs;
    std::vector<FrameGraphResourceHandle> outputs;
    std::vector<FrameGraphNodeHandle> edges;
    bool enabled = true;
    std::string name;
};

class FrameGraphBuilder {
public:
    static constexpr std::uint32_t kMaxNodes = 1024;
    static constexpr std::uint32_t kMaxResources = 1024;
    static constexpr std::uint32_t kMaxRenderPasses = 256;

    void Init(Velos::RHI::IDevice& device);
    void Reset();
    void Shutdown();

    // The caller retains ownership and must keep the pass alive until Shutdown().
    void RegisterRenderPass(std::string_view name, FrameGraphRenderPass& renderPass);

    [[nodiscard]] FrameGraphResourceHandle CreateNodeOutput(
        const FrameGraphResourceOutputCreation& creation,
        FrameGraphNodeHandle producer);
    [[nodiscard]] FrameGraphResourceHandle CreateNodeInput(
        const FrameGraphResourceInputCreation& creation);
    [[nodiscard]] FrameGraphNodeHandle CreateNode(const FrameGraphNodeCreation& creation);

    [[nodiscard]] FrameGraphNode* GetNode(std::string_view name);
    [[nodiscard]] const FrameGraphNode* GetNode(std::string_view name) const;
    [[nodiscard]] FrameGraphNode* AccessNode(FrameGraphNodeHandle handle);
    [[nodiscard]] const FrameGraphNode* AccessNode(FrameGraphNodeHandle handle) const;

    [[nodiscard]] FrameGraphResource* GetResource(std::string_view name);
    [[nodiscard]] const FrameGraphResource* GetResource(std::string_view name) const;
    [[nodiscard]] FrameGraphResource* AccessResource(FrameGraphResourceHandle handle);
    [[nodiscard]] const FrameGraphResource* AccessResource(
        FrameGraphResourceHandle handle) const;

    [[nodiscard]] Velos::RHI::IDevice& Device() const;

private:
    friend class FrameGraph;

    void DestroyOwnedResources();
    void AllocateResource(FrameGraphResource& resource);
    void RecreateResizableResources(std::uint32_t width, std::uint32_t height);

    Velos::RHI::IDevice* device_ = nullptr; // Non-owning.
    std::vector<FrameGraphNode> nodes_;
    std::vector<FrameGraphResource> resources_;
    std::unordered_map<std::string, FrameGraphNodeHandle> nodeMap_;
    std::unordered_map<std::string, FrameGraphResourceHandle> resourceMap_;
    std::unordered_map<std::string, FrameGraphRenderPass*> renderPassMap_;
};

class FrameGraph {
public:
    void Init(FrameGraphBuilder& builder);
    void Shutdown();
    void Parse(const std::filesystem::path& path);
    void Reset();
    void EnableRenderPass(std::string_view renderPassName);
    void DisableRenderPass(std::string_view renderPassName);
    void Compile();
    void AddUI();
    void Render(Velos::RHI::ICommandList& commandList, const RenderScene& scene);
    void OnResize(Velos::RHI::IDevice& device,
                  std::uint32_t width,
                  std::uint32_t height);

    [[nodiscard]] FrameGraphNode* GetNode(std::string_view name);
    [[nodiscard]] const FrameGraphNode* GetNode(std::string_view name) const;
    [[nodiscard]] FrameGraphNode* AccessNode(FrameGraphNodeHandle handle);
    [[nodiscard]] const FrameGraphNode* AccessNode(FrameGraphNodeHandle handle) const;
    [[nodiscard]] FrameGraphResource* GetResource(std::string_view name);
    [[nodiscard]] const FrameGraphResource* GetResource(std::string_view name) const;
    [[nodiscard]] FrameGraphResource* AccessResource(FrameGraphResourceHandle handle);
    [[nodiscard]] const FrameGraphResource* AccessResource(
        FrameGraphResourceHandle handle) const;

    void AddNode(const FrameGraphNodeCreation& node);

    [[nodiscard]] std::span<const FrameGraphNodeHandle> ExecutionOrder() const noexcept
    {
        return executionOrder_;
    }

private:
    std::vector<FrameGraphNodeHandle> nodes_;
    std::vector<FrameGraphNodeHandle> executionOrder_;
    FrameGraphBuilder* builder_ = nullptr; // Non-owning.
};

} // namespace Iryven
