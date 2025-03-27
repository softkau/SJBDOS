#pragma once

#include <cstdint>
#include <limits>
#include <deque>
#include <queue>
#include "message.hpp"
#include "interrupt.hpp"
#include "task.hpp"

namespace acpi { struct FADT; }

void InitLAPICTimer(const acpi::FADT* fadt=nullptr);
void StartLAPICTimer();
void StopLAPICTimer();
uint32_t LAPICTimerElapsed();

class Timer {
public:
	/** @brief Constructs Timer 
	 * @param timeout timeout duration for this timer
	 * @param value value to send when timeout
	 * @param task_id destination task ID
	 */
	Timer(unsigned long timeout, int value, TaskID_t task_id);

	unsigned long Timeout() const { return timeout; }
	int Value() const { return value; }
	uint64_t TaskID() const { return task_id; }

private:
	unsigned long timeout;
	int value;
	TaskID_t task_id;
};

inline bool operator<(const Timer& lhs, const Timer& rhs) {
	return lhs.Timeout() > rhs.Timeout();
}

class TimerManager {
public:
	TimerManager();
	void AddTimer(const Timer& timer);
	bool Tick();
	unsigned long CurrentTick() const { return tick; }
	auto Top() { return timers.top(); }
private:
	volatile unsigned long tick{0};
	std::priority_queue<Timer> timers{};
};

extern TimerManager* timer_manager;
extern unsigned long lapic_timer_freq; // LAPIC timer frequency, measured in Hz.
constexpr int kDefaultLAPICTimerFreq = 100; // assumed LAPIC timer frequency, if initialization failes. (in Hz.)
constexpr int kTimerFreq = 100;

constexpr int kTaskTimerPeriod = static_cast<int>(kTimerFreq * 0.02);
constexpr int kTaskTimerValue = std::numeric_limits<int>::max();

/*
struct TaskContext;
extern "C" void LAPICTimerOnInterrupt(const TaskContext& ctx_stack);
*/