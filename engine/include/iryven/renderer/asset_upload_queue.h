#pragma once

#include <functional>
#include <mutex>
#include <vector>

#include <iryven/assets/asset_handle.h>
#include <iryven/material.h>
#include <iryven/model.h>
#include <iryven/texture.h>

namespace Iryven {

struct AssetUploadRequest {
    AssetHandle handle;
    ModelHandle model;
    TextureHandle texture;
    MaterialHandle material;
	// Time spent reading and deserializing/importing the asset on the worker.
	double cpuLoadMilliseconds = 0.0;
    std::function<void()> onComplete;
    std::function<void(std::string)> onFailure;
};

class AssetUploadQueue {
public:
    void Enqueue(AssetUploadRequest request);
    [[nodiscard]] std::vector<AssetUploadRequest> Drain();

private:
    std::mutex mutex_;
    std::vector<AssetUploadRequest> requests_;
};

} // namespace Iryven
