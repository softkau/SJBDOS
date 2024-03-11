#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>

#include "syscall.h"

int getpid(void) {
	return 1;
}

int kill(int pid, int sig) {
	errno = EINVAL;
	return -1;
}

int close(int fd) {
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

off_t lseek(int fd, off_t offset, int whence) {
  errno = EBADF;
  return -1;
}

ssize_t read(int fd, void* buf, size_t count) {
  struct SyscallResult res = SyscallReadFile(fd, buf, count);
  if (res.error == 0) {
    return res.value;
  }
  errno = res.error;
  return -1;
}

void* sbrk(int incr) {
  static uint64_t dpage_end = 0;
  static uint64_t prog_break = 0;
  if (dpage_end == 0 || prog_break + incr > dpage_end) {
    int num_pages = (incr + 0xfff) / 0x1000;
    struct SyscallResult res = SyscallDemandPages(num_pages, 0);
    if (res.error) {
      errno = ENOMEM;
      return (void*)-1;
    }
    prog_break = res.value;
    dpage_end = res.value + 0x1000 * num_pages;
  }
  const uint64_t prev_break = prog_break;
  prog_break += incr;
  return (void*)prev_break;
}

ssize_t write(int fd, const void* buf, size_t count) {
  struct SyscallResult res = SyscallPutString(fd, buf, count);
  if (res.error == 0) {
    return res.value;
  }
  errno = res.error;
  return -1;
}

void _exit(int status) {
  SyscallExit(status);
}

int open(const char* path, int flags) {
  struct SyscallResult res = SyscallOpenFile(path, flags);
  if (res.error == 0) {
    return res.value;
  }
  errno = res.error;
  return -1;
}
