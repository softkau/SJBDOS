#include "../../kernel/logger.hpp"
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cstdio>

struct SyscallResult {
	uint64_t value;
	int error;
};
extern "C" SyscallResult SyscallExit(uint64_t);

long stk[100];
int stk_ptr = -1;

bool is_numeric(const char* s) {
	while (*s) {
		if (*s < '0' || *s > '9')
			return false;
		s++;
	}
	return true;
}

long atol(const char* s) {
	long v = 0;
	while (*s == ' ') s++;
	while (*s && *s != ' ') {
		v = 10 * v + (*s - '0');
		s++;
	}
	return v;
}

bool empty() { return stk_ptr == -1; }
void push(long x) { stk[++stk_ptr] = x; }
long pop() { return stk[stk_ptr--]; }

extern "C" void main(int argc, char** argv) {
	if (argc <= 1) {
		printf("Usage: rpn <expression>\n");
		SyscallExit(-1);
	}

	for (int i = 1; i < argc; i++) {
		if (is_numeric(argv[i])) {
			push(atol(argv[i]));
			continue;
		}
		if (strlen(argv[i]) != 1) {
			SyscallExit(-1);
		}
		
		if (empty()) SyscallExit(-1);
		int y = pop();
		if (empty()) SyscallExit(-1);
		int x = pop();
		switch(argv[i][0]) {
			case '+': push(x + y); break;
			case '-': push(x - y); break;
			case '*': push(x * y); break;
			case '/': push(x / y); break;
			default: SyscallExit(-1);
		}
	}

	if (empty())
		SyscallExit(-1);

	int result = static_cast<int>(pop());

	printf("%d\n", result);

	SyscallExit(result);
}