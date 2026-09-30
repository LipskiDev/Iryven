#pragma once

#include <iryven/rendering/render_camera.h>
#include <iryven/rendering/render_light.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

namespace Iryven {

struct alignas(16) LightDepthBins {
    static constexpr std::uint32_t kCount = 256;
    static constexpr std::uint32_t kEmpty = 0x0000ffffu;
    glm::vec4 depthPlane{0, 0, -1, 0};
    glm::vec4 mapping{0, 1, float(kCount), 0}; // near, far, bins per depth unit, reserved
    std::array<std::uint32_t, kCount> ranges{};
};
static_assert(sizeof(LightDepthBins) == 32 + 256 * 4);

inline LightDepthBins BuildLightDepthBins(
    const RenderCamera& camera, std::span<const RenderLight> sortedLocalLights)
{
    LightDepthBins result;
    result.ranges.fill(LightDepthBins::kEmpty);
    result.depthPlane = -glm::vec4(camera.view[0][2], camera.view[1][2],
        camera.view[2][2], camera.view[3][2]);
    if (std::isfinite(camera.nearPlane) && std::isfinite(camera.farPlane) &&
        camera.farPlane > camera.nearPlane) {
        result.mapping = {camera.nearPlane, camera.farPlane,
            float(LightDepthBins::kCount) / (camera.farPlane - camera.nearPlane), 0};
    }
    const auto binForDepth = [&](float depth) {
        return static_cast<std::uint32_t>(std::clamp(
            (depth - result.mapping.x) * result.mapping.z, 0.0f,
            float(LightDepthBins::kCount - 1)));
    };
    for (std::uint32_t index = 0; index < sortedLocalLights.size(); ++index) {
        const auto& light = sortedLocalLights[index];
        const float depth = glm::dot(result.depthPlane, glm::vec4(light.position, 1));
        const float radius = std::max(light.range, 0.0001f);
        std::uint32_t first = 0, last = LightDepthBins::kCount - 1;
        if (std::isfinite(depth) && std::isfinite(radius)) {
            const float padding = std::max(0.0001f, 8 * std::numeric_limits<float>::epsilon() *
                std::max(std::abs(depth), radius));
            const float low = depth - radius - padding, high = depth + radius + padding;
            if (high < result.mapping.x || low > result.mapping.y) continue;
            first = binForDepth(low);
            last = binForDepth(high);
        }
        for (auto bin = first; bin <= last; ++bin) {
            const auto minimum = std::min(result.ranges[bin] & 0xffffu, index);
            const auto maximum = std::max(result.ranges[bin] >> 16u, index);
            result.ranges[bin] = minimum | (maximum << 16u);
        }
    }
    return result;
}

} // namespace Iryven
