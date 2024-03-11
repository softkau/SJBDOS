#include "graphics.hpp"
#include "font.hpp"

void RGBPixelWriter::Write(Vector2D<int> pos, const PixelColor& c) {
	auto pixel = this->PixelAt(pos.x, pos.y);
	pixel[0] = c.r;
	pixel[1] = c.g;
	pixel[2] = c.b;
}

void BGRPixelWriter::Write(Vector2D<int> pos, const PixelColor& c) {
	auto pixel = this->PixelAt(pos.x, pos.y);
	pixel[0] = c.b;
	pixel[1] = c.g;
	pixel[2] = c.r;
}

void FillRect(PixelWriter& writer, const Vector2D<int>& pos, const Vector2D<int>& size, const PixelColor& c) {
	for (int dy = 0; dy < size.y; dy++)
		for (int dx = 0; dx < size.x; dx++)
			writer.Write({pos.x + dx, pos.y + dy}, c);
}
void DrawRect(PixelWriter& writer, const Vector2D<int>& pos, const Vector2D<int>& size, const PixelColor& c) {
	for (int dy = 0; dy < size.y; dy++) {
		writer.Write({pos.x,          pos.y + dy}, c);
		writer.Write({pos.x + size.x, pos.y + dy}, c);
	}
	for (int dx = 1; dx < size.x-1; dx++) {
		writer.Write({pos.x + dx, pos.y + size.y}, c);
		writer.Write({pos.x + dx, pos.y},          c);
	}
}
void FillRect(PixelWriter& writer, const Rectangle<int>& area, const PixelColor& c) {
	FillRect(writer, area.pos, area.size, c);
}
void DrawRect(PixelWriter& writer, const Rectangle<int>& area, const PixelColor& c) {
	DrawRect(writer, area.pos, area.size, c);
}

void DrawDesktop(PixelWriter& writer) {
	const int w = writer.Width();
	const int h = writer.Height();
	FillRect(writer, {0,0}, {w, h-50}, DESKTOP_BG_COLOR);
	FillRect(writer, {0,h-50}, {w,50}, {1,8,17});
	FillRect(writer, {0,h-50}, {w/5,50}, {80,80,80});
	DrawRect(writer, {10, h-40}, {30,30}, {160,160,160});
}

FrameBufferConfig kScreenConfig;
PixelWriter* kScreenWriter;

Vector2D<int> ScreenSize() {
	return {
		static_cast<int>(kScreenConfig.horizontal_resolution),
		static_cast<int>(kScreenConfig.vertical_resolution)
	};
}

namespace {
	constexpr size_t cxpr_max(size_t a, size_t b) { return a > b ? a : b; }
	char pixel_writer_buf[cxpr_max(sizeof(RGBPixelWriter), sizeof(BGRPixelWriter))];
}

void InitializeGraphics(const FrameBufferConfig& screen_config) {
	kScreenConfig = screen_config;

	switch (screen_config.pixel_format) {
		case kPixelRGBResv8BitPerColor:
			kScreenWriter = new(pixel_writer_buf) RGBPixelWriter{screen_config};
			break;
		case kPixelBGRResv8BitPerColor:
			kScreenWriter = new(pixel_writer_buf) BGRPixelWriter{screen_config};
			break;
		default:
			exit(1);
	}

	DrawDesktop(*kScreenWriter);
}
