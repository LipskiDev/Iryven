#pragma once

#include <iryven/events/event.h>

#include <spdlog/fmt/fmt.h>

#include <string>
#include <string_view>
#include <type_traits>

namespace fmt {
    template<typename T>
    struct formatter<
        T,
        char,
        std::enable_if_t<std::is_base_of_v<Iryven::Event, T>>>
        : formatter<std::string_view>
    {
        template<typename FormatContext>
        auto format(const T& event, FormatContext& context) const
        {
            const std::string text = event.ToString();

            return formatter<std::string_view>::format(
                text,
                context
            );
        }
    };

}