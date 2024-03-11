#include "../syscall.h"
#include <cstdio>
#include <cstring>

extern "C" void main(int argc, char** argv) {
	auto [id, err] = SyscallOpenWindow(200, 100, 10, 10, "winhello");
	if (err) {
		SyscallExit(err);
	}
	
	SyscallWinWriteString(id,  7, 24, 0xc00000, "hello world!");
	SyscallWinWriteString(id, 24, 40, 0x00c000, "hello world!");
	SyscallWinWriteString(id, 40, 56, 0x0000c0, "hello world!");

	AppEvent events[1];
	while (true) {
		auto [n, err] = SyscallReadEvent(events, 1);
		if (err) {
			printf("ReadEvent failure: %s\n", strerror(err));
			break;
		}

		if (events[0].type == AppEvent::kQuit) {
			break;
		} else {
			printf("unknown event: type = %d\n", events[0].type);
		}
	}
	SyscallCloseWindow(id);
	SyscallExit(0);
}