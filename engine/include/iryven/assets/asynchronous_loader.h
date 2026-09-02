#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <iryven/asset_manager.h>
#include <iryven/assets/asset_state.h>
#include <iryven/renderer/asset_upload_queue.h>
#include <TaskScheduler.h>

namespace Iryven {
class AsynchronousLoader {
public:
    explicit AsynchronousLoader(AssetManager& assets, AssetUploadQueue& uploads, std::uint32_t workerCount = 0);
    ~AsynchronousLoader();
    AssetHandle RequestModel(const std::filesystem::path& path);
    AssetHandle RequestTexture(const std::filesystem::path& path, TextureColorSpace colorSpace = TextureColorSpace::SRGB);
    AssetHandle RequestMaterial(const std::filesystem::path& path);
    AssetState GetState(AssetHandle handle) const;
    [[nodiscard]] ModelHandle GetModel(AssetHandle handle) const;
    void MarkGpuUploadComplete(AssetHandle handle);
    void MarkGpuUploadFailed(AssetHandle handle, std::string error);
private:
    struct LoadRequest {
        AssetHandle handle;
        AssetType type = AssetType::Model;
        std::filesystem::path path;
        TextureColorSpace colorSpace = TextureColorSpace::SRGB;
    };
    struct RunPinnedTaskLoopTask;
    struct AsynchronousLoadTask;

    AssetHandle Request(const std::filesystem::path&, AssetType, TextureColorSpace = TextureColorSpace::SRGB);
    void ProcessRequests();
    void ProcessRequest(const LoadRequest&) noexcept;
    AssetManager& assets_;
    AssetUploadQueue& uploads_;
    enki::TaskScheduler scheduler_;
    mutable std::mutex mutex_;
    std::condition_variable requestsAvailable_;
    bool stopping_ = false;
    std::atomic<std::uint64_t> next_{1};
    std::unordered_map<AssetHandle, AssetState> states_;
    std::unordered_map<AssetHandle, TextureColorSpace> textureColorSpaces_;
    std::deque<LoadRequest> requests_;
    std::unique_ptr<RunPinnedTaskLoopTask> pinnedTaskLoop_;
    std::unique_ptr<AsynchronousLoadTask> asynchronousLoadTask_;
};
}
