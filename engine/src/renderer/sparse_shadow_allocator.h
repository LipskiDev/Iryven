#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace Iryven {

// Assigns a fixed physical page budget to projected-influence-sorted lights.
// The Vulkan backend maps these page indices into the sparse cube array.
class SparseShadowAllocator {
public:
    struct LightRequest {
        std::uint32_t lightIndex = 0;
        std::uint32_t shadowSlot = 0;
        std::uint32_t requestedResolution = 0;
    };

    struct Allocation {
        std::uint32_t lightIndex = 0;
        std::uint32_t shadowSlot = 0;
        std::uint32_t resolution = 0;
        std::uint32_t firstPage = 0;
        std::uint32_t pageCount = 0;
    };

    void Configure(std::uint32_t pageWidth, std::uint32_t pageHeight,
        std::uint32_t availablePages);
    [[nodiscard]] std::span<const Allocation> BuildPlan(
        std::span<const LightRequest> requests);
    void Reset();

private:
    std::uint32_t pageWidth_ = 0;
    std::uint32_t pageHeight_ = 0;
    std::uint32_t availablePages_ = 0;
    std::vector<Allocation> allocations_;
};

} // namespace Iryven
