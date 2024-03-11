#pragma once

enum LogLevel {
	kError = 3,
	kWarn = 4,
	kInfo = 6,
	kDebug = 7
};

#ifdef __cplusplus
void SetLogLevel(enum LogLevel lvl);
int Log(enum LogLevel lvl, const char* format, ...);
#endif