#include <cmath>
#include <cstdlib>
#include <random>
#include "../syscall.h"

static constexpr int kRadius = 90;

const uint32_t Color(int deg) {
	int d30 = deg / 30;
	switch (d30) {
	case 0:  return (255 *       (deg) / 30) <<  8 | 0xff0000; // 0 ~ 29
	case 1:  return (255 *  (60 - deg) / 30) << 16 | 0x00ff00; // 30 ~ 59
	case 2:  return (255 *  (deg - 60) / 30)       | 0x00ff00; // 60 ~ 89
	case 3:  return (255 * (120 - deg) / 30) <<  8 | 0x0000ff; // 90 ~ 119
	case 4:  return (255 * (deg - 120) / 30) << 16 | 0x0000ff; // 120 ~ 149
	default: return (255 * (180 - deg) / 30)       | 0xff0000; // 150 ~ 180
	}
}

extern "C" void main(int argc, char** argv) {
	auto [layer_id, err] = SyscallOpenWindow(kRadius * 2 + 10 + 8, kRadius + 28, 10, 10, "lines");

	if (err) {
		exit(err);
	}
	
	const auto rad = [](double deg) -> double { return M_PI * deg / 180.0; };
	const int x0 = 4, y0 = 24, x1 = 4 + kRadius + 10, y1 = 24 + kRadius; // start positions
	for (int deg = 0; deg <= 90; deg += 5) {
		const int x = kRadius * cos(rad(deg));
		const int y = kRadius * sin(rad(deg));
		SyscallWinDrawLine(layer_id, x0, y0, x0 + x, y0 + y, Color(deg));
		SyscallWinDrawLine(layer_id, x1, y1, x1 + x, y1 - y, Color(deg + 90));
	}

	exit(0);
}