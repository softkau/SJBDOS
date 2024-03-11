#pragma once

#include <cstdint>
#include <array>
#include <deque>
#include <map>
#include <vector>
#include <memory>
#include <optional>
#include "error.hpp"
#include "message.hpp"
#include "fat.hpp"

struct FileMapping {
	int fd;
	uint64_t vaddr_begin, vaddr_end;
};

struct TaskContext {
	uint64_t cr3, rip, rflags, reserved;				// $00
	uint64_t cs, ss, fs, gs;							// $20
	uint64_t rax, rbx, rcx, rdx, rdi, rsi, rsp, rbp;	// $40
	uint64_t r8, r9, r10, r11, r12, r13, r14, r15;		// $80
	std::array<uint8_t, 512> fxsave_area;				// $c0
} __attribute__((packed));

using TaskID_t = uint64_t;
constexpr TaskID_t MainTaskID = 1;

using TaskFunc = void(TaskID_t task_id, int64_t data);

class TaskManager;

class Task {
public:
	static constexpr unsigned int kDefaultLvl = 1;
	static constexpr size_t kDefaultStackBytes = 8 * 4096;

private:
	/**
	 * @brief Task 객체를 생성합니다. 단독으로 호출되지 않고 TaskManager::NewTask를 통해서 호출됩니다.
	 * @param id Task 객체의 고유 번호입니다(중복되지 않도록 해주세요)
	 */
	Task(TaskID_t id);
public:
	/**
	 * @brief Task 객체의 TaskContext를 초기화하며, Task의 EntryPoint(시작 위치) 및 데이터를 지정합니다.
	 * 
	 * @param f Task의 entry point가 되는 함수
	 * @param data 함수 f의 두번째 parameter에 주어질 값
	 * @return *this가 반환됩니다
	 */
	Task& InitContext(TaskFunc* f, int64_t data);
	TaskID_t ID() const 			{ return id; }
	unsigned int Level() const 		{ return lvl; }
	bool Running() const 			{ return running; }
	TaskContext& Context()			{ return context; }

	/**
	 * @brief Task 객체에 Message를 등록합니다. 또한 Task가 Sleep 상태였던 경우, Running 상태가 됩니다
	 * @param msg 등록할 Message
	 */
	void SendMsg(const Message& msg);
	/**
	 * @brief Task 객체에 등록된 Message 중 1개를 꺼내옵니다.
	 * @return std::optional<Message>를 반환합니다. 등록된 Message가 없는 경우 std::nullopt를 반환합니다
	 */
	std::optional<Message> ReceiveMsg();

	Message Wait();
	Task& Sleep();
	Task& Wakeup();

	uint64_t DPagingBegin() const { return dpaging_begin; }
	uint64_t DPagingEnd() const { return dpaging_end; }
	void SetDPagingBegin(uint64_t v) { dpaging_begin = v; }
	void SetDPagingEnd(uint64_t v) { dpaging_end = v; }
	uint64_t FileMapEnd() const { return file_map_end; }
	void SetFileMapEnd(uint64_t v) { file_map_end = v; }
	std::vector<FileMapping>& FileMaps() { return file_maps; }

	std::vector<std::shared_ptr<::FileDescriptor>> files {};
	uint64_t os_stack_ptr;
private:
	TaskID_t id;
	std::vector<uint64_t> stack;
	alignas(16) TaskContext context;
	std::deque<Message> msgs;
	unsigned int lvl {kDefaultLvl};
	bool running {false};
	uint64_t dpaging_begin {0}, dpaging_end {0};
	uint64_t file_map_end {0};
	std::vector<FileMapping> file_maps {};

	Task& SetLevel(int lvl) { this->lvl = lvl; return *this; }
	Task& SetRunning(bool running) { this->running = running; return *this; }

	friend TaskManager;
};

class TaskManager {
public:
	// Task 레벨 (low priority)0 ~ 3(high priority)
	static constexpr int kTaskMaxLevel = 3;

	/* TaskManager를 초기화합니다 (진행 중이던 Context를 바탕으로 main task가 생성 및 등록되며, 유휴 Task가 1개 등록됩니다) */
	TaskManager();
	/**
	 * @brief 새로운 Task를 생성 및 등록합니다. this->NewTask().InitContext(...)와 같이 생성과 합께 Task 세부설정을 할 수 있습니다
	 * @return 생성된 Task의 레퍼런스(Task&)가 반환됩니다
	 */
	Task& NewTask();
	/**
	 * @brief Running 대기열에 있는 다음 Task로 Context 스위칭합니다
	 * @param current_ctx 현재 콘텍스트
	 */
	void SwitchTask(const TaskContext& current_ctx);
	Task* RotateCurrentRunningQueue(bool current_sleep = false);

	const Task& CurrentTask() const;
	Task& CurrentTask();
	Error SendMsg(TaskID_t task_id, const Message& msg);

	void Sleep(Task* task);
	Error Sleep(TaskID_t id);
	void Wakeup(Task* task, int lvl = -1);
	Error Wakeup(TaskID_t id, int lvl = -1);

	void Finish(int exit_code);
	WithError<int> WaitFinish(TaskID_t task_id);
private:
	std::vector<std::unique_ptr<Task>> tasks {};
	std::map<TaskID_t, int> finished_tasks {};
	std::map<TaskID_t, Task*> waiter_tasks {};
	TaskID_t latest_id {0};
	std::array<std::deque<Task*>, kTaskMaxLevel+1> running {}; // running[0] is the current context
	int current_lvl {kTaskMaxLevel};
	bool lvl_changed {false};

	void Erase(decltype(running)::value_type& task_grp, Task* task_to_erase);

	/**
	 * @brief 현재 실행 중(running == true)인 task의 running 레벨을 변경합니다
	 * 
	 * @param task 레벨을 변경할 task (sleep 상태인 경우 정의되지 않은 동작이 발생할 수 있습니다)
	 * @param lvl 변경할 레벨
	 */
	void ChangeRunningLevel(Task* task, int lvl);
};

extern TaskManager* task_manager;

void InitTask();
