#if 1
#include <cstdio>
#include <cstdlib>
#include <vector>

char buf[0x1000];

extern "C" void main(int argc, char** argv) {
	if (argc < 3) {
		printf("Usage: dpage <path> <num>\n");
		exit(1);
	}
	FILE* fp = fopen(argv[1], "r");
	if (fp == nullptr) {
		printf("Cannot open file: %s\n", argv[1]);
		exit(1);
	} 
	size_t n = 0, total = 0;
	std::vector<char> data;
	while ((n = fread(buf, sizeof(char), 0x1000, fp)) > 0) {
		for (size_t i = 0; i < n; i++) {
			data.push_back(buf[i]);
		}
		total += n;
		printf("read %lu bytes...\n", total);
	}
	printf("size of %s = %lu bytes\n", argv[1], data.size()); 
	size_t count = 0;
	char match = atoi(argv[2]);
	for (char c : data) {
		if (c == match) count++; 
	} 
	printf("the number of \'%c\' (0x%02x) = %lu\n", match, (int)match, count);
	exit(0);
}
#else

#include <cstdio>
#include <cstdlib>
#include "../syscall.h"

extern "C" void main(int argc, char** argv) {
	const char* filename = "/memmap";
	int ch = '\n';
	if (argc >= 3) {
		filename = argv[1];
		ch = atoi(argv[2]);
	}
	FILE* fp = fopen(filename, "r");
	if (!fp) {
		printf("failed to open %s\n", filename);
		exit(1);
	}

	SyscallResult res = SyscallDemandPages(1, 0);
	if (res.error) {
		exit(1);
	}
	char* buf = reinterpret_cast<char*>(res.value);
	char* buf0 = buf;

	size_t total = 0;
	size_t n;
	while ((n = fread(buf, 1, 4096, fp)) == 4096) {
	total += n;
	if (res = SyscallDemandPages(1, 0); res.error) {
		exit(1);
	}
	buf += 4096;
	}
	total += n;
	printf("size of %s = %lu bytes\n", filename, total);

	size_t num = 0;
	for (int i = 0; i < total; ++i) {
		if (buf0[i] == ch) {
			++num;
		}
	}
	printf("the number of '%c' (0x%02x) = %lu\n", ch, ch, num);
	exit(0);
}
#endif