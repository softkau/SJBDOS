#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include "../syscall.h"

static const int kCanvasSize = 100, kEyeSize = 10;

void DrawEye(uint64_t layerID_flg, int mx, int my, uint32_t color) {
	const double center_x = mx - (kCanvasSize/2 + 4);
	const double center_y = my - (kCanvasSize/2 + 24);

	const double angle = atan2(center_y, center_x);
	double distance = sqrt(center_x * center_x + center_y * center_y);
	distance = std::min<double>(distance, kCanvasSize/2 - kEyeSize/2);

	const double eye_center_x = cos(angle) * distance;
	const double eye_center_y = sin(angle) * distance;
	const int eye_x = static_cast<int>(eye_center_x) + (kCanvasSize/2 + 4);
	const int eye_y = static_cast<int>(eye_center_y) + (kCanvasSize/2 + 24);

	SyscallWinFillRect(layerID_flg, eye_x - kEyeSize/2, eye_y - kEyeSize/2, kEyeSize, kEyeSize, color);
}

extern "C" void main(int argc, void** argv) {
	auto [layerID, err] = SyscallOpenWindow(kCanvasSize + 8, kCanvasSize + 28, 10, 10, "eye");
	if (err) {
		exit(err);
	}

	SyscallWinFillRect(layerID, 4, 24, kCanvasSize, kCanvasSize, 0xffffff);

	AppEvent e;
	while (true) {
		auto [n, err] = SyscallReadEvent(&e, 1);
		if (err) {
			printf("ReadEvent failed: %s\n", strerror(err));
			break;
		}
		if (e.type == AppEvent::kQuit) {
			break;
		}
		else if (e.type == AppEvent::kMouseMove) {
			auto& arg = e.arg.mouse_move;
			SyscallWinFillRect(layerID | LAYER_NO_DRAW, 4, 24, kCanvasSize, kCanvasSize, 0xffffff);
			printf("X: %d, Y: %d\n", arg.x, arg.y);
			DrawEye(layerID, arg.x, arg.y, 0x000000);
		}
		else {
			printf("unknown event: type = %d\n", e.type);
		}
	}
	SyscallCloseWindow(layerID);
	exit(0);
}