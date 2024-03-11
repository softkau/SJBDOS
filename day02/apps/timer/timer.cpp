#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../syscall.h"

extern "C" void main(int argc, char** argv) {
	if (argc <= 1) {
		printf("Usage: timer <msec>\n");
		exit(1);
	}

	const unsigned long duration_ms = atoi(argv[1]);
	const auto timeout = SyscallCreateTimer(TIMER_ONESHOT_REL, 1, duration_ms);
	printf("timer created. timeout = %lu\n", timeout.value);

	AppEvent e;
	while (true) {
		auto [res, err] = SyscallReadEvent(&e, 1);
		if (err) {
			printf("ReadEvent Error: %s\n", strerror(err));
			break;
		}

		if (e.type == AppEvent::kTimerTimeout) {
			printf("%lu msec elapsed.\n", duration_ms);
			break;
		}
		else {
			printf("unknown event: %d\n", e.type);
		}
	}
	exit(0);
}