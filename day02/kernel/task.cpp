#include "task.hpp"
#include "timer.hpp"
#include "asmfunc.h"
#include "segment.hpp"
#include <cstring>
#include <cstdint>
#include <algorithm>

TaskManager* task_manager;

void InitTask() {
	task_manager = new TaskManager;

	DISABLE_INTERRUPT;
	timer_manager->AddTimer(Timer(
		timer_manager->CurrentTick() + kTaskTimerPeriod,
		kTaskTimerValue,
		MainTaskID
	));
	ENABLE_INTERRUPT;
}

Task::Task(TaskID_t id) : id(id) {

}

Task& Task::InitContext(TaskFunc* f, int64_t data) {
	const size_t stack_size = kDefaultStackBytes / sizeof(decltype(stack)::value_type);
	stack.resize(stack_size);
	uint64_t stack_begin = reinterpret_cast<uint64_t>(&stack[stack_size]);

	memset(&context, 0, sizeof(TaskContext));
	context.rip = reinterpret_cast<uint64_t>(f);
	context.rdi = id; // 1st arg
	context.rsi = data; // 2st arg

	context.cr3 = GetCR3();
	context.rflags = 0x202; // enable interrupt flag + 1(fixed)
	context.cs = kKernelCS;
	context.ss = kKernelSS;
	context.rsp = (stack_begin & ~0xFlu) - 8; // x64 stack 16bit-alignment restrictions (minus 8 to trick cpp compiler as if the task had been launched from "call" op)

	*reinterpret_cast<uint32_t*>(&context.fxsave_area[24]) = 0x1F80; // mxcsr register

	return *this;
}

void Task::SendMsg(const Message& msg) {
	msgs.push_back(msg);
	this->Wakeup();
}

std::optional<Message> Task::ReceiveMsg() {
	if (msgs.empty()) return std::nullopt;
	auto m = msgs.front();
	msgs.pop_front();
	return m;
}

Message Task::Wait() {
	DISABLE_INTERRUPT;
	auto msg = this->ReceiveMsg();
	if (msg)
		return *msg;

	this->Sleep();
	msg = this->ReceiveMsg();
	ENABLE_INTERRUPT;
	return *msg;
}

Task& Task::Sleep() {
	task_manager->Sleep(this);
	return *this;
}

Task& Task::Wakeup() {
	task_manager->Wakeup(this);
	return *this;
}

TaskManager::TaskManager() {
	running[this->current_lvl].push_back(&NewTask()
		.SetLevel(this->current_lvl)
		.SetRunning(true)
	);

	// underflow를 방지하기 위해 IDLE task(유휴 테스크)를 추가한다
	running[0].push_back(&NewTask()
		.InitContext([](uint64_t, int64_t) { while (true) __asm__("hlt"); }, 0xdeadbeef)
		.SetLevel(0)
		.SetRunning(true)
	);
}

Task& TaskManager::NewTask() {
	++latest_id;
	return *tasks.emplace_back(new Task(latest_id));
}

void TaskManager::SwitchTask(const TaskContext& current_ctx) {
	TaskContext& task_ctx = CurrentTask().Context();
	memcpy(&task_ctx, &current_ctx, sizeof(TaskContext));
	Task* current_task = RotateCurrentRunningQueue(false);
	if (&CurrentTask() != current_task) {
		RestoreContext(&CurrentTask().Context());
	}
}

Task* TaskManager::RotateCurrentRunningQueue(bool current_sleep) {
	auto& q = running[this->current_lvl];
	Task* current_task = q.front();
	q.pop_front();

	if (!current_sleep) {
		q.push_back(current_task);
	}

	// 현재 레벨의 Task 큐가 비어있으면 상위 레벨부터 하위 레벨 순으로 큐를 선택한다
	if (q.empty()) {
		this->lvl_changed = true;
	}

	// 현재 큐가 빈 경우 OR 상위 레벨의 Task가 Wakeup을 받은 경우
	if (this->lvl_changed) {
		this->lvl_changed = false;
		this->current_lvl = kTaskMaxLevel;

		while (current_lvl > 0 && running[this->current_lvl].empty()) {
			this->current_lvl--;
		}
	}

	return current_task;
}

const Task& TaskManager::CurrentTask() const {
	return *running[this->current_lvl].front();
}
Task& TaskManager::CurrentTask() {
	return *running[this->current_lvl].front();
}

Error TaskManager::SendMsg(TaskID_t task_id, const Message& msg) {
	auto it = std::find_if(tasks.begin(), tasks.end(), [task_id](const auto& task) { return task_id == task->ID(); });

	if (it == tasks.end())
		return MAKE_ERROR(Error::kNoSuchTask);

	(*it)->SendMsg(msg);
	return MAKE_ERROR(Error::kSuccess);
}

void TaskManager::Sleep(Task* task) {
	if (!task->Running()) return;

	task->SetRunning(false);

	if (task == running[this->current_lvl].front()) {
		Task* current_task = RotateCurrentRunningQueue(true);
		SwitchContext(&CurrentTask().Context(), &current_task->Context());
		return;
	}

	Erase(running[task->Level()], task);
}

Error TaskManager::Sleep(TaskID_t id) {
	auto it = std::find_if(tasks.begin(), tasks.end(), [id](const auto& task) { return id == task->ID(); });

	if (it == tasks.end())
		return MAKE_ERROR(Error::kNoSuchTask);

	Sleep(it->get());
	return MAKE_ERROR(Error::kSuccess);
}

void TaskManager::Wakeup(Task* task, int lvl) {
	if (task->Running()) {
		ChangeRunningLevel(task, lvl);
		return;
	}

	if (lvl < 0) {
		lvl = task->Level();
	}

	task->SetLevel(lvl);
	task->SetRunning(true);
	running[lvl].push_back(task);
	if (lvl > this->current_lvl)
		this->lvl_changed = true;
}

Error TaskManager::Wakeup(TaskID_t id, int lvl) {
	auto it = std::find_if(tasks.begin(), tasks.end(), [id](const auto& task) { return id == task->ID(); });

	if (it == tasks.end())
		return MAKE_ERROR(Error::kNoSuchTask);

	Wakeup(it->get(), lvl);
	return MAKE_ERROR(Error::kSuccess);
}

void TaskManager::Erase(decltype(running)::value_type& task_grp, Task* task_to_erase) {
	auto it = std::remove(task_grp.begin(), task_grp.end(), task_to_erase);
	task_grp.erase(it, task_grp.end());
}

void TaskManager::ChangeRunningLevel(Task* task, int lvl) {
	if (lvl < 0 || task->Level() == lvl) return;

	// 현재 Context가 아닌 Task인 경우
	if (task != running[current_lvl].front()) {
		Erase(running[task->Level()], task);
		running[lvl].push_back(task);
		task->SetLevel(lvl);
		if (lvl > current_lvl)
			lvl_changed = true;
		return;
	}
	// 현재 Context의 레벨을 바꿀 때
	running[current_lvl].pop_front();
	running[lvl].push_front(task);
	task->SetLevel(lvl);
	if (lvl >= current_lvl) {
		current_lvl = lvl;
	} else {
		current_lvl = lvl;
		lvl_changed = true;
	}
}

void TaskManager::Finish(int exit_code) {
	Task* cur_task = RotateCurrentRunningQueue(true);

	const auto task_id = cur_task->ID();
	auto it = std::find_if(tasks.begin(), tasks.end(), [cur_task](const auto& t) { return t.get() == cur_task; });
	tasks.erase(it);

	finished_tasks[task_id] = exit_code;
	if (auto it = waiter_tasks.find(task_id); it != waiter_tasks.end()) {
		auto waiter = it->second;
		waiter_tasks.erase(it);
		Wakeup(waiter);
	}

	RestoreContext(&CurrentTask().Context());
}

WithError<int> TaskManager::WaitFinish(TaskID_t task_id) {
	int exit_code;
	Task* cur_task = &CurrentTask();
	while (true) {
		if (auto it = finished_tasks.find(task_id); it != finished_tasks.end()) {
			exit_code = it->second;
			finished_tasks.erase(it);
			return { exit_code, MakeError(Error::kSuccess) };
		}

		waiter_tasks[task_id] = cur_task;
		Sleep(cur_task);
	}
}

__attribute__((no_caller_saved_registers))
extern "C" uint64_t GetCurrentTaskOSStackPointer(void) {
	return task_manager->CurrentTask().os_stack_ptr;
}