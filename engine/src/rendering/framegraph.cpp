#include <iryven/rendering/framegraph.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <deque>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace Iryven {
namespace {

using CpuClock = std::chrono::steady_clock;

[[nodiscard]] float ElapsedMilliseconds(CpuClock::time_point start)
{
    return std::chrono::duration<float, std::milli>(CpuClock::now() - start)
        .count();
}

[[nodiscard]] bool IsTextureResource(FrameGraphResourceType type)
{
    return type == FrameGraphResourceType::Texture ||
           type == FrameGraphResourceType::Attachment;
}

[[nodiscard]] bool IsDepthFormat(Velos::RHI::Format format)
{
    return format == Velos::RHI::Format::D32_FLOAT ||
           format == Velos::RHI::Format::D24_UNORM_S8_UINT;
}

[[nodiscard]] Velos::RHI::ImageAspect ImageAspectFor(Velos::RHI::Format format)
{
    if (format == Velos::RHI::Format::D24_UNORM_S8_UINT) {
        return Velos::RHI::ImageAspect::Depth | Velos::RHI::ImageAspect::Stencil;
    }
    return IsDepthFormat(format)
        ? Velos::RHI::ImageAspect::Depth
        : Velos::RHI::ImageAspect::Color;
}

[[nodiscard]] Velos::RHI::LoadOp ToLoadOp(RenderPassOperation operation)
{
    switch (operation) {
    case RenderPassOperation::Load: return Velos::RHI::LoadOp::Load;
    case RenderPassOperation::Clear: return Velos::RHI::LoadOp::Clear;
    case RenderPassOperation::DontCare: return Velos::RHI::LoadOp::DontCare;
    }
    return Velos::RHI::LoadOp::DontCare;
}

[[nodiscard]] bool AccessReads(FrameGraphAccess access)
{
    switch (access) {
    case FrameGraphAccess::VertexBufferRead:
    case FrameGraphAccess::IndexBufferRead:
    case FrameGraphAccess::UniformRead:
    case FrameGraphAccess::IndirectRead:
    case FrameGraphAccess::ShaderSampledRead:
    case FrameGraphAccess::ShaderStorageRead:
    case FrameGraphAccess::ShaderStorageReadWrite:
    case FrameGraphAccess::ColorAttachmentRead:
    case FrameGraphAccess::ColorAttachmentReadWrite:
    case FrameGraphAccess::DepthStencilRead:
    case FrameGraphAccess::DepthStencilReadWrite:
    case FrameGraphAccess::TransferRead:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool AccessWrites(FrameGraphAccess access)
{
    switch (access) {
    case FrameGraphAccess::ShaderStorageWrite:
    case FrameGraphAccess::ShaderStorageReadWrite:
    case FrameGraphAccess::ColorAttachmentWrite:
    case FrameGraphAccess::ColorAttachmentReadWrite:
    case FrameGraphAccess::DepthStencilWrite:
    case FrameGraphAccess::DepthStencilReadWrite:
    case FrameGraphAccess::TransferWrite:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] Velos::RHI::ResourceState ResourceStateFor(
    FrameGraphAccess access)
{
    using State = Velos::RHI::ResourceState;
    switch (access) {
    case FrameGraphAccess::None: return State::Common;
    case FrameGraphAccess::VertexBufferRead: return State::VertexBuffer;
    case FrameGraphAccess::IndexBufferRead: return State::IndexBuffer;
    case FrameGraphAccess::UniformRead: return State::UniformBuffer;
    case FrameGraphAccess::IndirectRead: return State::IndirectArgument;
    case FrameGraphAccess::ShaderSampledRead:
    case FrameGraphAccess::ShaderStorageRead: return State::ShaderRead;
    case FrameGraphAccess::ShaderStorageWrite: return State::ShaderWrite;
    case FrameGraphAccess::ShaderStorageReadWrite: return State::ShaderReadWrite;
    case FrameGraphAccess::ColorAttachmentRead: return State::ColorAttachmentRead;
    case FrameGraphAccess::ColorAttachmentWrite: return State::ColorAttachmentWrite;
    case FrameGraphAccess::ColorAttachmentReadWrite: return State::RenderTarget;
    case FrameGraphAccess::DepthStencilRead: return State::DepthRead;
    case FrameGraphAccess::DepthStencilWrite: return State::DepthWrite;
    case FrameGraphAccess::DepthStencilReadWrite: return State::DepthReadWrite;
    case FrameGraphAccess::TransferRead: return State::TransferSrc;
    case FrameGraphAccess::TransferWrite: return State::TransferDst;
    case FrameGraphAccess::Present: return State::Present;
    case FrameGraphAccess::Auto: break;
    }
    throw std::logic_error("Unresolved automatic frame-graph access");
}

[[nodiscard]] Velos::RHI::ImageLayout ImageLayoutFor(FrameGraphAccess access)
{
    using Layout = Velos::RHI::ImageLayout;
    switch (access) {
    case FrameGraphAccess::ShaderSampledRead: return Layout::ShaderReadOnly;
    case FrameGraphAccess::ShaderStorageRead:
    case FrameGraphAccess::ShaderStorageWrite:
    case FrameGraphAccess::ShaderStorageReadWrite: return Layout::General;
    case FrameGraphAccess::ColorAttachmentRead:
    case FrameGraphAccess::ColorAttachmentWrite:
    case FrameGraphAccess::ColorAttachmentReadWrite: return Layout::ColorAttachment;
    case FrameGraphAccess::DepthStencilRead:
    case FrameGraphAccess::DepthStencilWrite:
    case FrameGraphAccess::DepthStencilReadWrite: return Layout::DepthAttachment;
    case FrameGraphAccess::TransferRead: return Layout::TransferSrc;
    case FrameGraphAccess::TransferWrite: return Layout::TransferDst;
    case FrameGraphAccess::Present: return Layout::Present;
    default: return Layout::Undefined;
    }
}

[[nodiscard]] FrameGraphAccess ResolveAccess(
    FrameGraphAccess access, FrameGraphResourceType type,
    const FrameGraphResourceInfo& info, bool input,
    Velos::RHI::QueueType queue)
{
    if (access != FrameGraphAccess::Auto) return access;
    if (type == FrameGraphResourceType::Reference) return FrameGraphAccess::None;
    if (queue == Velos::RHI::QueueType::Transfer) {
        return input ? FrameGraphAccess::TransferRead
                     : FrameGraphAccess::TransferWrite;
    }
    if (type == FrameGraphResourceType::Attachment) {
        const auto* texture = std::get_if<FrameGraphTextureInfo>(&info);
        const bool depth = texture != nullptr && IsDepthFormat(texture->format);
        if (depth) {
            return FrameGraphAccess::DepthStencilReadWrite;
        }
        return FrameGraphAccess::ColorAttachmentReadWrite;
    }
    if (type == FrameGraphResourceType::Texture) {
        return input ? FrameGraphAccess::ShaderSampledRead
                     : FrameGraphAccess::ShaderStorageWrite;
    }
    return input ? FrameGraphAccess::ShaderStorageRead
                 : FrameGraphAccess::ShaderStorageWrite;
}

void ValidateAccess(const FrameGraphResource& use, bool output,
                    Velos::RHI::QueueType queue, std::string_view passName)
{
    if (use.access == FrameGraphAccess::Auto) {
        throw std::logic_error("Frame-graph access was not resolved");
    }
    if (use.type == FrameGraphResourceType::Reference) {
        if (use.access != FrameGraphAccess::None) {
            throw std::logic_error("Reference resource '" + use.name +
                                   "' must use FrameGraphAccess::None");
        }
        return;
    }
    if (use.access == FrameGraphAccess::None) {
        throw std::logic_error("Pass '" + std::string(passName) +
                               "' declares no access for resource '" +
                               use.name + "'");
    }
    if (output && !AccessWrites(use.access)) {
        throw std::logic_error("Output resource '" + use.name +
                               "' must declare a write access");
    }

    const bool texture = IsTextureResource(use.type);
    const FrameGraphAccess access = use.access;
    const bool imageOnly = access == FrameGraphAccess::ShaderSampledRead ||
        access == FrameGraphAccess::ColorAttachmentRead ||
        access == FrameGraphAccess::ColorAttachmentWrite ||
        access == FrameGraphAccess::ColorAttachmentReadWrite ||
        access == FrameGraphAccess::DepthStencilRead ||
        access == FrameGraphAccess::DepthStencilWrite ||
        access == FrameGraphAccess::DepthStencilReadWrite ||
        access == FrameGraphAccess::Present;
    const bool bufferOnly = access == FrameGraphAccess::VertexBufferRead ||
        access == FrameGraphAccess::IndexBufferRead ||
        access == FrameGraphAccess::UniformRead ||
        access == FrameGraphAccess::IndirectRead;
    if ((imageOnly && !texture) || (bufferOnly && texture)) {
        throw std::logic_error("Resource '" + use.name +
                               "' has an access incompatible with its type");
    }
    if (queue == Velos::RHI::QueueType::Transfer &&
        access != FrameGraphAccess::TransferRead &&
        access != FrameGraphAccess::TransferWrite) {
        throw std::logic_error("Transfer pass '" + std::string(passName) +
                               "' declares a non-transfer resource access");
    }
    if (queue == Velos::RHI::QueueType::Compute &&
        (access == FrameGraphAccess::VertexBufferRead ||
         access == FrameGraphAccess::IndexBufferRead ||
         access == FrameGraphAccess::ColorAttachmentRead ||
         access == FrameGraphAccess::ColorAttachmentWrite ||
         access == FrameGraphAccess::ColorAttachmentReadWrite ||
         access == FrameGraphAccess::DepthStencilRead ||
         access == FrameGraphAccess::DepthStencilWrite ||
         access == FrameGraphAccess::DepthStencilReadWrite ||
         access == FrameGraphAccess::Present)) {
        throw std::logic_error("Compute pass '" + std::string(passName) +
                               "' declares a graphics-only resource access");
    }
}

void ValidateOutput(const FrameGraphResourceOutputCreation& creation)
{
    if (creation.name.empty()) {
        throw std::invalid_argument("Frame-graph output names cannot be empty");
    }

    if (creation.type == FrameGraphResourceType::Buffer &&
        !std::holds_alternative<FrameGraphBufferInfo>(creation.info)) {
        throw std::invalid_argument("Frame-graph buffer output requires buffer info");
    }

    if (IsTextureResource(creation.type) &&
        !std::holds_alternative<FrameGraphTextureInfo>(creation.info)) {
        throw std::invalid_argument("Frame-graph texture output requires texture info");
    }
}

[[nodiscard]] bool IsConcurrent(const FrameGraphResourceInfo& info)
{
    if (const auto* buffer = std::get_if<FrameGraphBufferInfo>(&info)) {
        return buffer->concurrentQueues;
    }
    if (const auto* texture = std::get_if<FrameGraphTextureInfo>(&info)) {
        return texture->concurrentQueues;
    }
    return true;
}

void EnableConcurrentQueues(FrameGraphResourceInfo& info)
{
    if (auto* buffer = std::get_if<FrameGraphBufferInfo>(&info)) {
        buffer->concurrentQueues = true;
    } else if (auto* texture = std::get_if<FrameGraphTextureInfo>(&info)) {
        texture->concurrentQueues = true;
    }
}

} // namespace

void FrameGraphBuilder::Init(Velos::RHI::IDevice& device)
{
    if (device_ != nullptr && device_ != &device) {
        throw std::logic_error("FrameGraphBuilder is already initialized");
    }

    device_ = &device;
    nodes_.reserve(kMaxNodes);
    resources_.reserve(kMaxResources);
    nodeMap_.reserve(kMaxNodes);
    resourceMap_.reserve(kMaxResources);
    renderPassMap_.reserve(kMaxRenderPasses);
}

void FrameGraphBuilder::DestroyOwnedResources()
{
    if (device_ == nullptr) return;

    for (FrameGraphHandle index = 0; index < resources_.size(); ++index) {
        FrameGraphResource& resource = resources_[index];
        if (resource.external || resource.outputHandle.handle != index) continue;

        if (auto* buffer = std::get_if<FrameGraphBufferInfo>(&resource.info);
            buffer != nullptr && buffer->handle.IsValid()) {
            device_->DestroyBuffer(buffer->handle);
            buffer->handle = {};
        } else if (auto* texture = std::get_if<FrameGraphTextureInfo>(&resource.info);
                   texture != nullptr) {
            if (texture->view.IsValid()) {
                device_->DestroyImageView(texture->view);
                texture->view = {};
            }
            if (texture->handle.IsValid()) {
                device_->DestroyImage(texture->handle);
                texture->handle = {};
            }
        }
    }
}

void FrameGraphBuilder::AllocateResource(FrameGraphResource& resource)
{
    if (resource.external || resource.type == FrameGraphResourceType::Reference) return;

    if (auto* buffer = std::get_if<FrameGraphBufferInfo>(&resource.info)) {
        if (!buffer->handle.IsValid()) {
            buffer->handle = Device().CreateBuffer({
                .size = static_cast<Velos::u64>(buffer->size),
                .usage = buffer->usage,
                .concurrentQueues = buffer->concurrentQueues,
                .debugName = resource.name.c_str(),
            });
            if (!buffer->handle.IsValid()) {
                throw std::runtime_error("Failed to allocate frame-graph buffer '" +
                                         resource.name + "'");
            }
        }
        return;
    }

    auto* texture = std::get_if<FrameGraphTextureInfo>(&resource.info);
    if (texture == nullptr) return;

    if (!texture->handle.IsValid()) {
        texture->handle = Device().CreateImage({
            .width = texture->width,
            .height = texture->height,
            .depth = texture->depth,
            .format = texture->format,
            .usage = texture->usage,
            .concurrentQueues = texture->concurrentQueues,
            .debugName = resource.name.c_str(),
        });
        if (!texture->handle.IsValid()) {
            throw std::runtime_error("Failed to allocate frame-graph image '" +
                                     resource.name + "'");
        }
    }
    if (!texture->view.IsValid()) {
        texture->view = Device().CreateImageView({
            .image = texture->handle,
            .format = texture->format,
            .aspect = ImageAspectFor(texture->format),
            .debugName = resource.name.c_str(),
        });
        if (!texture->view.IsValid()) {
            throw std::runtime_error("Failed to allocate frame-graph image view '" +
                                     resource.name + "'");
        }
    }
}

void FrameGraphBuilder::RecreateResizableResources(
    std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0) return;

    for (FrameGraphHandle index = 0; index < resources_.size(); ++index) {
        FrameGraphResource& resource = resources_[index];
        if (resource.external || resource.outputHandle.handle != index) continue;

        auto* texture = std::get_if<FrameGraphTextureInfo>(&resource.info);
        if (texture == nullptr || !texture->resizeWithSwapchain ||
            (texture->width == width && texture->height == height)) {
            continue;
        }

        if (texture->view.IsValid()) Device().DestroyImageView(texture->view);
        if (texture->handle.IsValid()) Device().DestroyImage(texture->handle);
        texture->view = {};
        texture->handle = {};
        texture->width = width;
        texture->height = height;
        AllocateResource(resource);

        for (FrameGraphResource& input : resources_) {
            if (input.outputHandle.handle == index && &input != &resource) {
                input.info = resource.info;
            }
        }
    }
}

void FrameGraphBuilder::Reset()
{
    DestroyOwnedResources();
    nodes_.clear();
    resources_.clear();
    nodeMap_.clear();
    resourceMap_.clear();
}

void FrameGraphBuilder::Shutdown()
{
    Reset();
    renderPassMap_.clear();
    device_ = nullptr;
}

void FrameGraphBuilder::RegisterRenderPass(
    std::string_view name, FrameGraphRenderPass& renderPass)
{
    if (name.empty()) {
        throw std::invalid_argument("Render-pass names cannot be empty");
    }
    if (renderPassMap_.size() >= kMaxRenderPasses &&
        !renderPassMap_.contains(std::string(name))) {
        throw std::length_error("Frame-graph render-pass capacity exceeded");
    }

    auto [iterator, inserted] = renderPassMap_.insert_or_assign(
        std::string(name), &renderPass);
    (void)inserted;

    if (FrameGraphNode* node = GetNode(name)) {
        node->graphRenderPass = iterator->second;
    }
}

FrameGraphResourceHandle FrameGraphBuilder::CreateNodeOutput(
    const FrameGraphResourceOutputCreation& creation,
    FrameGraphNodeHandle producer)
{
    ValidateOutput(creation);
    if (!producer.IsValid() || producer.handle >= nodes_.size()) {
        throw std::out_of_range("Frame-graph output has an invalid producer");
    }
    if (resources_.size() >= kMaxResources) {
        throw std::length_error("Frame-graph resource capacity exceeded");
    }
    if (resourceMap_.contains(creation.name)) {
        throw std::invalid_argument("Duplicate frame-graph output: " + creation.name);
    }

    const FrameGraphResourceHandle handle{
        static_cast<FrameGraphHandle>(resources_.size()) };
    resources_.push_back(FrameGraphResource{
        .type = creation.type,
        .access = creation.access,
        .info = creation.info,
        .external = creation.external,
        .producer = producer,
        .outputHandle = handle,
        .referenceCount = 0,
        .name = creation.name,
    });

    resourceMap_.emplace(creation.name, handle);
    return handle;
}

FrameGraphResourceHandle FrameGraphBuilder::CreateNodeInput(
    const FrameGraphResourceInputCreation& creation)
{
    if (creation.name.empty()) {
        throw std::invalid_argument("Frame-graph input names cannot be empty");
    }
    if (resources_.size() >= kMaxResources) {
        throw std::length_error("Frame-graph resource capacity exceeded");
    }

    const FrameGraphResourceHandle handle{
        static_cast<FrameGraphHandle>(resources_.size()) };
    resources_.push_back(FrameGraphResource{
        .type = creation.type,
        .access = creation.access,
        .info = creation.info,
        .external = creation.external,
        .producer = {},
        .outputHandle = {},
        .referenceCount = 0,
        .name = creation.name,
    });
    return handle;
}

FrameGraphNodeHandle FrameGraphBuilder::CreateNode(
    const FrameGraphNodeCreation& creation)
{
    if (creation.name.empty()) {
        throw std::invalid_argument("Frame-graph node names cannot be empty");
    }
    if (nodeMap_.contains(creation.name)) {
        throw std::invalid_argument("Duplicate frame-graph node: " + creation.name);
    }
    if (nodes_.size() >= kMaxNodes) {
        throw std::length_error("Frame-graph node capacity exceeded");
    }
    if (resources_.size() + creation.inputs.size() + creation.outputs.size() >
        kMaxResources) {
        throw std::length_error("Frame-graph resource capacity exceeded");
    }

    std::unordered_set<std::string> outputNames;
    outputNames.reserve(creation.outputs.size());
    for (const auto& output : creation.outputs) {
        ValidateOutput(output);
        if (resourceMap_.contains(output.name) ||
            !outputNames.emplace(output.name).second) {
            throw std::invalid_argument("Duplicate frame-graph output: " + output.name);
        }
    }
    for (const auto& input : creation.inputs) {
        if (input.name.empty()) {
            throw std::invalid_argument("Frame-graph input names cannot be empty");
        }
    }

    const FrameGraphNodeHandle handle{
        static_cast<FrameGraphHandle>(nodes_.size()) };
    FrameGraphNode node;
    node.name = creation.name;
    node.queue = creation.queue;
    node.enabled = creation.enabled;
    node.inputs.reserve(creation.inputs.size());
    node.outputs.reserve(creation.outputs.size());
    node.edges.reserve(creation.outputs.size());

    if (const auto pass = renderPassMap_.find(creation.name);
        pass != renderPassMap_.end()) {
        node.graphRenderPass = pass->second;
    }

    nodes_.push_back(std::move(node));
    nodeMap_.emplace(creation.name, handle);

    for (const auto& output : creation.outputs) {
        nodes_[handle.handle].outputs.push_back(CreateNodeOutput(output, handle));
    }
    for (const auto& input : creation.inputs) {
        nodes_[handle.handle].inputs.push_back(CreateNodeInput(input));
    }
    return handle;
}

FrameGraphNode* FrameGraphBuilder::GetNode(std::string_view name)
{
    const auto found = nodeMap_.find(std::string(name));
    return found == nodeMap_.end() ? nullptr : AccessNode(found->second);
}

const FrameGraphNode* FrameGraphBuilder::GetNode(std::string_view name) const
{
    const auto found = nodeMap_.find(std::string(name));
    return found == nodeMap_.end() ? nullptr : AccessNode(found->second);
}

FrameGraphNode* FrameGraphBuilder::AccessNode(FrameGraphNodeHandle handle)
{
    return handle.IsValid() && handle.handle < nodes_.size()
        ? &nodes_[handle.handle]
        : nullptr;
}

const FrameGraphNode* FrameGraphBuilder::AccessNode(FrameGraphNodeHandle handle) const
{
    return handle.IsValid() && handle.handle < nodes_.size()
        ? &nodes_[handle.handle]
        : nullptr;
}

FrameGraphResource* FrameGraphBuilder::GetResource(std::string_view name)
{
    const auto found = resourceMap_.find(std::string(name));
    return found == resourceMap_.end() ? nullptr : AccessResource(found->second);
}

const FrameGraphResource* FrameGraphBuilder::GetResource(std::string_view name) const
{
    const auto found = resourceMap_.find(std::string(name));
    return found == resourceMap_.end() ? nullptr : AccessResource(found->second);
}

FrameGraphResource* FrameGraphBuilder::AccessResource(FrameGraphResourceHandle handle)
{
    return handle.IsValid() && handle.handle < resources_.size()
        ? &resources_[handle.handle]
        : nullptr;
}

const FrameGraphResource* FrameGraphBuilder::AccessResource(
    FrameGraphResourceHandle handle) const
{
    return handle.IsValid() && handle.handle < resources_.size()
        ? &resources_[handle.handle]
        : nullptr;
}

Velos::RHI::IDevice& FrameGraphBuilder::Device() const
{
    if (device_ == nullptr) {
        throw std::logic_error("FrameGraphBuilder is not initialized");
    }
    return *device_;
}

void FrameGraph::Init(FrameGraphBuilder& builder)
{
    builder_ = &builder;
    nodes_.reserve(FrameGraphBuilder::kMaxNodes);
    executionOrder_.reserve(FrameGraphBuilder::kMaxNodes);
    executionBatches_.reserve(3);
    queueTimelines_.reserve(2);
    graphicsSubmissionWaits_.reserve(2);
}

void FrameGraph::Shutdown()
{
    DestroyTimelines();
    Reset();
    builder_ = nullptr;
}

void FrameGraph::Parse(const std::filesystem::path&)
{
    throw std::logic_error(
        "FrameGraph::Parse requires a JSON integration; use AddNode for now");
}

void FrameGraph::Reset()
{
    nodes_.clear();
    executionOrder_.clear();
    executionBatches_.clear();
    resourceStates_.clear();
    graphicsSubmissionWaits_.clear();
    if (builder_ != nullptr) builder_->Reset();
}

void FrameGraph::EnableRenderPass(std::string_view renderPassName)
{
    FrameGraphNode* node = GetNode(renderPassName);
    if (node == nullptr) {
        throw std::invalid_argument("Unknown frame-graph pass: " +
                                    std::string(renderPassName));
    }
    node->enabled = true;
}

void FrameGraph::DisableRenderPass(std::string_view renderPassName)
{
    FrameGraphNode* node = GetNode(renderPassName);
    if (node == nullptr) {
        throw std::invalid_argument("Unknown frame-graph pass: " +
                                    std::string(renderPassName));
    }
    node->enabled = false;
}

void FrameGraph::Compile()
{
    if (builder_ == nullptr) {
        throw std::logic_error("FrameGraph is not initialized");
    }

    executionOrder_.clear();
    executionBatches_.clear();
    resourceStates_.clear();
    std::vector<std::uint32_t> indegrees(nodes_.size(), 0);
    std::size_t enabledNodeCount = 0;

    const auto addEdge = [this, &indegrees](FrameGraphNodeHandle parentHandle,
                                             FrameGraphNodeHandle childHandle) {
        if (!parentHandle.IsValid() || !childHandle.IsValid() ||
            parentHandle == childHandle) {
            return;
        }
        FrameGraphNode* parent = AccessNode(parentHandle);
        const FrameGraphNode* child = AccessNode(childHandle);
        if (parent == nullptr || child == nullptr ||
            !parent->enabled || !child->enabled) {
            return;
        }
        if (std::find(parent->edges.begin(), parent->edges.end(), childHandle) ==
            parent->edges.end()) {
            parent->edges.push_back(childHandle);
            ++indegrees[childHandle.handle];
        }
    };

    for (FrameGraphNodeHandle handle : nodes_) {
        FrameGraphNode* node = AccessNode(handle);
        assert(node != nullptr);
        node->edges.clear();
        node->referenceCount = 0;
        if (node->enabled) {
            ++enabledNodeCount;
            for (FrameGraphResourceHandle outputHandle : node->outputs) {
                FrameGraphResource* output = AccessResource(outputHandle);
                output->access = ResolveAccess(output->access, output->type,
                                               output->info, false, node->queue);
                ValidateAccess(*output, true, node->queue, node->name);
            }
        }
    }
    for (FrameGraphHandle index = 0; index < FrameGraphBuilder::kMaxResources; ++index) {
        FrameGraphResource* resource = AccessResource({ index });
        if (resource == nullptr) break;
        resource->referenceCount = 0;
    }

    struct HazardState {
        FrameGraphNodeHandle lastWriter{};
        std::vector<FrameGraphNodeHandle> readers;
    };
    std::vector<HazardState> hazards(builder_->resources_.size());
    for (FrameGraphNodeHandle producerHandle : nodes_) {
        const FrameGraphNode* producer = AccessNode(producerHandle);
        if (!producer->enabled) continue;
        for (FrameGraphResourceHandle outputHandle : producer->outputs) {
            const FrameGraphResource* output = AccessResource(outputHandle);
            if (output->type == FrameGraphResourceType::Reference) continue;
            hazards[outputHandle.handle].lastWriter = producerHandle;
        }
    }

    for (FrameGraphNodeHandle childHandle : nodes_) {
        FrameGraphNode* child = AccessNode(childHandle);
        if (!child->enabled) continue;

        for (FrameGraphResourceHandle inputHandle : child->inputs) {
            FrameGraphResource* input = AccessResource(inputHandle);
            FrameGraphResource* output = GetResource(input->name);

            if (output == nullptr) {
                if (!input->external) {
                    throw std::logic_error("Resource '" + input->name +
                                           "' has no producer");
                }
                input->outputHandle = inputHandle;
                input->access = ResolveAccess(input->access, input->type,
                                              input->info, true, child->queue);
                ValidateAccess(*input, false, child->queue, child->name);
                continue;
            }
            if (output->producer == childHandle) {
                throw std::logic_error("Node '" + child->name +
                                       "' consumes its own output '" + input->name + "'");
            }

            FrameGraphNode* parent = AccessNode(output->producer);
            if (parent == nullptr || !parent->enabled) {
                throw std::logic_error("Enabled node '" + child->name +
                                       "' depends on disabled producer for '" +
                                       input->name + "'");
            }

            const Velos::RHI::QueueRelationship queueRelationship =
                QueueRelationship(parent->queue, child->queue);
            if (queueRelationship ==
                    Velos::RHI::QueueRelationship::DifferentFamily &&
                output->type != FrameGraphResourceType::Reference) {
                if (output->external && !IsConcurrent(output->info)) {
                    throw std::logic_error(
                        "External resource '" + output->name +
                        "' crosses frame-graph queue families but was not declared "
                        "concurrentQueues");
                }
                EnableConcurrentQueues(output->info);
            }

            const auto requestedTexture =
                std::get_if<FrameGraphTextureInfo>(&input->info);
            const RenderPassOperation requestedLoadOp = requestedTexture
                ? requestedTexture->loadOp : RenderPassOperation::DontCare;
            input->producer = output->producer;
            input->outputHandle = output->outputHandle;
            input->info = output->info;
            if (auto* texture = std::get_if<FrameGraphTextureInfo>(&input->info);
                texture != nullptr &&
                requestedLoadOp != RenderPassOperation::DontCare) {
                texture->loadOp = requestedLoadOp;
            }
            input->external = output->external;
            input->access = ResolveAccess(input->access, input->type,
                                          input->info, true, child->queue);
            ValidateAccess(*input, false, child->queue, child->name);
            ++output->referenceCount;

            if (input->type == FrameGraphResourceType::Reference) {
                addEdge(output->producer, childHandle);
                continue;
            }

            HazardState& hazard = hazards[output->outputHandle.handle];
            if (AccessReads(input->access)) {
                addEdge(hazard.lastWriter, childHandle);
            }
            if (AccessWrites(input->access)) {
                addEdge(hazard.lastWriter, childHandle);
                for (FrameGraphNodeHandle reader : hazard.readers) {
                    addEdge(reader, childHandle);
                }
                hazard.readers.clear();
                hazard.lastWriter = childHandle;
            } else if (AccessReads(input->access) &&
                       std::find(hazard.readers.begin(), hazard.readers.end(),
                                 childHandle) == hazard.readers.end()) {
                hazard.readers.push_back(childHandle);
            }
        }
    }

    std::deque<FrameGraphNodeHandle> ready;
    for (FrameGraphNodeHandle handle : nodes_) {
        const FrameGraphNode* node = AccessNode(handle);
        if (node->enabled && indegrees[handle.handle] == 0) ready.push_back(handle);
    }

    while (!ready.empty()) {
        const FrameGraphNodeHandle handle = ready.front();
        ready.pop_front();
        executionOrder_.push_back(handle);

        const FrameGraphNode* node = AccessNode(handle);
        for (FrameGraphNodeHandle child : node->edges) {
            if (--indegrees[child.handle] == 0) ready.push_back(child);
        }
    }

    if (executionOrder_.size() != enabledNodeCount) {
        executionOrder_.clear();
        throw std::logic_error("Frame graph contains a dependency cycle");
    }

    for (FrameGraphNodeHandle handle : executionOrder_) {
        FrameGraphNode* node = AccessNode(handle);
        for (FrameGraphResourceHandle outputHandle : node->outputs) {
            FrameGraphResource* resource = AccessResource(outputHandle);
            builder_->AllocateResource(*resource);
        }
    }
    resourceStates_.assign(builder_->resources_.size(), {});

    std::vector<std::size_t> nodeBatches(
        nodes_.size(), std::numeric_limits<std::size_t>::max());
    for (FrameGraphNodeHandle handle : executionOrder_) {
        const FrameGraphNode* node = AccessNode(handle);
        if (executionBatches_.empty() ||
            QueueRelationship(executionBatches_.back().queue, node->queue) !=
                Velos::RHI::QueueRelationship::SameQueue) {
            executionBatches_.push_back({ .queue = node->queue });
        }
        nodeBatches[handle.handle] = executionBatches_.size() - 1;
        executionBatches_.back().nodes.push_back(handle);
    }

    for (FrameGraphNodeHandle parentHandle : executionOrder_) {
        const FrameGraphNode* parent = AccessNode(parentHandle);
        const std::size_t parentBatch = nodeBatches[parentHandle.handle];
        for (FrameGraphNodeHandle childHandle : parent->edges) {
            const std::size_t childBatch = nodeBatches[childHandle.handle];
            if (parentBatch == childBatch) continue;
            auto& dependencies = executionBatches_[childBatch].dependencies;
            if (std::find(dependencies.begin(), dependencies.end(), parentBatch) ==
                dependencies.end()) {
                dependencies.push_back(parentBatch);
            }
        }
    }
}

void FrameGraph::AddUI()
{
    for (FrameGraphNodeHandle handle : executionOrder_) {
        FrameGraphNode* node = AccessNode(handle);
        if (node->graphRenderPass != nullptr) node->graphRenderPass->AddUI();
    }
}

void FrameGraph::BeginFrame()
{
    graphicsSubmissionWaits_.clear();
}

Velos::RHI::QueueRelationship FrameGraph::QueueRelationship(
    Velos::RHI::QueueType first, Velos::RHI::QueueType second) const
{
    if (first == second) return Velos::RHI::QueueRelationship::SameQueue;

    // Structural frame-graph tests can compile a graph without initializing an
    // RHI device. In that case logical queues remain distinct.
    if (builder_ == nullptr || builder_->device_ == nullptr) {
        return Velos::RHI::QueueRelationship::DifferentFamily;
    }
    return builder_->Device().GetQueueRelationship(first, second);
}

FrameGraph::QueueTimelineState& FrameGraph::TimelineFor(
    Velos::RHI::QueueType queue)
{
    const auto found = std::find_if(
        queueTimelines_.begin(), queueTimelines_.end(),
        [queue](const QueueTimelineState& timeline) {
            return timeline.queue == queue;
        });
    if (found != queueTimelines_.end()) return *found;

    QueueTimelineState timeline{
        .queue = queue,
        .semaphore = builder_->Device().CreateSemaphore(
            Velos::RHI::SemaphoreType::Timeline),
    };
    if (!timeline.semaphore.IsValid()) {
        throw std::runtime_error("Failed to create frame-graph queue timeline");
    }
    queueTimelines_.push_back(timeline);
    return queueTimelines_.back();
}

void FrameGraph::DestroyTimelines()
{
    if (queueTimelines_.empty()) return;
    if (builder_ != nullptr && builder_->device_ != nullptr) {
        builder_->Device().WaitIdle();
        for (const QueueTimelineState& timeline : queueTimelines_) {
            if (timeline.semaphore.IsValid()) {
                builder_->Device().DestroySemaphore(timeline.semaphore);
            }
        }
    }
    queueTimelines_.clear();
}

void FrameGraph::RecordBatch(
    const FrameGraphQueueBatch& batch,
    Velos::RHI::ICommandList& commandList, const RenderScene& scene)
{
    for (FrameGraphNodeHandle handle : batch.nodes) {
        FrameGraphNode* node = AccessNode(handle);
        if (node->graphRenderPass == nullptr) {
            throw std::logic_error("No render-pass implementation registered for '" +
                                   node->name + "'");
        }

        const auto resourceSetupStart = CpuClock::now();

        std::vector<Velos::RHI::ColorAttachmentDesc> colorAttachments;
        Velos::RHI::DepthAttachmentDesc depthAttachment{};
        bool hasDepthAttachment = false;
        std::uint32_t renderWidth = 0;
        std::uint32_t renderHeight = 0;

        std::vector<Velos::RHI::BufferBarrier> bufferBarriers;
        std::vector<Velos::RHI::ImageBarrier> imageBarriers;
        bufferBarriers.reserve(node->inputs.size() + node->outputs.size());
        imageBarriers.reserve(node->inputs.size() + node->outputs.size());

        const auto transition = [&](const FrameGraphResource& use,
                                    const FrameGraphResource& resource) {
            if (!resource.outputHandle.IsValid() ||
                resource.outputHandle.handle >= resourceStates_.size()) {
                throw std::logic_error("Resource '" + resource.name +
                                       "' has no compiled state slot");
            }
            TrackedResourceState& tracked =
                resourceStates_[resource.outputHandle.handle];
            const Velos::RHI::ResourceState desiredState =
                ResourceStateFor(use.access);

            if (const auto* buffer = std::get_if<FrameGraphBufferInfo>(
                    &resource.info)) {
                if (!buffer->handle.IsValid()) {
                    throw std::logic_error("Pass '" + node->name +
                                           "' references an invalid buffer");
                }
                if (tracked.physicalHandle != buffer->handle.id || tracked.image) {
                    tracked = {};
                    tracked.physicalHandle = buffer->handle.id;
                }

                const bool sameQueue = tracked.initialized &&
                    QueueRelationship(tracked.queue, node->queue) ==
                        Velos::RHI::QueueRelationship::SameQueue;
                if (sameQueue &&
                    (tracked.access != use.access ||
                     AccessWrites(tracked.access) || AccessWrites(use.access))) {
                    bufferBarriers.push_back({
                        .buffer = buffer->handle,
                        .oldState = ResourceStateFor(tracked.access),
                        .newState = desiredState,
                        .sourceQueue = tracked.queue,
                        .destinationQueue = node->queue,
                    });
                }
                tracked.access = use.access;
                tracked.queue = node->queue;
                tracked.image = false;
                tracked.initialized = true;
                return;
            }

            const auto* texture = std::get_if<FrameGraphTextureInfo>(
                &resource.info);
            if (texture == nullptr || !texture->handle.IsValid()) {
                throw std::logic_error("Pass '" + node->name +
                                       "' references an invalid image");
            }
            if (tracked.physicalHandle != texture->handle.id || !tracked.image) {
                tracked = {};
                tracked.physicalHandle = texture->handle.id;
                tracked.image = true;
            }

            const Velos::RHI::ImageLayout actualLayout =
                builder_->Device().GetImageLayout(texture->handle, 0);
            if (actualLayout == Velos::RHI::ImageLayout::Undefined) {
                tracked.initialized = false;
                tracked.access = FrameGraphAccess::None;
            }
            tracked.layout = actualLayout;

            const Velos::RHI::ImageLayout desiredLayout =
                ImageLayoutFor(use.access);
            if (desiredLayout == Velos::RHI::ImageLayout::Undefined) {
                throw std::logic_error("Image resource '" + resource.name +
                                       "' has a buffer-only access");
            }

            const bool sameQueue = tracked.initialized &&
                QueueRelationship(tracked.queue, node->queue) ==
                    Velos::RHI::QueueRelationship::SameQueue;
            const bool layoutChange = actualLayout != desiredLayout;
            const bool memoryHazard = sameQueue &&
                (tracked.access != use.access ||
                 AccessWrites(tracked.access) || AccessWrites(use.access));
            if (layoutChange || memoryHazard) {
                imageBarriers.push_back({
                    .image = texture->handle,
                    .oldLayout = actualLayout,
                    .newLayout = desiredLayout,
                    .oldState = sameQueue
                        ? ResourceStateFor(tracked.access)
                        : Velos::RHI::ResourceState::Undefined,
                    .newState = desiredState,
                    .useExplicitStates = true,
                    .aspect = ImageAspectFor(texture->format),
                    .sourceQueue = sameQueue ? tracked.queue : node->queue,
                    .destinationQueue = node->queue,
                });
            }
            tracked.access = use.access;
            tracked.layout = desiredLayout;
            tracked.queue = node->queue;
            tracked.image = true;
            tracked.initialized = true;
        };

        const auto addAttachment = [&](const FrameGraphResource& use,
                                       const FrameGraphResource& resource) {
            const auto* texture = std::get_if<FrameGraphTextureInfo>(&resource.info);
            const auto* useTexture = std::get_if<FrameGraphTextureInfo>(&use.info);
            if (texture == nullptr || useTexture == nullptr ||
                !texture->view.IsValid()) {
                throw std::logic_error("Attachment '" + use.name +
                                       "' has no valid image view");
            }
            if (renderWidth != 0 &&
                (renderWidth != texture->width || renderHeight != texture->height)) {
                throw std::logic_error("Pass '" + node->name +
                                       "' uses attachments with different extents");
            }
            renderWidth = texture->width;
            renderHeight = texture->height;

            if (IsDepthFormat(texture->format)) {
                if (hasDepthAttachment) {
                    throw std::logic_error("Pass '" + node->name +
                                           "' has multiple depth attachments");
                }
                depthAttachment.view = texture->view;
                depthAttachment.loadOp = ToLoadOp(useTexture->loadOp);
                depthAttachment.storeOp = Velos::RHI::StoreOp::Store;
                depthAttachment.clearDepth = useTexture->clearDepth;
                depthAttachment.clearStencil = useTexture->clearStencil;
                hasDepthAttachment = true;
            } else {
                Velos::RHI::ColorAttachmentDesc colorAttachment{};
                colorAttachment.view = texture->view;
                colorAttachment.loadOp = ToLoadOp(useTexture->loadOp);
                colorAttachment.storeOp = Velos::RHI::StoreOp::Store;
                colorAttachment.clearValue = useTexture->clearColor;
                colorAttachments.push_back(colorAttachment);
            }
        };

        for (FrameGraphResourceHandle inputHandle : node->inputs) {
            const FrameGraphResource* input = AccessResource(inputHandle);
            const FrameGraphResource* resource = input->outputHandle.IsValid()
                ? AccessResource(input->outputHandle)
                : input;
            if (resource == nullptr) continue;
            if (input->type != FrameGraphResourceType::Reference) {
                transition(*input, *resource);
            }
            if (input->type == FrameGraphResourceType::Attachment) {
                addAttachment(*input, *resource);
            }
        }

        for (FrameGraphResourceHandle outputHandle : node->outputs) {
            const FrameGraphResource* output = AccessResource(outputHandle);
            if (output->type != FrameGraphResourceType::Reference) {
                transition(*output, *output);
            }
            if (output->type == FrameGraphResourceType::Attachment) {
                addAttachment(*output, *output);
            }
        }

        if (!bufferBarriers.empty() || !imageBarriers.empty()) {
            commandList.PipelineBarrier(bufferBarriers, imageBarriers);
        }
		cpuTimings_.resourceSetupMs += ElapsedMilliseconds(resourceSetupStart);

		const auto preRenderStart = CpuClock::now();
        node->graphRenderPass->PreRender(commandList, scene);
		cpuTimings_.preRenderMs += ElapsedMilliseconds(preRenderStart);

        const bool hasAttachments = !colorAttachments.empty() || hasDepthAttachment;
		const auto renderingSetupStart = CpuClock::now();
        if (hasAttachments) {
            commandList.SetViewport({
                .width = static_cast<float>(renderWidth),
                .height = static_cast<float>(renderHeight),
            });
            commandList.SetScissor({ .extent = { renderWidth, renderHeight } });
            commandList.BeginRendering({
                .renderArea = { .extent = { renderWidth, renderHeight } },
                .colorAttachments = colorAttachments.data(),
                .colorAttachmentCount = static_cast<std::uint32_t>(colorAttachments.size()),
                .depthAttachment = hasDepthAttachment ? &depthAttachment : nullptr,
            });
        }
		cpuTimings_.renderingSetupMs += ElapsedMilliseconds(renderingSetupStart);

		const auto drawRecordStart = CpuClock::now();
        try {
            node->graphRenderPass->Render(commandList, scene);
        } catch (...) {
			cpuTimings_.drawRecordMs += ElapsedMilliseconds(drawRecordStart);
            if (hasAttachments) commandList.EndRendering();
            throw;
        }
		cpuTimings_.drawRecordMs += ElapsedMilliseconds(drawRecordStart);
		if (hasAttachments) {
			const auto renderingEndStart = CpuClock::now();
			commandList.EndRendering();
			cpuTimings_.renderingSetupMs += ElapsedMilliseconds(renderingEndStart);
		}
    }
}

void FrameGraph::Render(const RenderScene& scene)
{
    if (builder_ == nullptr) {
        throw std::logic_error("FrameGraph is not initialized");
    }
	cpuTimings_ = {};
	auto schedulingStart = CpuClock::now();

    std::vector<Velos::RHI::TimelineSemaphorePoint> batchSignals(
        executionBatches_.size());
    const auto appendGraphicsWait = [this](
        const Velos::RHI::TimelineSemaphorePoint& point) {
        const auto found = std::find_if(
            graphicsSubmissionWaits_.begin(), graphicsSubmissionWaits_.end(),
            [&point](const Velos::RHI::TimelineSemaphorePoint& wait) {
                return wait.semaphore.id == point.semaphore.id;
            });
        if (found == graphicsSubmissionWaits_.end()) {
            graphicsSubmissionWaits_.push_back(point);
        } else {
            found->value = std::max(found->value, point.value);
        }
    };

    for (std::size_t batchIndex = 0;
         batchIndex < executionBatches_.size(); ++batchIndex) {
        const FrameGraphQueueBatch& batch = executionBatches_[batchIndex];
        std::vector<Velos::RHI::TimelineSemaphorePoint> waits;
        waits.reserve(batch.dependencies.size() + batch.nodes.size());
        const auto appendWait = [&waits](
            const Velos::RHI::TimelineSemaphorePoint& point) {
            const auto found = std::find_if(
                waits.begin(), waits.end(),
                [&point](const Velos::RHI::TimelineSemaphorePoint& wait) {
                    return wait.semaphore.id == point.semaphore.id;
                });
            if (found == waits.end()) {
                waits.push_back(point);
            } else {
                found->value = std::max(found->value, point.value);
            }
        };
        for (std::size_t dependency : batch.dependencies) {
            if (dependency >= batchIndex ||
                !batchSignals[dependency].semaphore.IsValid()) {
                throw std::logic_error(
                    "Frame-graph queue dependency was not submitted");
            }
            if (QueueRelationship(executionBatches_[dependency].queue,
                                  batch.queue) !=
                Velos::RHI::QueueRelationship::SameQueue) {
                appendWait(batchSignals[dependency]);
            }
        }

        const auto visitBatchResources = [this, &batch](auto&& visitor) {
            for (FrameGraphNodeHandle nodeHandle : batch.nodes) {
                const FrameGraphNode* node = AccessNode(nodeHandle);
                const auto visitUse = [this, &visitor](
                    FrameGraphResourceHandle useHandle, bool input) {
                    const FrameGraphResource* use = AccessResource(useHandle);
                    if (use == nullptr ||
                        use->type == FrameGraphResourceType::Reference) {
                        return;
                    }
                    const FrameGraphResource* resource = input &&
                        use->outputHandle.IsValid()
                            ? AccessResource(use->outputHandle)
                            : use;
                    if (resource == nullptr ||
                        !resource->outputHandle.IsValid() ||
                        resource->outputHandle.handle >= resourceStates_.size()) {
                        throw std::logic_error(
                            "Frame-graph batch references an uncompiled resource");
                    }
                    visitor(resource->outputHandle.handle);
                };
                for (FrameGraphResourceHandle input : node->inputs) {
                    visitUse(input, true);
                }
                for (FrameGraphResourceHandle output : node->outputs) {
                    visitUse(output, false);
                }
            }
        };

        visitBatchResources([this, &batch, &appendWait](std::uint32_t stateIndex) {
            const TrackedResourceState& state = resourceStates_[stateIndex];
            if (state.initialized &&
                QueueRelationship(state.queue, batch.queue) !=
                    Velos::RHI::QueueRelationship::SameQueue &&
                state.completion.semaphore.IsValid()) {
                appendWait(state.completion);
            }
        });

        QueueTimelineState& timeline = TimelineFor(batch.queue);
		cpuTimings_.schedulingMs += ElapsedMilliseconds(schedulingStart);

		const auto acquireStart = CpuClock::now();
        Velos::RHI::ICommandList& queueCommands =
            builder_->Device().AcquireCommandList(batch.queue);
		cpuTimings_.acquireCommandListMs += ElapsedMilliseconds(acquireStart);

		const auto commandBeginStart = CpuClock::now();
        queueCommands.Begin();
		cpuTimings_.commandBeginMs += ElapsedMilliseconds(commandBeginStart);
        RecordBatch(batch, queueCommands, scene);

		const auto commandEndStart = CpuClock::now();
        queueCommands.End();
		cpuTimings_.commandEndMs += ElapsedMilliseconds(commandEndStart);
		schedulingStart = CpuClock::now();

        if (timeline.nextSignalValue ==
            std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("Frame-graph queue timeline exhausted");
        }
        const Velos::RHI::TimelineSemaphorePoint signal{
            timeline.semaphore, timeline.nextSignalValue++ };
        const Velos::RHI::TimelineSemaphorePoint signals[] = { signal };
		cpuTimings_.schedulingMs += ElapsedMilliseconds(schedulingStart);

		const auto submitStart = CpuClock::now();
        builder_->Device().Submit(batch.queue, queueCommands, {
            .waits = waits,
            .signals = signals,
        });
		cpuTimings_.queueSubmitMs += ElapsedMilliseconds(submitStart);
		schedulingStart = CpuClock::now();
        batchSignals[batchIndex] = signal;
        visitBatchResources([this, &signal](std::uint32_t stateIndex) {
            resourceStates_[stateIndex].completion = signal;
        });
    }

    for (std::size_t batchIndex = 0;
         batchIndex < executionBatches_.size(); ++batchIndex) {
        if (QueueRelationship(executionBatches_[batchIndex].queue,
                              Velos::RHI::QueueType::Graphics) !=
            Velos::RHI::QueueRelationship::SameQueue) {
            appendGraphicsWait(batchSignals[batchIndex]);
        }
    }
	cpuTimings_.schedulingMs += ElapsedMilliseconds(schedulingStart);
}

void FrameGraph::OnResize(
    Velos::RHI::IDevice& device, std::uint32_t width, std::uint32_t height)
{
    if (builder_ == nullptr) return;
    if (&device != &builder_->Device()) {
        throw std::invalid_argument("FrameGraph resized with a different device");
    }
    builder_->RecreateResizableResources(width, height);
    resourceStates_.assign(builder_->resources_.size(), {});

    for (FrameGraphNodeHandle handle : executionOrder_) {
        FrameGraphNode* node = AccessNode(handle);
        if (node->graphRenderPass != nullptr) {
            node->graphRenderPass->OnResize(device, width, height);
        }
    }
}

FrameGraphNode* FrameGraph::GetNode(std::string_view name)
{
    return builder_ != nullptr ? builder_->GetNode(name) : nullptr;
}

const FrameGraphNode* FrameGraph::GetNode(std::string_view name) const
{
    return builder_ != nullptr ? builder_->GetNode(name) : nullptr;
}

FrameGraphNode* FrameGraph::AccessNode(FrameGraphNodeHandle handle)
{
    return builder_ != nullptr ? builder_->AccessNode(handle) : nullptr;
}

const FrameGraphNode* FrameGraph::AccessNode(FrameGraphNodeHandle handle) const
{
    return builder_ != nullptr ? builder_->AccessNode(handle) : nullptr;
}

FrameGraphResource* FrameGraph::GetResource(std::string_view name)
{
    return builder_ != nullptr ? builder_->GetResource(name) : nullptr;
}

const FrameGraphResource* FrameGraph::GetResource(std::string_view name) const
{
    return builder_ != nullptr ? builder_->GetResource(name) : nullptr;
}

FrameGraphResource* FrameGraph::AccessResource(FrameGraphResourceHandle handle)
{
    return builder_ != nullptr ? builder_->AccessResource(handle) : nullptr;
}

const FrameGraphResource* FrameGraph::AccessResource(
    FrameGraphResourceHandle handle) const
{
    return builder_ != nullptr ? builder_->AccessResource(handle) : nullptr;
}

void FrameGraph::AddNode(const FrameGraphNodeCreation& node)
{
    if (builder_ == nullptr) {
        throw std::logic_error("FrameGraph is not initialized");
    }
    nodes_.push_back(builder_->CreateNode(node));
    executionOrder_.clear();
}

} // namespace Iryven
