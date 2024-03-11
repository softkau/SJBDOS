#ifdef __cplusplus
#include <cstddef>
#include <cstdint>

extern "C" {
#else
#include <stddef.h>
#include <stdint.h>
#endif

#include "../kernel/logger.hpp"
#include "../kernel/app_event.hpp"

/**
 * @brief 시스템콜 반환값
 * @details value는 시스템콜의 결과값, error는 시스템콜 에러가 발생했을시 설정됨(errno)
 */
struct SyscallResult {
	uint64_t value;
	int error;
};
struct SysTimerResult {
	uint64_t tick;
	int freq;
};

struct SyscallResult SyscallLogString(enum LogLevel level, const char* message);
struct SyscallResult SyscallPutString(int fd, const char* s, size_t len);
void   SyscallExit(int exit_code);
struct SyscallResult SyscallOpenWindow(int w, int h, int x, int y, const char* title);

#define LAYER_NO_DRAW (0x00000001ull << 32)
struct SyscallResult SyscallWinWriteString(uint64_t layer_id_flags, int x, int y, uint32_t color, const char* s);
struct SyscallResult SyscallWinFillRect(uint64_t layer_id_flags, int x, int y, int w, int h, uint32_t color);
struct SysTimerResult SyscallGetCurrentTick();
struct SyscallResult SyscallWinRedraw(uint64_t layer_id_flags);
struct SyscallResult SyscallWinDrawLine(uint64_t layer_id_flags, int x0, int y0, int x1, int y1, uint32_t color);
#define SyscallWinFillRectangle SyscallWinFillRect /* support for mikanos */
#define LAYER_NO_REDRAW LAYER_NO_DRAW /* support for mikanos */

struct SyscallResult SyscallCloseWindow(uint64_t layer_id_flags);
struct SyscallResult SyscallReadEvent(struct AppEvent* events, size_t len);

#define TIMER_ONESHOT_ABS 0 /* 단발 모드, 절대 시각에 인터럽트 발생 */
#define TIMER_ONESHOT_REL 1 /* 단발 모드, 현재 시각 + 타임아웃에 인터럽트 발생 */

/**
 * @brief 밀리초 단위로 타이머를 생성합니다
 * 
 * @param mode 타이머 동작 설정값 (TIMER_ONESHOT_REL, TIMER_ONESHOT_ABS)
 * @param timer_value 타이머 반환값 (반드시 양수여야함)
 * @param timeout_ms 타이머 타임아웃 값 (밀리초 단위)
 * @return struct SyscallResult 
 */
struct SyscallResult SyscallCreateTimer(unsigned int mode, int timer_value, unsigned long timeout_ms);

struct SyscallResult SyscallOpenFile(const char* path, int flags);
struct SyscallResult SyscallReadFile(int fd, void* buf, size_t count);

struct SyscallResult SyscallDemandPages(size_t num_pages, int flags);
struct SyscallResult SyscallMapFile(int fd, size_t* file_size, int flags);

#ifdef __cplusplus
} // extern "C"
#endif
