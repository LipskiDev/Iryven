#pragma once

#include <cstdint>
#include <functional>
#include <limits>

namespace Iryven {

inline constexpr std::uint64_t InvalidHandleId = std::numeric_limits<std::uint64_t>::max();

template <typename Tag>
struct Handle {
    std::uint64_t id = InvalidHandleId;
    [[nodiscard]] constexpr bool IsValid() const noexcept { return id != InvalidHandleId; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsValid(); }
    friend constexpr bool operator==(const Handle&, const Handle&) = default;
};

} // namespace Iryven

template <typename Tag>
struct std::hash<Iryven::Handle<Tag>> {
    std::size_t operator()(const Iryven::Handle<Tag>& handle) const noexcept {
        return std::hash<std::uint64_t>{}(handle.id);
    }
};
