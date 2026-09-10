#include <iryven/rendering/framegraph.h>

#include <algorithm>
#include <cassert>
#include <deque>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace Iryven {
namespace {

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
    if (creation.type != FrameGraphResourceType::Reference &&
        resourceMap_.contains(creation.name)) {
        throw std::invalid_argument("Duplicate frame-graph output: " + creation.name);
    }

    const FrameGraphResourceHandle handle{
        static_cast<FrameGraphHandle>(resources_.size()) };
    resources_.push_back(FrameGraphResource{
        .type = creation.type,
        .info = creation.info,
        .external = creation.external,
        .producer = producer,
        .outputHandle = handle,
        .referenceCount = 0,
        .name = creation.name,
    });

    if (creation.type != FrameGraphResourceType::Reference) {
        resourceMap_.emplace(creation.name, handle);
    }
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
        if (output.type != FrameGraphResourceType::Reference &&
            (resourceMap_.contains(output.name) ||
             !outputNames.emplace(output.name).second)) {
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
    std::vector<std::uint32_t> indegrees(nodes_.size(), 0);
    std::size_t enabledNodeCount = 0;

    for (FrameGraphNodeHandle handle : nodes_) {
        FrameGraphNode* node = AccessNode(handle);
        assert(node != nullptr);
        node->edges.clear();
        node->referenceCount = 0;
        if (node->enabled) {
            ++enabledNodeCount;
            if (node->queue != Velos::RHI::QueueType::Graphics) {
                const auto usesAttachment = [this](FrameGraphResourceHandle handle) {
                    const FrameGraphResource* resource = AccessResource(handle);
                    return resource != nullptr &&
                           resource->type == FrameGraphResourceType::Attachment;
                };
                if (std::any_of(node->inputs.begin(), node->inputs.end(),
                                usesAttachment) ||
                    std::any_of(node->outputs.begin(), node->outputs.end(),
                                usesAttachment)) {
                    throw std::logic_error(
                        "Non-graphics frame-graph pass '" + node->name +
                        "' cannot use render attachments");
                }
            }
        }
    }
    for (FrameGraphHandle index = 0; index < FrameGraphBuilder::kMaxResources; ++index) {
        FrameGraphResource* resource = AccessResource({ index });
        if (resource == nullptr) break;
        resource->referenceCount = 0;
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

            if (parent->queue != child->queue &&
                output->type != FrameGraphResourceType::Reference) {
                if (output->external && !IsConcurrent(output->info)) {
                    throw std::logic_error(
                        "External resource '" + output->name +
                        "' crosses frame-graph queues but was not declared "
                        "concurrentQueues");
                }
                EnableConcurrentQueues(output->info);
            }

            input->producer = output->producer;
            input->outputHandle = output->outputHandle;
            input->info = output->info;
            input->external = output->external;
            ++output->referenceCount;

            if (std::find(parent->edges.begin(), parent->edges.end(), childHandle) ==
                parent->edges.end()) {
                parent->edges.push_back(childHandle);
                ++indegrees[childHandle.handle];
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

    std::vector<std::size_t> nodeBatches(
        nodes_.size(), std::numeric_limits<std::size_t>::max());
    for (FrameGraphNodeHandle handle : executionOrder_) {
        const FrameGraphNode* node = AccessNode(handle);
        if (executionBatches_.empty() ||
            executionBatches_.back().queue != node->queue) {
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
        node->graphRenderPass->PreRender(commandList, scene);

        Velos::RHI::ColorAttachmentDesc colorAttachment{};
        Velos::RHI::DepthAttachmentDesc depthAttachment{};
        bool hasColorAttachment = false;
        bool hasDepthAttachment = false;
        std::uint32_t renderWidth = 0;
        std::uint32_t renderHeight = 0;

        const auto transition = [&](const FrameGraphTextureInfo& texture,
                                    Velos::RHI::ImageLayout layout) {
            if (!texture.handle.IsValid()) {
                throw std::logic_error("Pass '" + node->name +
                                       "' references an invalid image");
            }
            const Velos::RHI::ImageLayout oldLayout =
                builder_->Device().GetImageLayout(texture.handle, 0);
            if (oldLayout != layout) {
                commandList.Barrier({
                    .image = texture.handle,
                    .oldLayout = oldLayout,
                    .newLayout = layout,
                    .aspect = ImageAspectFor(texture.format),
                });
            }
        };

        const auto addAttachment = [&](const FrameGraphResource& use,
                                       const FrameGraphResource& resource) {
            const auto* texture = std::get_if<FrameGraphTextureInfo>(&resource.info);
            if (texture == nullptr || !texture->view.IsValid()) {
                throw std::logic_error("Attachment '" + resource.name +
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
                transition(*texture, Velos::RHI::ImageLayout::DepthAttachment);
                depthAttachment.view = texture->view;
                depthAttachment.loadOp = ToLoadOp(texture->loadOp);
                depthAttachment.storeOp = Velos::RHI::StoreOp::Store;
                depthAttachment.clearDepth = texture->clearDepth;
                depthAttachment.clearStencil = texture->clearStencil;
                hasDepthAttachment = true;
            } else {
                if (hasColorAttachment) {
                    throw std::logic_error(
                        "Velos currently supports one color attachment per pass");
                }
                transition(*texture, Velos::RHI::ImageLayout::ColorAttachment);
                colorAttachment.view = texture->view;
                colorAttachment.loadOp = ToLoadOp(texture->loadOp);
                colorAttachment.storeOp = Velos::RHI::StoreOp::Store;
                colorAttachment.clearValue = texture->clearColor;
                hasColorAttachment = true;
            }
            (void)use;
        };

        for (FrameGraphResourceHandle inputHandle : node->inputs) {
            const FrameGraphResource* input = AccessResource(inputHandle);
            const FrameGraphResource* resource = input->outputHandle.IsValid()
                ? AccessResource(input->outputHandle)
                : input;
            if (resource == nullptr) continue;

            if (input->type == FrameGraphResourceType::Texture) {
                if (const auto* texture =
                        std::get_if<FrameGraphTextureInfo>(&resource->info)) {
                    transition(*texture, Velos::RHI::ImageLayout::ShaderReadOnly);
                }
            } else if (input->type == FrameGraphResourceType::Attachment) {
                addAttachment(*input, *resource);
            }
        }

        for (FrameGraphResourceHandle outputHandle : node->outputs) {
            const FrameGraphResource* output = AccessResource(outputHandle);
            if (output->type == FrameGraphResourceType::Attachment) {
                addAttachment(*output, *output);
            }
        }

        const bool hasAttachments = hasColorAttachment || hasDepthAttachment;
        if (hasAttachments) {
            commandList.SetViewport({
                .width = static_cast<float>(renderWidth),
                .height = static_cast<float>(renderHeight),
            });
            commandList.SetScissor({ .extent = { renderWidth, renderHeight } });
            commandList.BeginRendering({
                .renderArea = { .extent = { renderWidth, renderHeight } },
                .colorAttachments = hasColorAttachment ? &colorAttachment : nullptr,
                .colorAttachmentCount = hasColorAttachment ? 1u : 0u,
                .depthAttachment = hasDepthAttachment ? &depthAttachment : nullptr,
            });
        }

        try {
            node->graphRenderPass->Render(commandList, scene);
        } catch (...) {
            if (hasAttachments) commandList.EndRendering();
            throw;
        }
        if (hasAttachments) commandList.EndRendering();
    }
}

void FrameGraph::Render(const RenderScene& scene)
{
    if (builder_ == nullptr) {
        throw std::logic_error("FrameGraph is not initialized");
    }

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
        waits.reserve(batch.dependencies.size());
        for (std::size_t dependency : batch.dependencies) {
            if (dependency >= batchIndex ||
                !batchSignals[dependency].semaphore.IsValid()) {
                throw std::logic_error(
                    "Frame-graph queue dependency was not submitted");
            }
            if (executionBatches_[dependency].queue != batch.queue) {
                waits.push_back(batchSignals[dependency]);
            }
        }

        QueueTimelineState& timeline = TimelineFor(batch.queue);
        Velos::RHI::ICommandList& queueCommands =
            builder_->Device().AcquireCommandList(batch.queue);
        queueCommands.Begin();
        RecordBatch(batch, queueCommands, scene);
        queueCommands.End();

        if (timeline.nextSignalValue ==
            std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("Frame-graph queue timeline exhausted");
        }
        const Velos::RHI::TimelineSemaphorePoint signal{
            timeline.semaphore, timeline.nextSignalValue++ };
        const Velos::RHI::TimelineSemaphorePoint signals[] = { signal };
        builder_->Device().Submit(batch.queue, queueCommands, {
            .waits = waits,
            .signals = signals,
        });
        batchSignals[batchIndex] = signal;
    }

    for (std::size_t batchIndex = 0;
         batchIndex < executionBatches_.size(); ++batchIndex) {
        if (executionBatches_[batchIndex].queue !=
            Velos::RHI::QueueType::Graphics) {
            appendGraphicsWait(batchSignals[batchIndex]);
        }
    }
}

void FrameGraph::OnResize(
    Velos::RHI::IDevice& device, std::uint32_t width, std::uint32_t height)
{
    if (builder_ == nullptr) return;
    if (&device != &builder_->Device()) {
        throw std::invalid_argument("FrameGraph resized with a different device");
    }
    builder_->RecreateResizableResources(width, height);

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
