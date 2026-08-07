#pragma once

#include <glm/glm.hpp>

namespace Iryven {
	struct Color {
		glm::vec4 value{ 0.0f, 0.0f, 0.0, 1.0f };

		constexpr Color() = default;

        constexpr Color(float r, float g, float b, float a = 1.0f)
            : value(r, g, b, a)
        {
        }

        constexpr explicit Color(const glm::vec4& value)
            : value(value)
        {
        }

        [[nodiscard]] constexpr float R() const { return value.r; }
        [[nodiscard]] constexpr float G() const { return value.g; }
        [[nodiscard]] constexpr float B() const { return value.b; }
        [[nodiscard]] constexpr float A() const { return value.a; }

        [[nodiscard]] constexpr const glm::vec4& Vector() const
        {
            return value;
        }

        static const Color Black;
        static const Color White;
        static const Color Gray;
        static const Color LightGray;
        static const Color DarkGray;
        static const Color Red;
        static const Color Green;
        static const Color Blue;
        static const Color Yellow;
        static const Color Cyan;
        static const Color Magenta;
        static const Color Orange;
        static const Color Purple;
        static const Color Pink;
        static const Color Brown;
        static const Color CornflowerBlue;
        static const Color Transparent;
	};

    inline constexpr Color Color::Black{ 0.0f, 0.0f, 0.0f, 1.0f };
    inline constexpr Color Color::White{ 1.0f, 1.0f, 1.0f, 1.0f };
    inline constexpr Color Color::Gray{ 0.5f, 0.5f, 0.5f, 1.0f };
    inline constexpr Color Color::LightGray{ 0.75f, 0.75f, 0.75f, 1.0f };
    inline constexpr Color Color::DarkGray{ 0.25f, 0.25f, 0.25f, 1.0f };
    inline constexpr Color Color::Red{ 1.0f, 0.0f, 0.0f, 1.0f };
    inline constexpr Color Color::Green{ 0.0f, 1.0f, 0.0f, 1.0f };
    inline constexpr Color Color::Blue{ 0.0f, 0.0f, 1.0f, 1.0f };
    inline constexpr Color Color::Yellow{ 1.0f, 1.0f, 0.0f, 1.0f };
    inline constexpr Color Color::Cyan{ 0.0f, 1.0f, 1.0f, 1.0f };
    inline constexpr Color Color::Magenta{ 1.0f, 0.0f, 1.0f, 1.0f };
    inline constexpr Color Color::Orange{ 1.0f, 0.647f, 0.0f, 1.0f };
    inline constexpr Color Color::Purple{ 0.5f, 0.0f, 0.5f, 1.0f };
    inline constexpr Color Color::Pink{ 1.0f, 0.753f, 0.796f, 1.0f };
    inline constexpr Color Color::Brown{ 0.647f, 0.165f, 0.165f, 1.0f };
    inline constexpr Color Color::CornflowerBlue{ 0.392f, 0.584f, 0.929f, 1.0f };
    inline constexpr Color Color::Transparent{ 0.0f, 0.0f, 0.0f, 0.0f };
}
