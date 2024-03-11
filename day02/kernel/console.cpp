#include <cstring>

#include "window.hpp"
#include "console.hpp"
#include "font.hpp"

Console::Console(const PixelColor& fg_color, const PixelColor& bg_color)
	: fg_color(fg_color), bg_color(bg_color),
	buffer{}, cursor_y(0), cursor_x(0) {
}

void Console::SetWindow(const std::shared_ptr<Window>& window) {
	if (this->window == window) return;
	this->window = window;
	this->writer = window->Writer();
	refresh();
}

void Console::SetWriter(PixelWriter* writer) {
	if (this->writer == writer) return;

	this->writer = writer;
	refresh();
}

void Console::SetOnDraw(const std::function<void()>& callback) {
	this->callback = callback;
}

void Console::refresh() {
	kConsole->clear();
	for (int i = 0; i < rows; i++) {
		font::WriteString(*writer, 0, font::FONT_HEIGHT * i, buffer[i], fg_color);
	}
}

void Console::puts(const char* str) {
	while (*str) {
		int x = cursor_x * font::FONT_WIDTH;
		int y = cursor_y * font::FONT_HEIGHT;
		switch (*str) {
		case '\n':
			nl();
			break;
		default:
			if (cursor_x < cols-1) {
				font::WriteASCII(*writer, x, y, *str, fg_color);
				buffer[cursor_y][cursor_x] = *str;
				cursor_x++;
			}
		}
		
		str++;
	}
	callback();
}

void Console::clear() {
	for (int y = 0; y < rows * font::FONT_HEIGHT; y++)
		for (int x = 0; x < cols * font::FONT_WIDTH; x++)
			writer->Write({x, y}, bg_color);
}

void Console::nl() {
	cursor_x = 0;
	if (cursor_y < rows - 1) {
		cursor_y++;
		return;
	}

	// scroll up when the terminal is full
	if (window) {
		Rect<int> sh_src = {{0,font::FONT_HEIGHT}, {font::FONT_WIDTH*cols, font::FONT_HEIGHT*(rows-1)}};
		window->Shift({0, 0}, sh_src);
		FillRect(*writer, {0, font::FONT_HEIGHT*(rows-1)}, {font::FONT_WIDTH*cols, font::FONT_HEIGHT}, bg_color);
	}
	else {
		clear();
		memmove(buffer[0], buffer[1], (cols+1) * (rows-1));
		memset(buffer[rows-1], 0, cols+1);
		for (int y = 0; y < rows-1; y++) {
			font::WriteString(*writer, 0, y * font::FONT_HEIGHT, buffer[y], fg_color);
		}
	}
}

Console* kConsole;

namespace {
	char console_buf[sizeof(Console)];
}

void InitializeConsole() {
	kConsole = new(console_buf) Console{ DESKTOP_FG_COLOR, DESKTOP_BG_COLOR };
	kConsole->SetWriter(kScreenWriter);
}