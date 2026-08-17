#include <iryven/assets/font.h>
#include <msdfgen/msdfgen.h>
#include <msdf-atlas-gen/msdf-atlas-gen.h>
#include <iryven/log.h>

namespace Iryven {


	Font::Font(const std::filesystem::path& path)
	{
		msdfgen::FreetypeHandle* freeType = msdfgen::initializeFreetype();

		if (!freeType) return;

		msdfgen::FontHandle* font = msdfgen::loadFont(freeType, path.string().c_str());

		if (!font) {
			IRYVEN_CORE_ERROR("Font at {} not found.", path.string());
			msdfgen::deinitializeFreetype(freeType);
			return;
		}

		std::vector<msdf_atlas::GlyphGeometry> geometry;
		msdf_atlas::FontGeometry fontGeometry(&geometry);

		msdf_atlas::Charset charset;

		for (std::uint32_t codepoint = 0x20; codepoint <= 0x7E; ++codepoint)
			charset.add(codepoint);

		fontGeometry.loadCharset(font, 1.0, charset);

		const double maxCornerAngle = 3.0;
		for (msdf_atlas::GlyphGeometry& glyph : geometry)
			glyph.edgeColoring(&msdfgen::edgeColoringInkTrap, maxCornerAngle, 0);

		msdf_atlas::TightAtlasPacker packer;

		packer.setDimensionsConstraint(msdf_atlas::DimensionsConstraint::SQUARE);
		packer.setMinimumScale(48.0);

		packer.setPixelRange(4.0);
		packer.setMiterLimit(1.0);

		packer.pack(geometry.data(), geometry.size());

		int width = 0, height = 0;
		packer.getDimensions(width, height);

		msdf_atlas::ImmediateAtlasGenerator<
			float,
			4,
			msdf_atlas::mtsdfGenerator,
			msdf_atlas::BitmapAtlasStorage<msdf_atlas::byte, 4>
		> generator(width, height);

		msdf_atlas::GeneratorAttributes attributes;
		generator.setAttributes(attributes);
		generator.setThreadCount(4);

		generator.generate(geometry.data(), geometry.size());

		const msdfgen::BitmapConstRef<msdf_atlas::byte, 4> bitmap =
			generator.atlasStorage();

		atlasWidth_ = static_cast<std::uint32_t>(width);
		atlasHeight_ = static_cast<std::uint32_t>(height);

		const std::size_t byteCount =
			static_cast<std::size_t>(width) *
			static_cast<std::size_t>(height) * 4;

		atlasPixels_.assign(
			bitmap.pixels,
			bitmap.pixels + byteCount
		);

		for (const msdf_atlas::GlyphGeometry& source : geometry) {
			Glyph glyph;
			glyph.advance = source.getAdvance();

			source.getQuadPlaneBounds(
				glyph.planeBounds.x,
				glyph.planeBounds.y,
				glyph.planeBounds.z,
				glyph.planeBounds.w
			);

			source.getQuadAtlasBounds(
				glyph.atlasBounds.x,
				glyph.atlasBounds.y,
				glyph.atlasBounds.z,
				glyph.atlasBounds.w
			);

			glyphs_.emplace(
				static_cast<char32_t>(source.getCodepoint()),
				glyph
			);
		}

		const msdfgen::FontMetrics& sourceMetrics =
			fontGeometry.getMetrics();

		metrics_.ascender = sourceMetrics.ascenderY;
		metrics_.descender = sourceMetrics.descenderY;
		metrics_.lineHeight = sourceMetrics.lineHeight;

		msdfgen::destroyFont(font);

		msdfgen::deinitializeFreetype(freeType);

	}

	bool Font::IsValid() const
	{
		return atlasWidth_ > 0 &&
			atlasHeight_ > 0 &&
			!atlasPixels_.empty() &&
			!glyphs_.empty();
	}

	const FontMetrics& Font::GetMetrics() const
	{
		return metrics_;
	}

	const Glyph* Font::FindGlyph(char32_t codepoint) const
	{
		const auto iterator = glyphs_.find(codepoint);
		return iterator != glyphs_.end()
			? &iterator->second
			: nullptr;
	}

	std::span<const std::uint8_t> Font::GetAtlasPixels() const
	{
		return atlasPixels_;
	}

	std::uint32_t Font::GetAtlasWidth() const
	{
		return atlasWidth_;
	}

	std::uint32_t Font::GetAtlasHeight() const
	{
		return atlasHeight_;
	}

}
