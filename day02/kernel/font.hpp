#pragma once

#include "graphics.hpp"
#include "error.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H

struct PixelColor;
class PixelWriter;

namespace font {
constexpr int FONT_WIDTH = 8;
constexpr int FONT_HEIGHT = 16;
constexpr Vector2D<int> FONT_SIZE { FONT_WIDTH, FONT_HEIGHT };

void InitFont();

int CountUTF8Size(uint8_t c);
std::pair<char32_t, int> ConvertUTF8to32(const char* u8);
void WriteASCII(PixelWriter& writer, int x, int y, char c, const PixelColor& color);
void WriteASCII(PixelWriter& writer, Vector2D<int> xy, char c, const PixelColor& color);
void WriteString(PixelWriter& writer, int x, int y, const char* str, const PixelColor& color);
void WriteString(PixelWriter& writer, Vector2D<int> xy, const char* str, const PixelColor& color);
Error WriteUnicode(PixelWriter& writer, Vector2D<int> xy, char32_t c, const PixelColor& color);
bool IsHankaku(char32_t c);
};
