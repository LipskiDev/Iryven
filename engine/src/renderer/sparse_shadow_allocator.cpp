#include "sparse_shadow_allocator.h"

#include <algorithm>
#include <numeric>

namespace Iryven {

void SparseShadowAllocator::Configure(std::uint32_t pageWidth,
    std::uint32_t pageHeight, std::uint32_t availablePages)
{
    pageWidth_ = pageWidth;
    pageHeight_ = pageHeight;
    availablePages_ = availablePages;
    allocations_.clear();
}

std::span<const SparseShadowAllocator::Allocation>
SparseShadowAllocator::BuildPlan(std::span<const LightRequest> requests)
{
    allocations_.clear();
    if (pageWidth_ == 0 || pageHeight_ == 0) return allocations_;

	// Requests arrive in descending visual priority. Preserve every light at
	// the smallest supported mip before allowing a few large maps to exhaust
	// the pool and make lower-priority lights blink on and off.
	std::vector<LightRequest> fitted(requests.begin(), requests.end());
	const auto pageCount = [this](std::uint32_t resolution) {
		const auto blocksX = (resolution + pageWidth_ - 1) / pageWidth_;
		const auto blocksY = (resolution + pageHeight_ - 1) / pageHeight_;
		return blocksX * blocksY * 6u;
	};
	auto totalPages = [&] {
		return std::accumulate(fitted.begin(), fitted.end(), std::uint64_t{0},
			[&](std::uint64_t total, const LightRequest& request) {
				return total + pageCount(request.requestedResolution);
			});
	};

	std::uint64_t requiredPages = totalPages();
	while (requiredPages > availablePages_) {
		auto request = std::find_if(fitted.rbegin(), fitted.rend(),
			[](const LightRequest& candidate) {
				return candidate.requestedResolution > 128u;
			});
		if (request == fitted.rend()) break;
		const auto oldPages = pageCount(request->requestedResolution);
		request->requestedResolution /= 2u;
		requiredPages -= oldPages - pageCount(request->requestedResolution);
	}
	while (requiredPages > availablePages_ && !fitted.empty()) {
		requiredPages -= pageCount(fitted.back().requestedResolution);
		fitted.pop_back();
	}

	// Physical page offsets now depend on persistent cubemap slots rather than
	// projected-influence sorting, avoiding a full sparse remap when two nearby
	// lights exchange priority.
	std::ranges::sort(fitted, {}, &LightRequest::shadowSlot);
    std::uint32_t nextPage = 0;
	for (const auto& request : fitted) {
		const auto pages = pageCount(request.requestedResolution);
        allocations_.push_back({request.lightIndex, request.shadowSlot,
            request.requestedResolution, nextPage, pages});
        nextPage += pages;
    }
    return allocations_;
}

void SparseShadowAllocator::Reset()
{
    allocations_.clear();
}

} // namespace Iryven
