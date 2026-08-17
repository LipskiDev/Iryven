#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <glm/ext/vector_double4.hpp>
#include <span>
#include <unordered_map>
#include <vector>

namespace Iryven {

	struct Glyph {
		double advance = 0.0;

		glm::dvec4 planeBounds{};

		glm::dvec4 atlasBounds{};
	};

	struct FontMetrics {
		double ascender = 0.0;
		double descender = 0.0;
		double lineHeight = 0.0;
	};

	class Font {
	public:
		explicit Font(const std::filesystem::path& path);

		bool IsValid() const;

		const FontMetrics& GetMetrics() const;
		const Glyph* FindGlyph(char32_t codepoint) const;

		std::span<const std::uint8_t> GetAtlasPixels() const;
		std::uint32_t GetAtlasWidth() const;
		std::uint32_t GetAtlasHeight() const;

	private:
		std::vector<std::uint8_t> atlasPixels_;
		std::unordered_map<char32_t, Glyph> glyphs_;

		FontMetrics metrics_;
		std::uint32_t atlasWidth_ = 0;
		std::uint32_t atlasHeight_ = 0;
	};

	using FontHandle = std::shared_ptr<const Font>;
}
