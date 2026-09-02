#include <iryven/renderer/asset_upload_queue.h>

namespace Iryven {

void AssetUploadQueue::Enqueue(AssetUploadRequest request)
{
    std::scoped_lock lock(mutex_);
    requests_.push_back(std::move(request));
}

std::vector<AssetUploadRequest> AssetUploadQueue::Drain()
{
    std::scoped_lock lock(mutex_);
    std::vector<AssetUploadRequest> requests;
    requests.swap(requests_);
    return requests;
}

} // namespace Iryven
