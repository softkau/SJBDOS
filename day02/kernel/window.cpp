#include "window.hpp"
#include "logger.hpp"
#include "font.hpp"

Window::Window(int width, int height, PixelFormat shadow_format) : width(width), height(height) {
	data.resize(width * height);

	FrameBufferConfig config{};
	config.frame_buffer = nullptr;
	config.horizontal_resolution = width;
	config.vertical_resolution = height;
	config.pixel_format = shadow_format;

	if (auto err = shadow_buffer.Init(config)) {
		Log(kError, "failed to initialize shadow buffer: %s at %s:%d\n",
			err.Name(), err.File(), err.Line());
	}
}

void Window::DrawTo(FrameBuffer& dst, Vector2D<int> pos, const Rectangle<int>& area) {
	if (!transparent_color) {
		Rect<int> window_area{ pos, this->Size() };
		Rect<int> intersection = area & window_area;
		dst.Copy(intersection.pos, shadow_buffer, { intersection.pos - pos, intersection.size });
		return;
	}

	const auto tc = *transparent_color;
	auto* framebuf = dst.Writer();

	int dy_beg = (pos.y < 0) ? -pos.y : 0;
	int dx_beg = (pos.x < 0) ? -pos.x : 0;
	int dy_end = (pos.y + height > framebuf->Height()) ? framebuf->Height() - pos.y : height;
	int dx_end = (pos.x + width  > framebuf->Width())  ? framebuf->Width()  - pos.x : width;

	for (int dy = dy_beg; dy < dy_end; dy++) {
		for (int dx = dx_beg; dx < dx_end; dx++) {
			const auto c = this->At(dx, dy);
			if (c != tc)
				framebuf->Write(pos + Vector2D<int>{dx, dy}, c);
		}
	}
}

void Window::Write(Vector2D<int> pos, const PixelColor& c) {
	At(pos.x, pos.y) = c;
	shadow_buffer.Writer()->Write(pos, c);
}

void Window::Shift(Vector2D<int> dst_pos, const Rectangle<int>& src) {
	shadow_buffer.Shift(dst_pos, src);
}

void Window::Deactivate() {

}
void Window::Activate() {

}

WindowRegion Window::GetWindowRegion(Vector2D<int> pos) const {
	return WindowRegion::Other;
}

void DrawTextbox(PixelWriter& writer, Vector2D<int> pos, Vector2D<int> size, uint32_t bg_color, uint32_t light_color, uint32_t shadow_color) {
	auto fill_rect = [&writer](Vector2D<int> pos, Vector2D<int> size, uint32_t c) {
		FillRect(writer, pos, size, ToColor(c));
	};

	fill_rect(pos + Vector2D<int>{1, 1}, size - Vector2D<int>{2, 2}, bg_color);

	fill_rect(pos, {size.x, 1}, shadow_color);
	fill_rect(pos, {1, size.y}, shadow_color);
	fill_rect(pos + Vector2D<int>{0, size.y}, {size.x, 1}, light_color);
	fill_rect(pos + Vector2D<int>{size.x, 0}, {1, size.y}, light_color);
}

void DrawTextbox(PixelWriter& writer, Vector2D<int> pos, Vector2D<int> size) {
	DrawTextbox(writer, pos, size, 0xffffff, 0xc6c6c6, 0x848484);
}

constexpr int CLOSE_BTN_WIDTH = 16;
constexpr int CLOSE_BTN_HEIGHT = 14;
const char close_button_gfx[CLOSE_BTN_HEIGHT][CLOSE_BTN_WIDTH+1] = {
	"...............@",
	".:::::::::::::$@",
	".:::::::::::::$@",
	".:::@@::::@@::$@",
	".::::@@::@@:::$@",
	".:::::@@@@::::$@",
	".::::::@@:::::$@",
	".:::::@@@@::::$@",
	".::::@@::@@:::$@",
	".:::@@::::@@::$@",
	".:::::::::::::$@",
	".:::::::::::::$@",
	".$$$$$$$$$$$$$$@",
	"@@@@@@@@@@@@@@@@"
};

TitleBarWindow::TitleBarWindow(const std::string& title, int width, int height, PixelFormat shadow_format)
	: Window(width, height, shadow_format), title(title) {
	DrawWindow(*Writer(), title.c_str());
}

void TitleBarWindow::Activate() {
	Window::Activate();
	DrawWindowTitle(*Writer(), title.c_str(), true);
}

void TitleBarWindow::Deactivate() {
	Window::Deactivate();
	DrawWindowTitle(*Writer(), title.c_str(), false);
}

WindowRegion TitleBarWindow::GetWindowRegion(Vector2D<int> pos) const {
	if (pos.x < 2 || pos.x >= Width() - 2 || pos.y < 2 || pos.y >= Height() - 2) return WindowRegion::Border;
	else if (pos.y < TitleBarWindow::TopLeftMargin.y) {
		if (pos.x >= Width() - 5 - CLOSE_BTN_WIDTH && pos.x < Width() - 5 &&
			pos.y >= 5 && pos.y < 5 + CLOSE_BTN_HEIGHT) return WindowRegion::CloseButton;
		else
			return WindowRegion::TitleBar;
	}
	return WindowRegion::Other;
}

void DrawWindow(PixelWriter& writer, const char* title) {
	auto fill_rect = [&writer](Vector2D<int> pos, Vector2D<int> size, uint32_t c) {
		FillRect(writer, pos, size, ToColor(c));
	};

	const auto win_w = writer.Width();
	const auto win_h = writer.Height();

	fill_rect({0, 0},         {win_w,1},              0xC6C6C6);
	fill_rect({1, 1},         {win_w - 2,1},          0xFFFFFF);
	fill_rect({0, 0},         {1, win_h},             0xC6C6C6);
	fill_rect({1, 1},         {1, win_h - 2},         0xFFFFFF);
	fill_rect({win_w - 2, 1}, {1, win_h - 2},         0x848484);
	fill_rect({win_w - 1, 0}, {1, win_h},             0x000000);
	fill_rect({2, 2},         {win_w - 4, win_h - 4}, 0xC6C6C6);
	//fill_rect({3, 3},         {win_w - 6, 18},        0x000084);
	fill_rect({1, win_h - 2}, {win_w - 2, 1},         0x848484);
	fill_rect({0, win_h - 1}, {win_w, 1},             0x000000);

	/*
	font::WriteString(writer, 24, 4, title, ToColor(0xFFFFFF));
	for (int y = 0; y < CLOSE_BTN_HEIGHT; y++)
		for (int x = 0; x < CLOSE_BTN_WIDTH; x++) {
			PixelColor c;
			switch (close_button_gfx[y][x]) {
				case '@': c = ToColor(0x000000); break;
				case '$': c = ToColor(0x848484); break;
				case ':': c = ToColor(0xC6C6C6); break;
				default:  c = ToColor(0xFFFFFF);
			}
			writer.Write({win_w - 5 - CLOSE_BTN_WIDTH + x, 5 + y}, c);
		}
	*/
	DrawWindowTitle(writer, title, false);
}


void DrawWindowTitle(PixelWriter& writer, const char* title, bool active) {
	auto fill_rect = [&writer](Vector2D<int> pos, Vector2D<int> size, uint32_t c) {
		FillRect(writer, pos, size, ToColor(c));
	};

	const auto win_w = writer.Width();
	//const auto win_h = writer.Height();

	fill_rect({3, 3}, {win_w - 6, 18}, (active ? 0x000084 : 0x848484));

	font::WriteString(writer, 24, 4, title, ToColor(0xFFFFFF));
	for (int y = 0; y < CLOSE_BTN_HEIGHT; y++)
		for (int x = 0; x < CLOSE_BTN_WIDTH; x++) {
			PixelColor c;
			switch (close_button_gfx[y][x]) {
				case '@': c = ToColor(0x000000); break;
				case '$': c = ToColor(0x848484); break;
				case ':': c = ToColor(0xC6C6C6); break;
				default:  c = ToColor(0xFFFFFF);
			}
			writer.Write({win_w - 5 - CLOSE_BTN_WIDTH + x, 5 + y}, c);
		}
}
