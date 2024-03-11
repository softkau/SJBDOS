#pragma once

#include <functional>
#include <memory>
#include "graphics.hpp"

class Window;

class Console {
public:
	static const int rows = 25, cols = 80;
	Console(const PixelColor& fg_color, const PixelColor& bg_color);

	void SetWindow(const std::shared_ptr<Window>& window);
	void SetWriter(PixelWriter* writer);
	void SetOnDraw(const std::function<void()>& callback = {});
	void refresh();

	/** prints string to console. @param str c-string */
	void puts(const char* str);

	/** clears console. */
	void clear();

private:
	/** adds new line */
	void nl();
	std::function<void()> callback = [](){};

	std::shared_ptr<Window> window = nullptr;
	PixelWriter* writer = nullptr;
	const PixelColor fg_color, bg_color;
	char buffer[rows][cols+1];
	int cursor_y, cursor_x;
};

extern Console* kConsole;

/* 콘솔을 초기화합니다(kConsole). InitializeGraphics가 선행되어야합니다. */
void InitializeConsole();
