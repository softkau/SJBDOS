#include <errno.h>
#include <sys/types.h>

void _exit(void) {
	while (1) __asm__("hlt");
}

void* prog_brk, *prog_brk_end;

void* sbrk(int incr) {
	if (prog_brk == 0 || prog_brk + incr >= prog_brk_end) {
		errno = ENOMEM;
		return (void*)-1;
	}

	void* prv_brk = prog_brk;

	char* tmp = prog_brk;
	tmp += incr;
	prog_brk = tmp;
	
	return prv_brk;
}

int getpid(void) {
	return 1;
}

int kill(int pid, int sig) {
	errno = EINVAL;
	return -1;
}

int open(const char* path, int flags) {
	errno = EBADF;
	return -1;
}

int close(int fd) {
	errno = EBADF;
	return -1;
}

off_t lseek(int fd, off_t offset, int whence) {
	errno = EBADF;
	return -1;
}

ssize_t read(int fd, void* buf, size_t count) {
	errno = EBADF;
	return -1;
}

ssize_t write(int fd, const void* buf, size_t count) {
	errno = EBADF;
	return -1;
}

int fstat(int fd, struct stat* buf) {
	errno = EBADF;
	return -1;
}

int isatty(int fd) {
	errno = EBADF;
	return -1;
}
