#include <iryven/assets/asynchronous_loader.h>

#include <algorithm>
#include <thread>

namespace Iryven {
    struct AsynchronousLoader::RunPinnedTaskLoopTask final : enki::IPinnedTask {
        explicit RunPinnedTaskLoopTask(enki::TaskScheduler& scheduler) : scheduler(scheduler) {}

        void Execute() override {
            while (!scheduler.GetIsShutdownRequested()) {
                scheduler.WaitForNewPinnedTasks();
                scheduler.RunPinnedTasks();
            }
        }

        enki::TaskScheduler& scheduler;
    };

    struct AsynchronousLoader::AsynchronousLoadTask final : enki::IPinnedTask {
        explicit AsynchronousLoadTask(AsynchronousLoader& loader) : loader(loader) {}

        void Execute() override { loader.ProcessRequests(); }

        AsynchronousLoader& loader;
    };

    AsynchronousLoader::AsynchronousLoader(
        AssetManager& assets,
        AssetUploadQueue& uploads,
        std::uint32_t workers)
        : assets_(assets), uploads_(uploads) {
        enki::TaskSchedulerConfig config;
        const auto defaultWorkers = std::max(2u, std::thread::hardware_concurrency());
        config.numTaskThreadsToCreate = std::max(2u, workers == 0 ? defaultWorkers : workers);
        scheduler_.Initialize(config);

        const auto ioThread = scheduler_.GetNumTaskThreads() - 1;
        pinnedTaskLoop_ = std::make_unique<RunPinnedTaskLoopTask>(scheduler_);
        pinnedTaskLoop_->threadNum = ioThread;
        scheduler_.AddPinnedTask(pinnedTaskLoop_.get());

        asynchronousLoadTask_ = std::make_unique<AsynchronousLoadTask>(*this);
        asynchronousLoadTask_->threadNum = ioThread;
        scheduler_.AddPinnedTask(asynchronousLoadTask_.get());
    }

    AsynchronousLoader::~AsynchronousLoader() {
        {
            std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        requestsAvailable_.notify_all();
        scheduler_.WaitforAllAndShutdown();
    }

    AssetHandle AsynchronousLoader::RequestModel(const std::filesystem::path& p) {
        return Request(p, AssetType::Model);
    }

    AssetHandle AsynchronousLoader::RequestTexture(const std::filesystem::path& p, TextureColorSpace c) {
        return Request(p, AssetType::Texture, c);
    }

    AssetHandle AsynchronousLoader::RequestMaterial(const std::filesystem::path& p) {
        return Request(p, AssetType::Material);
    }

    AssetState AsynchronousLoader::GetState(AssetHandle handle) const {
        std::scoped_lock lock(mutex_);
        const auto found = states_.find(handle);
        return found == states_.end() ? AssetState{} : found->second;
    }

    ModelHandle AsynchronousLoader::GetModel(AssetHandle handle) const {
        const AssetState state = GetState(handle);
        if (state.type != AssetType::Model || state.state != LoadingState::Ready) return {};
        return assets_.GetModel(state.path);
    }

    void AsynchronousLoader::MarkGpuUploadComplete(AssetHandle handle) {
        std::scoped_lock lock(mutex_);
        auto& state = states_.at(handle);
        if (state.state == LoadingState::WaitingGpu) state.state = LoadingState::Ready;
    }

    void AsynchronousLoader::MarkGpuUploadFailed(AssetHandle handle, std::string error) {
        std::scoped_lock lock(mutex_);
        auto& state = states_.at(handle);
        state.state = LoadingState::Failed;
        state.error = std::move(error);
    }

    AssetHandle AsynchronousLoader::Request(
        const std::filesystem::path& path,
        AssetType type,
        TextureColorSpace colorSpace) {
        const auto normalizedPath = AssetManager::NormalizePath(path);

        AssetHandle handle;
        {
            std::scoped_lock lock(mutex_);
            for (const auto& [existingHandle, state] : states_) {
                if (state.path != normalizedPath || state.type != type) continue;
                if (type != AssetType::Texture || textureColorSpaces_.at(existingHandle) == colorSpace)
                    return existingHandle;
            }

            handle = AssetHandle{ next_.fetch_add(1, std::memory_order_relaxed) };
            states_.emplace(handle, AssetState{
                .asset = handle,
                .path = normalizedPath,
                .type = type,
                .state = LoadingState::Queued,
            });
            if (type == AssetType::Texture) textureColorSpaces_.emplace(handle, colorSpace);
            requests_.push_back({handle, type, normalizedPath, colorSpace});
        }

        requestsAvailable_.notify_one();
        return handle;
    }

    void AsynchronousLoader::ProcessRequests() {
        while (true) {
            LoadRequest request;
            {
                std::unique_lock lock(mutex_);
                requestsAvailable_.wait(lock, [this] { return stopping_ || !requests_.empty(); });
                if (stopping_ && requests_.empty()) return;
                request = std::move(requests_.front());
                requests_.pop_front();
            }
            ProcessRequest(request);
        }
    }

    void AsynchronousLoader::ProcessRequest(const LoadRequest& request) noexcept {
        try {
            {
                std::scoped_lock lock(mutex_);
                states_.at(request.handle).state = LoadingState::LoadingCpu;
            }

            switch (request.type) {
            case AssetType::Model: {
                AssetUploadRequest uploadRequest{
                    .handle = request.handle,
                    .model = assets_.LoadModel(request.path),
                    .onComplete = [this, handle = request.handle] { MarkGpuUploadComplete(handle); },
                    .onFailure = [this, handle = request.handle](std::string error) {
                        MarkGpuUploadFailed(handle, std::move(error));
                    },
                };
                {
                    std::scoped_lock lock(mutex_);
                    states_.at(request.handle).state = LoadingState::WaitingGpu;
                }
                uploads_.Enqueue(std::move(uploadRequest));
                break;
            }
            case AssetType::Texture: {
                [[maybe_unused]] const auto texture = assets_.LoadTexture(request.path, request.colorSpace);
                break;
            }
            case AssetType::Material: {
                [[maybe_unused]] const auto material = assets_.LoadMaterial(request.path);
                break;
            }
            }

            if (request.type != AssetType::Model) {
                std::scoped_lock lock(mutex_);
                states_.at(request.handle).state = LoadingState::Ready;
            }
        }
        catch (const std::exception& exception) {
            std::scoped_lock lock(mutex_);
            auto& state = states_.at(request.handle);
            state.state = LoadingState::Failed;
            state.error = exception.what();
        }
        catch (...) {
            std::scoped_lock lock(mutex_);
            auto& state = states_.at(request.handle);
            state.state = LoadingState::Failed;
            state.error = "Unknown asset loading failure";
        }
    }

}
