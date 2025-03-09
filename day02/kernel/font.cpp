#include "font.hpp"
#include "graphics.hpp"
#include "fat.hpp"
#include <cstdint>
#include <vector>
#include "logger.hpp"

extern const uint8_t _binary_build_hankaku_bin_start;
extern const uint8_t _binary_build_hankaku_bin_end;
extern const uint8_t _binary_build_hankaku_bin_size;

namespace font {

namespace {
	FT_Library ft_library;
	std::vector<uint8_t>* nihongo_buf;
}

void InitFont() {
	if (int err = FT_Init_FreeType(&ft_library)) {
		exit(1);
	}

	auto [ entry, pos_slash ] = fat::FindFile("/nihongo.ttf");
	if (entry == nullptr || pos_slash) {
    Log(kError, "NO /nihongo.ttf IN DISK.");
    exit(1);
  }

	const size_t size = entry->dir_FileSize;
	nihongo_buf = new std::vector<uint8_t>(size);
	if (fat::LoadFile(nihongo_buf->data(), size, entry) != size) {
		delete nihongo_buf;
    Log(kError, "Error while loading nihongo.ttf");
		exit(1);
	}
}

WithError<FT_Face> NewFTFace() {
	FT_Face face;
	if (int err = FT_New_Memory_Face(ft_library, nihongo_buf->data(), nihongo_buf->size(), 0, &face)) {
		return { face, MakeError(Error::kFreeTypeError) };
	}
	if (int err = FT_Set_Pixel_Sizes(face, 16, 16)) {
		return { face, MakeError(Error::kFreeTypeError) };
	}
	return { face, MakeError(Error::kSuccess) };
}

Error RenderUnicode(char32_t c, FT_Face face) {
	const auto glyph_idx = FT_Get_Char_Index(face, c);
	if (glyph_idx == 0) {
		return MakeError(Error::kFreeTypeError);
	}

	if (int err = FT_Load_Glyph(face, glyph_idx, FT_LOAD_RENDER | FT_LOAD_TARGET_MONO)) {
		return MakeError(Error::kFreeTypeError);
	}
	return MakeError(Error::kSuccess);
}

static const uint8_t* GetFont(char c) {
	auto index = FONT_HEIGHT * static_cast<unsigned>(c);
	if (index >= reinterpret_cast<uintptr_t>(&_binary_build_hankaku_bin_size)) {
		return nullptr;
	}
	return &_binary_build_hankaku_bin_start + index;
}

int CountUTF8Size(uint8_t c) {
	if (c < 0x80) return 1;
	else if (0xc0 <= c && c < 0xe0) return 2;
	else if (0xe0 <= c && c < 0xf0) return 3;
	else if (0xf0 <= c && c < 0xf8) return 4;
	else return 1;
}


std::pair<char32_t, int> ConvertUTF8to32(const char* u8) {
	switch (CountUTF8Size(u8[0])) {
		case 1: return {
			static_cast<char32_t>(u8[0]),
			1
		};
		case 2: return {
			(static_cast<char32_t>(u8[0]) & 0b0001'1111) << 6 |
			(static_cast<char32_t>(u8[1]) & 0b0011'1111) << 0,
			2
		};
		case 3: return {
			(static_cast<char32_t>(u8[0]) & 0b0000'1111) << 12 |
			(static_cast<char32_t>(u8[1]) & 0b0011'1111) <<  6 |
			(static_cast<char32_t>(u8[2]) & 0b0011'1111) <<  0,
			3
		};
		case 4: return {
			(static_cast<char32_t>(u8[0]) & 0b0000'0111) << 18 |
			(static_cast<char32_t>(u8[1]) & 0b0011'1111) << 12 |
			(static_cast<char32_t>(u8[2]) & 0b0011'1111) <<  6 |
			(static_cast<char32_t>(u8[3]) & 0b0011'1111) <<  0,
			4
		};
		default: return { 0, 1 };
	}
}

void WriteASCII(PixelWriter& writer, int x, int y, char c, const PixelColor& color) {
	const uint8_t* font = GetFont(c);
	
	for (int i = 0; i < FONT_HEIGHT; i++)
		for (int j = 0; j < FONT_WIDTH; j++)
			if ((font[i] << j) & 0x80u)
				writer.Write({ x + j, y + i }, color);
}

void WriteASCII(PixelWriter& writer, Vector2D<int> xy, char c, const PixelColor& color) {
	WriteASCII(writer, xy.x, xy.y, c, color);
}

void WriteString(PixelWriter& writer, int x, int y, const char* str, const PixelColor& color) {
	int i = 0;
	while (*str) {
		const auto [ u32, bytes ] = ConvertUTF8to32(str);
		WriteUnicode(writer, Vector2D<int>{ x + i * FONT_WIDTH, y }, u32, color);
		str += bytes;
		i += (IsHankaku(u32) ? 1 : 2);
	}
}

void WriteString(PixelWriter& writer, Vector2D<int> xy, const char* str, const PixelColor& color) {
	WriteString(writer, xy.x, xy.y, str, color);
}

Error WriteUnicode(PixelWriter& writer, Vector2D<int> xy, char32_t c, const PixelColor& color) {
	if (c < 0x7f) {
		WriteASCII(writer, xy, c, color);
		return MakeError(Error::kSuccess);
	}

	auto [ face, err ] = NewFTFace();
	if (err) {
		WriteASCII(writer, xy, '?', color);
		WriteASCII(writer, xy + Vector2D<int>{FONT_WIDTH, 0}, '?', color);
		return err;
	}
	if (auto err = RenderUnicode(c, face)) {
		FT_Done_Face(face);
		WriteASCII(writer, xy, '?', color);
		WriteASCII(writer, xy + Vector2D<int>{FONT_WIDTH, 0}, '?', color);
		return err;
	}
	FT_Bitmap& bitmap = face->glyph->bitmap;

	const int baseline = (face->height + face->descender) * face->size->metrics.y_ppem / face->units_per_EM;
	const auto glyph_topleft = xy + Vector2D<int> { face->glyph->bitmap_left, baseline - face->glyph->bitmap_top };

	for (int dy = 0; dy < bitmap.rows; ++dy) {
		unsigned char* q = &bitmap.buffer[bitmap.pitch * dy];
		if (bitmap.pitch < 0) {
			q -= bitmap.pitch * bitmap.rows;
		}
		for (int dx = 0; dx < bitmap.width; ++dx) {
			const bool b = q[dx >> 3] & (0x80 >> (dx & 0x7));
			if (b) {
				writer.Write(glyph_topleft + Vector2D<int>{dx, dy}, color);
			}
		}
	}

	FT_Done_Face(face);
	return MakeError(Error::kSuccess);
}

bool IsHankaku(char32_t c) {
	return c <= 0x7f;
}

};