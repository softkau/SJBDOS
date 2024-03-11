#pragma once
#include "graphics.hpp"

constexpr int MOUSE_WIDTH = 15;
constexpr int MOUSE_HEIGHT = 24;

void DrawMouseCursor(PixelWriter* writer, const Vector2D<int>& pos, const PixelColor& outline, const PixelColor& fill);
void DrawMouseCursor(PixelWriter* writer, const PixelColor& outline, const PixelColor& fill_1, const PixelColor& fill_2);
unsigned InitializeMouse();
