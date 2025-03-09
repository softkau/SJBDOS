#include "timer.hpp"
#include "acpi.hpp"
#include "task.hpp"

#include <limits>

union LVTTimer {
	uint32_t data;
	struct {
		uint32_t vector_id : 8;			// 0:7		Interrupt Vector ID
		uint32_t : 4;					// 8:11		Unused
		uint32_t delivery_status : 1;	// 12		Interrupt Delivery Status; 0 = idle, 1 = send pending
		uint32_t : 3;					// 13:15	Unused
		uint32_t mask : 1;				// 16		Interrupt Mask; 1 = disable interrupt
		uint32_t timer_mode : 2;		// 17:18	Timer Mode; 0 = oneshot, 1 = periodic
		uint32_t : 13;					// 19:31	Padding
	} __attribute__((packed)) bits;
} __attribute__((packed));

/*
oneshot : timer terminates when time out.
periodic : timer re-initializes to [initial_count] when time out.
both : when 0 is written to [initial_count], timer terminates.
*/

namespace {
	constexpr uint32_t COUNT_MAX = 0xFFFFFFFFu;
	volatile uint32_t* const lvt_timer     = reinterpret_cast<uint32_t*>(0xFEE00320ul); // interrupt settings
	volatile uint32_t* const initial_count = reinterpret_cast<uint32_t*>(0xFEE00380ul); // initial value of counter
	volatile uint32_t* const current_count = reinterpret_cast<uint32_t*>(0xFEE00390ul); // current value of counter
	volatile uint32_t* const divide_config = reinterpret_cast<uint32_t*>(0xFEE003E0ul); // counter decrement speed

	/*
	[divide_config]
	only uses 3rd, 1st, and 0th bit; 2nd bit stays 0.
	0000 -> clock speed set to 1/2
	0001 -> clock speed set to 1/3
	1010 -> clock speed set to 1/128
	1011 -> clock speed set to 1/1
	*/
}

unsigned long lapic_timer_freq = kDefaultLAPICTimerFreq;

void InitLAPICTimer(const acpi::FADT* fadt) {
	timer_manager = new TimerManager;

	*divide_config = 0b1011;

	if (fadt) {
		// 일회용 타이머를 설정해서 LAPIC 타이머의 주기를 측정한다
		LVTTimer oneshot = {};
		oneshot.bits.mask = 1; // disable interrupt
		oneshot.bits.timer_mode = 0; // oneshot
		*lvt_timer = oneshot.data;

		StartLAPICTimer();
		acpi::WaitMilliseconds(fadt, 100); // ACPI PM 타이머를 사용해서 약 100ms (0.1s) 대기한다
		const auto elapsed = LAPICTimerElapsed();
		StopLAPICTimer();

		lapic_timer_freq = static_cast<unsigned long>(elapsed) * 10; // Hz
	}

	LVTTimer timer = {};
	timer.bits.vector_id = InterruptVector::LAPICTImer;
	timer.bits.mask = 0; // enable interrupt (to vector_id)
	timer.bits.timer_mode = 1; // periodic
	timer.bits.delivery_status = 0;

	*lvt_timer = timer.data;
	*initial_count = lapic_timer_freq / kTimerFreq;
}

void StartLAPICTimer() {
	*initial_count = COUNT_MAX;
}

void StopLAPICTimer() {
	*initial_count = 0;
}

uint32_t LAPICTimerElapsed() {
	return COUNT_MAX - *current_count;
}

TimerManager* timer_manager;

Timer::Timer(unsigned long timeout, int value, TaskID_t task_id) : timeout{timeout}, value{value}, task_id{task_id} {

}

TimerManager::TimerManager() {
	timers.push(Timer{std::numeric_limits<unsigned long>::max(), -1, MainTaskID}); // 파수꾼(sentinel) 테크닉
}

#include "logger.hpp"

void TimerManager::AddTimer(const Timer &timer) {
	timers.push(timer);
}

bool TimerManager::Tick() {
	++tick;

	bool task_timer_timeout = false;
	while (true) {
		const auto& t = timers.top();
		if (t.Timeout() > tick) { break; } // 타임아웃된 Timer가 없으므로 루프를 종료한다

		if (t.Value() == kTaskTimerValue) { // 콘텍스트 스위칭 주기 타이머인 경우 특수 처리한다
			task_timer_timeout = true;
			timers.pop();
			timers.push(Timer(tick + kTaskTimerPeriod, kTaskTimerValue, MainTaskID));
			continue;
		}

		// Timer t가 타임아웃됐다면 TimerTimeout 메세지를 보내면서 t를 제거한다
		Message m{Message::TimerTimeout};
		m.arg.timer.timeout = t.Timeout();
		m.arg.timer.value = t.Value();
		task_manager->SendMsg(t.TaskID(), m);
		timers.pop();
	}

	return task_timer_timeout;
}

extern "C" void LAPICTimerOnInterrupt(const TaskContext& ctx_stack) {
	const bool task_timer_timeout = timer_manager->Tick();
	NotifyEOI();

	if (task_timer_timeout) {
		task_manager->SwitchTask(ctx_stack);
	}
}
