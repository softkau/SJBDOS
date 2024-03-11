#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../syscall.h"

static const int kWidth = 200, kHeight = 130;

bool IsInside(int x, int y) {
	return 4 <= x && x < 4 + kWidth && 24 <= y && y < 24 + kHeight;
}

extern "C" void main(int argc, char** argv) {
	auto [layer_id, err] = SyscallOpenWindow(kWidth + 8, kHeight + 28, 10, 10, "paint");
	if (err) {
		exit(err);
	}

	AppEvent event;
	bool press = false;
	while (true) {
		auto [ n, err ] = SyscallReadEvent(&event, 1);
		if (err) {
			printf("revnt failed: %s\n", strerror(err));
			break;
		}
		if (event.type == AppEvent::kQuit) break;
		else if (event.type == AppEvent::kMouseMove) {
			auto& arg = event.arg.mouse_move;
			const auto px = arg.x - arg.dx;
			const auto py = arg.y - arg.dy;
			if (press && IsInside(px, py) && IsInside(arg.x, arg.y)) {
				SyscallWinDrawLine(layer_id, px, py, arg.x, arg.y, 0x000000);
			}
		}
		else if (event.type == AppEvent::kMouseButton) {
			auto& arg = event.arg.mouse_button;
			if (arg.button == 0) { // Lclick
				press = arg.press;
				SyscallWinFillRect(layer_id, arg.x, arg.y, 1, 1, 0x000000); // draw dot
			}
		}
		else {
			printf("unknown event: %d\n", event.type);
		}
	}
	SyscallCloseWindow(layer_id);
	exit(0);
}