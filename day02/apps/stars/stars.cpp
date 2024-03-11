#include <cstdlib>
#include <random>
#include "../syscall.h"

static constexpr int kWidth = 100, kHeight = 100;

extern "C" void main(int argc, char** argv) {
	auto [layerID, err] = SyscallOpenWindow(kWidth + 8, kHeight + 28, 10, 10, "stars");
	if (err) {
		exit(err);
	}

	SyscallWinFillRect(layerID, 4, 24, kWidth, kHeight, 0x000000);

	int num_stars = 100;
	if (argc >= 2) {
		num_stars = atoi(argv[1]);
	}

	auto timer_beg = SyscallGetCurrentTick();

	std::default_random_engine rand_engine;
	std::uniform_int_distribution x_dist(0, kWidth - 2), y_dist(0, kHeight - 2);
	for (int i = 0; i < num_stars; i++) {
		int x = x_dist(rand_engine);
		int y = y_dist(rand_engine);
		SyscallWinFillRect(layerID | LAYER_NO_DRAW, 4 + x, 24 + y, 2, 2, 0xfff100);
	}
	SyscallWinRedraw(layerID); // flush

	auto timer_end = SyscallGetCurrentTick();

	printf("%lu tick ~ %lu tick\n", timer_beg.tick, timer_end.tick);
	printf("%d stars in %lu ms.\n", num_stars, (timer_end.tick - timer_beg.tick) * 1000 / timer_beg.freq);

	exit(0);
}