#pragma once

#include "window.hpp"
#include "task.hpp"
#include "fat.hpp"
#include "error.hpp"
#include "file.hpp"

#include <deque>
#include <array>
#include <map>
#include <optional>
#include <string>

struct TerminalArgs {
	std::string command_line;
	bool exit_after_command;
	bool show_window;
	std::array<std::shared_ptr<FileDescriptor>, 3> files;
};

class Terminal {
public:
	static constexpr int rows = 15, columns = 60;
	static constexpr Vector2D<int> padding = { 4, 4 };
	static constexpr int LineMax = 128;
	Terminal(Task& task, const TerminalArgs* args = nullptr);
	unsigned int LayerID() const { return layerID; }
	Rect<int> InputKey(uint8_t modifier, uint8_t keycode, char ascii);
	void Print(char32_t c);
	void Print(const char* s, std::optional<size_t> len = std::nullopt); // linebuf와 linebuf_index가 변경되지 않음
	void PrintFormat(const char* format, ...);
	void BlinkCursor();
	Vector2D<int> GetCursorPos() const;
	Rect<int> GetCursorArea() const;
	void ReDraw();

private:
	std::shared_ptr<TitleBarWindow> window = nullptr;
	unsigned int layerID;
	TaskID_t taskID;

	Vector2D<int> cursor{0,0};
	bool cursor_visible {false};
	void DrawCursor(bool visible);

	int linebuf_index{0};
	std::array<char, LineMax> linebuf{};
	std::deque<std::array<char, LineMax>> cmd_history;
	int cmd_history_idx{-1};
	Rect<int> ViewHistory(int steps);

	void Scroll();
	void ExecuteLine();

	std::array<std::shared_ptr<FileDescriptor>, 3> files;
	int last_exit_code{0};
public:
	WithError<int> ExecuteFile(const fat::DirectoryEntry* file, char* command, char* args);
	int ExitCode() const { return last_exit_code; }
};

class TerminalFileDescriptor : public FileDescriptor {
public:
	explicit TerminalFileDescriptor(Terminal& term);
	size_t Read(void* buf, size_t len) override;
	size_t Write(const void* buf, size_t len) override;
	size_t Size() const override;
	size_t Load(void* buf, size_t len, size_t offset) override;
private:
	Terminal& term;
};

class PipeDescriptor : public FileDescriptor {
public:
	explicit PipeDescriptor(Task& task);
	size_t Read(void* buf, size_t len) override;
	size_t Write(const void* buf, size_t len) override;
	size_t Size() const override { return 0; }
	size_t Load(void* buf, size_t len, size_t offset) override { return 0; }
	
	void FinishWrite();
private:
	Task& task;
	char data[16];
	size_t len{0};
	bool closed{false};
};

extern std::map<uint64_t, Terminal*>* terminals;

void TaskTerminal(TaskID_t taskID, int64_t data);
