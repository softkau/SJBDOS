#include "logger.hpp"
#include "console.hpp"

#include <cstdio>

static int log_level = kWarn;

void SetLogLevel(enum LogLevel lvl) {
	log_level = lvl;
}

int Log(enum LogLevel lvl, const char* format, ...) {
	if (lvl > log_level) return 0;

	char buf[1024];
	va_list ap;
	va_start(ap, format);
	int result = vsprintf(buf, format, ap);
	va_end(ap);

	kConsole->puts(buf);
	return result;
}