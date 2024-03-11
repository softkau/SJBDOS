#include "terminal.hpp"
#include "font.hpp"
#include "layer.hpp"
#include "interrupt.hpp"
#include "logger.hpp"
#include "pci.hpp"
#include "fat.hpp"
#include "timer.hpp"
#include "elf.h"
#include "paging.hpp"
#include "asmfunc.h"
#include "keyboard.hpp"
#include "memory_manager.hpp"

#include <cstring>
#include <cstdio>
#include <vector>
#include <string>

struct AppLoadInfo {
	uint64_t vaddr_begin, vaddr_end, entry;
	PageMapEntry* pml4;
	int app_count;
};

std::map<uint64_t, Terminal*>* terminals;
std::map<const fat::DirectoryEntry*, AppLoadInfo>* app_loads;

void ListAllEntries(FileDescriptor& fd, unsigned long cluster, bool is_verbose);

size_t PrintToFD(FileDescriptor& fd, const char* format, ...) {
	va_list ap;
	char s[128];

	va_start(ap, format);
	int n = vsprintf(s, format, ap);
	va_end(ap);

	return fd.Write(s, n);
}

char* rtrim(char* s) {
	char* p = s + strlen(s);
	while (p > s && std::isspace(p[-1])) p--;
	*p = '\0';
	return s;
}

fat::DirectoryEntry* FindCommand(const char* cmd, unsigned long dir_cluster = 0) {
	auto [ entry, post_slash ] = fat::FindFile(cmd, dir_cluster);
	if (entry && (entry->dir_Attr == fat::ATTR_DIRECTORY || post_slash)) {
		return nullptr;
	} else if (entry) {
		return entry;
	}

	if (dir_cluster != 0 || strchr(cmd, '/') != nullptr) {
		return nullptr;
	}

	auto [ apps, post_slash2 ] = fat::FindFile("apps");
	if (!apps || apps->dir_Attr != fat::ATTR_DIRECTORY) {
		return nullptr;
	}

	return FindCommand(cmd, fat::GetFirstCluster(apps));
}

namespace {
	void DrawTerminal(PixelWriter& writer, Vector2D<int> pos, Vector2D<int> size) {
		DrawTextbox(writer, pos, size, 0, 0xc6c6c6, 0x848484);
	}

	WithError<int> MakeArgVector(char* command, char* first_arg, char** argv, int argv_len, char* argbuf, int argbuf_len) {
		int argc = 0;
		int argbuf_idx = 0;

		auto push_to_argv = [&](const char* s) {
			int sz = strlen(s) + 1;
			if (argc >= argv_len || argbuf_idx + sz > argbuf_len) {
				return MakeError(Error::kFull);
			}

			argv[argc++] = &argbuf[argbuf_idx];
			strcpy(&argbuf[argbuf_idx], s);
			argbuf_idx += sz;
			return MakeError(Error::kSuccess);
		};
		
		if (auto err = push_to_argv(command)) {
			return { argc, err };
		}

		if (!first_arg) {
			return { argc, MakeError(Error::kSuccess) };
		}

		char* p = first_arg;
		while (*p) {
			while (isspace(*p)) ++p;
			if (*p == 0) break;
			char* s = p;
			while (*p != 0 && !isspace(*p)) ++p;
			
			if (*p == 0) {
				if (auto err = push_to_argv(s)) return { argc, err };
				break;
			}
			*p = 0;
			if (auto err = push_to_argv(s)) return { argc, err };
			++p;
		}

		return { argc, MakeError(Error::kSuccess) };
	}

	const Elf64_Phdr* GetPHDR(const Elf64_Ehdr* ehdr) {
		return reinterpret_cast<const Elf64_Phdr*>(reinterpret_cast<uintptr_t>(ehdr) + ehdr->e_phoff);
	}

	uintptr_t GetFirstLoadAddress(const Elf64_Ehdr* ehdr) {
		uintptr_t first = std::numeric_limits<uintptr_t>::max();
		auto phdr = GetPHDR(ehdr);
		for (Elf64_Half i = 0; i < ehdr->e_phnum; ++i) {
			if (phdr[i].p_type != PT_LOAD) continue;
			first = std::min(first, phdr[i].p_vaddr);
		}
		return first;
	}
	uintptr_t GetLastLoadAddress(const Elf64_Ehdr* ehdr) {
		uintptr_t last = 0;
		auto phdr = GetPHDR(ehdr);
		for (Elf64_Half i = 0; i < ehdr->e_phnum; ++i) {
			if (phdr[i].p_type != PT_LOAD) continue;
			last = std::max(last, phdr[i].p_vaddr + phdr[i].p_memsz);
		}
		return last;
	}

	WithError<uint64_t> CopyLoadSegments(const Elf64_Ehdr* ehdr) {
		auto phdr = GetPHDR(ehdr);
		uint64_t last_addr = 0;

		for (Elf64_Half i = 0; i < ehdr->e_phnum; ++i) {
			if (phdr[i].p_type != PT_LOAD) continue;

			// do something cool here
			LinearAddress4Level dst_addr;
			dst_addr.value = phdr[i].p_vaddr;

			const auto beg = phdr[i].p_vaddr & ~static_cast<uint64_t>(0xffful);
			const auto end = ((phdr[i].p_vaddr + phdr[i].p_memsz + 0xfff) / 0x1000) * 0x1000;
			const auto num_4kpages = (end - beg) / 0x1000;

			last_addr = std::max(last_addr, phdr[i].p_vaddr + phdr[i].p_memsz);

			if (auto err = SetupPageMaps(dst_addr, num_4kpages, false)) {
				return { last_addr, err };
			}
			// do something cool here

			auto dst = reinterpret_cast<uint8_t*>(phdr[i].p_vaddr);
			auto src = reinterpret_cast<const uint8_t*>(ehdr) + phdr[i].p_offset;
			auto paddings = phdr[i].p_memsz - phdr[i].p_filesz;

			memcpy(dst, src, phdr[i].p_filesz);
			memset(dst + phdr[i].p_filesz, 0, paddings);
		}
		return { last_addr, MakeError(Error::kSuccess) };
	}

	WithError<uint64_t> LoadELF(const Elf64_Ehdr* ehdr) {
		if (ehdr->e_type != ET_EXEC) {
			return { 0, MakeError(Error::kInvalidFormat) };
		}

		uintptr_t first = GetFirstLoadAddress(ehdr);

		if (first < 0xffff'8000'0000'0000) { // cannoical address check
			return { 0, MakeError(Error::kInvalidFormat) };
		}

		auto [last_addr, err] = CopyLoadSegments(ehdr);
		if (err) {
			return { last_addr, err };
		}

		return { last_addr, MakeError(Error::kSuccess) };
	}
}

Terminal::Terminal(Task& task, const TerminalArgs* args) : taskID(task.ID()) {
	bool show_window = true;
	for (int i = 0; i < files.size(); i++) {
		files[i] = std::make_shared<TerminalFileDescriptor>(*this);
	}

	if (args) {
		show_window = args->show_window;
		for (int i = 0; i < files.size(); i++) {
			if (args->files[i])
				files[i] = args->files[i];
		}
	}

	if (show_window) {
		window = std::make_shared<TitleBarWindow>(
			"Terminal",
			columns * font::FONT_WIDTH  + padding.x * 2 + TitleBarWindow::MarginX,
			rows    * font::FONT_HEIGHT + padding.y * 2 + TitleBarWindow::MarginY,
			kScreenConfig.pixel_format);

		DrawTerminal(*window->InnerWriter(), {0, 0}, window->InnerSize());

		layerID = kLayerManager->NewLayer()
			.SetWindow(window)
			.SetDraggable(true)
			.ID();

		Print(">");
	}
	
	cmd_history.resize(8);
}

Rect<int> Terminal::InputKey(uint8_t modifier, uint8_t keycode, char ascii) {
	DrawCursor(false);
	Rect<int> draw_area{GetCursorPos(), font::FONT_SIZE + Vector2D<int>{font::FONT_WIDTH, 0}};

	if (ascii == '\n') {
		linebuf[linebuf_index] = 0;
		if (linebuf_index > 0) {
			cmd_history.pop_back();
			cmd_history.push_front(linebuf);
		}
		linebuf_index = 0;
		cmd_history_idx = -1;
		cursor.x = 0;
		if (cursor.y < rows - 1)
			++cursor.y;
		else {
			Scroll();
			ReDraw();
		}

		ExecuteLine();
		Print(">");

		if (window) {
			draw_area.pos = TitleBarWindow::TopLeftMargin;
			draw_area.size = window->InnerSize();
		}
	}
	else if (ascii == '\b') {
		if (cursor.x > 0 && linebuf_index > 0) {
			cursor.x--;
			if (window) {
				FillRect(*window->Writer(), GetCursorPos(), font::FONT_SIZE, ToColor(0));
			}
			draw_area.pos = GetCursorPos();
			--linebuf_index;
			//if (linebuf_index > 0) {
			//	--linebuf_index;
			//}
		}
	}
	else if (ascii != 0) {
		if (cursor.x < columns - 1 && linebuf_index < LineMax - 1) {
			linebuf[linebuf_index++] = ascii;
			if (window) {
				font::WriteASCII(*window->Writer(), GetCursorPos(), ascii, ToColor(0xffffff));
			}
			cursor.x++;
		}
	}
	else if (keycode == 0x51) { // key down
		draw_area = ViewHistory(-1);
	}
	else if (keycode == 0x52) { // key up
		draw_area = ViewHistory(+1);
	}

	DrawCursor(true);
	return draw_area;
}

void Terminal::Print(char32_t c) {
	if (!window) return;

	auto newline = [this]() {
		cursor.x = 0;
		if (cursor.y < rows - 1) {
			cursor.y++;
		} else {
			Scroll();
		}
	};

	if (c == U'\n') newline();
	else if (font::IsHankaku(c)) {
		if (cursor.x == columns)
			newline();
		font::WriteUnicode(*window->Writer(), GetCursorPos(), c, gfx::color::WHITE);
		cursor.x++;
	}
	else {
		if (cursor.x >= columns - 1)
			newline();
		font::WriteUnicode(*window->Writer(), GetCursorPos(), c, gfx::color::WHITE);
		cursor.x += 2;
	}
}

void Terminal::Print(const char* s, std::optional<size_t> len) {
	const auto cursor_before = GetCursorPos();
	DrawCursor(false);

	if (len) {
		size_t i = 0;
		while (i < *len) {
			auto [u32, n] = font::ConvertUTF8to32(s + i);
			Print(u32);
			i += n;
		}
	} else {
		while (*s) {
			auto [u32, n] = font::ConvertUTF8to32(s);
			Print(u32);
			s += n;
		}
	}

	DrawCursor(true);

	if (window) {
		const auto cursor_after = GetCursorPos();
		Vector2D<int> draw_pos{ TitleBarWindow::TopLeftMargin.x, cursor_before.y };
		Vector2D<int> draw_sz { window->InnerSize().x, cursor_after.y - cursor_before.y + font::FONT_HEIGHT };
		Rect<int> draw_area { draw_pos, draw_sz };

		Message msg = MakeLayerMessage(taskID, layerID, LayerOperation::DrawPartial, draw_area);
		DISABLE_INTERRUPT;
		task_manager->SendMsg(MainTaskID, msg);
		ENABLE_INTERRUPT;
	}
}

void Terminal::PrintFormat(const char* format, ...) {
	va_list ap;
	char s[64];

	va_start(ap, format);
	vsprintf(s, format, ap);
	va_end(ap);

	Print(s);
}

void Terminal::BlinkCursor() {
	cursor_visible = !cursor_visible;
	DrawCursor(cursor_visible);
}

Vector2D<int> Terminal::GetCursorPos() const {
	return TitleBarWindow::TopLeftMargin + padding + Vector2D<int>{
		font::FONT_WIDTH*cursor.x, font::FONT_HEIGHT*cursor.y
	};
}

Rect<int> Terminal::GetCursorArea() const {
	return {
		GetCursorPos(),
		font::FONT_SIZE - Vector2D<int>{1, 1}
	};
}

void Terminal::DrawCursor(bool visible) {
	if (window) {
		const auto color = (visible ? ToColor(0xffffff) : ToColor(0));
		FillRect(*window->Writer(), GetCursorPos(), font::FONT_SIZE - Vector2D<int>{1,1}, color);
	}
}

Rect<int> Terminal::ViewHistory(int steps) {
	cmd_history_idx += steps;
	cmd_history_idx = std::min(cmd_history_idx, static_cast<int>(cmd_history.size()) - 1);
	cmd_history_idx = std::max(cmd_history_idx, -1);

	cursor.x = 1;
	Rect<int> draw_area;
	if (window) {
		const auto pos = GetCursorPos();
		draw_area = { pos, vec_multiply(font::FONT_SIZE, { columns - 1, 1 })};
		FillRect(*window->Writer(), draw_area, gfx::color::BLACK);
	}

	const char* history = "";
	if (cmd_history_idx >= 0) {
		history = &cmd_history[cmd_history_idx][0];
	}

	strcpy(&linebuf[0], history);
	linebuf_index = strlen(history);
	Print(history);
	return draw_area;
}

void Terminal::Scroll() {
	if (!window) return;
	
	window->Shift(
		TitleBarWindow::TopLeftMargin + padding,
		Rect<int>{
			Vector2D<int>{0, font::FONT_HEIGHT} + TitleBarWindow::TopLeftMargin + padding,
			Vector2D<int>{font::FONT_WIDTH*columns, font::FONT_HEIGHT*(rows-1)}
		}
	);
	FillRect(*window->InnerWriter(),
		padding + Vector2D<int>{0,font::FONT_HEIGHT*cursor.y},
		{font::FONT_WIDTH*columns, font::FONT_HEIGHT},
		ToColor(0)
	);
}

void Terminal::ExecuteLine() {
	char* command = &linebuf[0];
	char* first_arg = strchr(&linebuf[0], ' ');
	char* redir_char = strchr(&linebuf[0], '>');
	char* pipe_char = strchr(&linebuf[0], '|');

	if (first_arg) {
		*first_arg = 0;
		do {
			++first_arg;
		} while (isspace(*first_arg));
	}

	auto original_stdout = files[1];
	int exit_code = 0;
	if (redir_char) {
		*redir_char = 0;
		char* redir_dest = redir_char + 1;
		while (isspace(*redir_dest)) { // skip spaces
			++redir_dest;
		}
		rtrim(redir_dest);
		auto [ file, post_slash ] = fat::FindFile(redir_dest);
		if (file == nullptr) {
			auto [ new_file, err ] = fat::CreateFile(redir_dest);
			if (err) {
				PrintToFD(*files[2], "failed to create a redirect file: %s\n", err.Name());
				return;
			}
			file = new_file;
		}
		else if (file->dir_Attr == fat::ATTR_DIRECTORY || post_slash) {
			PrintToFD(*files[2], "failed to create a directory\n");
			return;
		}
		files[1] = std::make_shared<fat::FileDescriptor>(*file);
	}
	std::shared_ptr<PipeDescriptor> pipe_fd;
	TaskID_t subtask_id = 0;
	if (pipe_char) {
		*pipe_char = 0;
		char* subcommand = &pipe_char[1];
		while (isspace(*subcommand)) {
			subcommand++;
		}

		auto& subtask = task_manager->NewTask();
		pipe_fd = std::make_shared<PipeDescriptor>(subtask);
		// TODO : free somewhere!!!
		auto term_args = new TerminalArgs{
			subcommand, true, false,
			{ pipe_fd, files[1], files[2] }	
		};
		files[1] = pipe_fd;

		subtask_id = subtask.InitContext(TaskTerminal, reinterpret_cast<int64_t>(term_args)).Wakeup().ID();
		(*layer_task_map)[layerID] = subtask_id;
	}

	auto& stdin_ = *files[0];
	auto& stdout_ = *files[1];
	auto& stderr_ = *files[2];
	if (strcmp(command, "echo") == 0) {
		if (first_arg && first_arg[0] == '$') {
			if (strcmp(first_arg + 1, "?") == 0) {
				PrintToFD(stdout_, "%d", last_exit_code);
			}
		} else if (first_arg) {
			PrintToFD(stdout_, first_arg);
		}
		PrintToFD(stdout_, "\n");
	} else if (strcmp(command, "clear") == 0) {
		FillRect(*window->InnerWriter(),
			padding,
			vec_multiply(font::FONT_SIZE, {columns, rows}),
			gfx::color::BLACK
		);
		cursor.y = 0;
	} else if (strcmp(command, "lspci") == 0) {
		for (int i = 0; i < pci::g::num_devices; i++) {
			const auto& device = pci::g::devices[i];
			PrintToFD(stdout_, "%02x:%02x.%d vend=%04x head=%02x class=%02x.%02x.%02x\n",
				device.bus,
				device.device,
				device.func,
				device.vendor_id,
				device.hdr_type,
				device.class_code.base,
				device.class_code.sub,
				device.class_code.interface
			);
		}
	} else if (strcmp(command, "ls") == 0) {
		rtrim(first_arg);
		if (first_arg[0] == '\0') {
			ListAllEntries(stdout_, fat::boot_volume_image->bpb_RootClus, false);
		} else {
			auto [ dir, post_slash ] = fat::FindFile(first_arg);
			if (dir == nullptr) {
				PrintToFD(stderr_, "No such file or directory: %s\n", first_arg);
				exit_code = 1;
			} else if (dir->dir_Attr == fat::ATTR_DIRECTORY) {
				ListAllEntries(stdout_, fat::GetFirstCluster(dir), false);
			} else {
				if (post_slash) {
					PrintToFD(stderr_, "Not a directory\n");
					exit_code = 1;
				} else {
					PrintToFD(stdout_, "File(s) found.");
				}
			}
		}
	} else if (strcmp(command, "cat") == 0) {
		rtrim(first_arg);

		std::shared_ptr<FileDescriptor> fd;
		if (!first_arg || first_arg[0] == '\0') {
			fd = files[0];
		} else {
			auto [ file_entry, post_slash ] = fat::FindFile(first_arg);
			if (!file_entry) {
				PrintToFD(stderr_, "no such file: %s\n", first_arg);
				exit_code = 1;
			} else if (file_entry->dir_Attr != fat::ATTR_DIRECTORY && post_slash) {
				PrintToFD(stderr_, "%s is not a directory\n", first_arg);
				exit_code = 1;
			} else {
				fd = std::make_shared<fat::FileDescriptor>(*file_entry);
			}
		}
		if (fd) {
			char u8buf[1024];
			
			DrawCursor(false);
			while (true) {
				if (ReadDelim(*fd, '\n', u8buf, sizeof(u8buf)) == 0) break;
				PrintToFD(stdout_, "%s", u8buf);
			}
			DrawCursor(true);
		}
	} else if (strcmp(command, "noterm") == 0) {
		// TODO : free somewhere!!!
		auto term_args = new TerminalArgs{
			first_arg, true, false,
			{ files[0], files[1], files[2] }
		};
		task_manager->NewTask()
			.InitContext(TaskTerminal, reinterpret_cast<int64_t>(term_args))
			.Wakeup();
	} else if (strcmp(command, "memstat") == 0) {
		const auto p_stat = memory_manager->Stat();

		PrintToFD(stdout_, "Phys used: %lu frames (%llu MiB)\n", p_stat.allocated_frames, p_stat.allocated_frames * BytesPerFrame / 1024 / 1024);
		PrintToFD(stdout_, "Phys total: %lu frames (%llu MiB)\n", p_stat.total_frames, p_stat.total_frames * BytesPerFrame / 1024 / 1024);
		
	} else if (command[0] != 0) {
		auto file_entry = FindCommand(command);
		if (!file_entry) {
			PrintToFD(stderr_, "no such command: %s\n", command);
			exit_code = 1;
		} else {
			auto [excode, err] = ExecuteFile(file_entry, command, first_arg);
			if (err) {
				PrintToFD(stderr_, "Error occurred while executing %s:\n%s from file %s(at line %d)", command,
					err.Name(), err.File(), err.Line());
			}
			exit_code = excode;
		}
	}

	if (pipe_fd) {
		Log(kWarn, "Task %lu finished writing.\n", taskID);
		Log(kWarn, "Waiting for SubTask %lu to finish...\n", subtask_id);
		pipe_fd->FinishWrite();
		DISABLE_INTERRUPT;
		auto [ ec, err ] = task_manager->WaitFinish(subtask_id);
		(*layer_task_map)[layerID] = taskID;
		ENABLE_INTERRUPT;
		Log(kWarn, "SubTask %lu finished.\n", subtask_id);

		exit_code = ec;
	}

	last_exit_code = exit_code;
	files[1] = original_stdout;
	Log(kWarn, "Task %lu finished.\n", taskID);
}

WithError<AppLoadInfo> LoadApp(const fat::DirectoryEntry* file, Task& task) {
	PageMapEntry* temp_pml4;
	if (auto [pml4, err] = SetupPML4(task); err) {
		return { {}, err };
	} else {
		temp_pml4 = pml4;
	}

	// when loading already loaded app
	if (auto it = app_loads->find(file); it != app_loads->end()) {
		it->second.app_count++;
		AppLoadInfo app_load = it->second;
		auto err = CopyPageMaps(temp_pml4, app_load.pml4, 4, 256);
		app_load.pml4 = temp_pml4;
		return { app_load, err };
	}

	// first time loading the app (create 'clean' template pml4; will be used if same app is called afterwards)
	std::vector<uint8_t> buf(file->dir_FileSize);
	fat::LoadFile(&buf[0], file->dir_FileSize, file);

	auto elf_header = reinterpret_cast<Elf64_Ehdr*>(&buf[0]);
	if (memcmp(elf_header->e_ident, "\x7f" "ELF", 4) != 0) {
		return { {}, MakeError(Error::kInvalidFormat) };
	}

	auto [ last_addr, load_err ] = LoadELF(elf_header);
	if (load_err) {
		return { {}, load_err };
	}

	// reigster loaded app
	AppLoadInfo app_load{ GetFirstLoadAddress(elf_header), last_addr, elf_header->e_entry, temp_pml4, 1 };
	app_loads->insert(std::make_pair(file, app_load));

	// use new pml4 (switch from 'template pml4' to 'app pml4')
	if (auto [pml4, err] = SetupPML4(task); err) {
		return { app_load, err };
	} else {
		app_load.pml4 = pml4;
	}
	auto err = CopyPageMaps(app_load.pml4, temp_pml4, 4, 256);
	return { app_load, err };
}

WithError<int> Terminal::ExecuteFile(const fat::DirectoryEntry* file, char* command, char* args) {
	DISABLE_INTERRUPT;
	auto& task = task_manager->CurrentTask();
	ENABLE_INTERRUPT;

	auto [app_load, err] = LoadApp(file, task);
	if (err) {
		return { 0, err };
	}

	const int stack_size = 16 * 4096;
	const LinearAddress4Level stack_frame_addr{0xffff'ffff'ffff'f000 - stack_size};
	const LinearAddress4Level args_frame_addr {0xffff'ffff'ffff'f000};

	if (auto err = SetupPageMaps(stack_frame_addr, stack_size / 4096, true)) {
		return { 0, err };
	}

	auto argv = reinterpret_cast<char**>(args_frame_addr.value);
	int argv_len = 32;
	auto argbuf = reinterpret_cast<char*>(args_frame_addr.value + sizeof(char**) * argv_len);
	int argbuf_len = 4096 - sizeof(char**) * argv_len;
	
	if (auto err = SetupPageMaps(args_frame_addr, 1, true)) {
		return { 0, err };
	}

	auto argc = MakeArgVector(command, args, argv, argv_len, argbuf, argbuf_len);
	if (argc.error) {
		return { 0, argc.error };
	}

	for (int i = 0; i < 3; i++)
		task.files.push_back(files[i]);
	
	const uintptr_t elf_dpaging_begin = (app_load.vaddr_end + 0xfff) & ~static_cast<uintptr_t>(0xfff);
	task.SetDPagingBegin(elf_dpaging_begin);
	task.SetDPagingEnd(elf_dpaging_begin);
	task.SetFileMapEnd(stack_frame_addr.value);

	int ret = CallApp(argc.value, &argv[0], 3 << 3 | 3, app_load.entry, stack_frame_addr.value + stack_size - 8, &task.os_stack_ptr);

	task.files.clear();
	task.FileMaps().clear();
	// PrintFormat("app exited with status: %d\n", ret);

	if (auto err = CleanPageMaps(LinearAddress4Level{0xffff'8000'0000'0000})) {
		return { ret, err };
	}

	if (auto err = FreePML4(task)) {
		return { ret, err };
	}
	#if 0
	auto it = app_loads->find(file);
	it->second.app_count--;
	if (it->second.app_count == 0) {
		CleanTempPML4(reinterpret_cast<uint64_t>(it->second.pml4), 256);
		app_loads->erase(it);
	}
	#endif

	return { ret, MakeError(Error::kSuccess) };
}

void Terminal::ReDraw() {
	Rect<int> draw_area { TitleBarWindow::TopLeftMargin, window->InnerSize() };
	Message msg = MakeLayerMessage(taskID, layerID, LayerOperation::DrawPartial, draw_area);
	DISABLE_INTERRUPT;
	task_manager->SendMsg(MainTaskID, msg);
	ENABLE_INTERRUPT;
}

TerminalFileDescriptor::TerminalFileDescriptor(Terminal& term) : term(term) {

}

size_t TerminalFileDescriptor::Read(void* buf, size_t len) {
	char* bufc = reinterpret_cast<char*>(buf);
	DISABLE_INTERRUPT;
	auto& task = task_manager->CurrentTask();
	ENABLE_INTERRUPT;

	while (true) {
		auto msg = task.Wait();
		if (msg.type != Message::KeyPush || !msg.arg.keyboard.press) {
			continue;
		}

		if (msg.arg.keyboard.modifier & (kLControlBitMask | kRControlBitMask)) {
			char s[3] = "^ ";
			s[1] = toupper(msg.arg.keyboard.ascii);
			term.Print(s);
			if (msg.arg.keyboard.keycode == 7 /* D */) {
				return 0; // EOT
			}
			continue;
		}
		
		bufc[0] = msg.arg.keyboard.ascii;
		term.Print(bufc, 1); // echo back
		term.ReDraw();
		return 1;
	}
}

size_t TerminalFileDescriptor::Write(const void* buf, size_t len) {
	const char* bufc = reinterpret_cast<const char*>(buf);
	term.Print(bufc, len);
	term.ReDraw();
	return len;
}

size_t TerminalFileDescriptor::Size() const {
	return 0;
}

size_t TerminalFileDescriptor::Load(void* buf, size_t len, size_t offset) {
	return this->Read(buf, len);
}

PipeDescriptor::PipeDescriptor(Task& task) : task{task} {
	
}

size_t PipeDescriptor::Read(void* buf, size_t len) {
	if (this->len > 0) {
		const size_t copy_bytes = std::min(this->len, len);
		memcpy(buf, data, copy_bytes);
		memmove(data, data + copy_bytes, this->len - copy_bytes);
		this->len -= copy_bytes;
		return copy_bytes;
	}

	if (closed) {
		return 0;
	}

	while (true) {
		auto msg = task.Wait();
		if (msg.type != Message::Pipe) {
			continue;
		}

		if (msg.arg.pipe.len == 0) {
			closed = true;
			return 0;
		}

		size_t copy_bytes = std::min<size_t>(len, msg.arg.pipe.len);
		memcpy(buf, msg.arg.pipe.data, copy_bytes);
		this->len = msg.arg.pipe.len - copy_bytes;
		memcpy(data, msg.arg.pipe.data + copy_bytes, this->len);
		return copy_bytes;
	}
}
size_t PipeDescriptor::Write(const void* buf, size_t len) {
	const char* bufc = reinterpret_cast<const char*>(buf);
	Message msg{Message::Pipe};
	size_t sent_bytes = 0;
	while (sent_bytes < len) {
		msg.arg.pipe.len = std::min(len - sent_bytes, sizeof(msg.arg.pipe.data));
		memcpy(msg.arg.pipe.data, bufc + sent_bytes, msg.arg.pipe.len);
		sent_bytes += msg.arg.pipe.len;
		DISABLE_INTERRUPT;
		task_manager->SendMsg(task.ID(), msg);
		ENABLE_INTERRUPT;
	}
	return sent_bytes;
}

void PipeDescriptor::FinishWrite() {
	Message msg{ Message::Pipe };
	msg.arg.pipe.len = 0;
	DISABLE_INTERRUPT;
	task.SendMsg(msg);
	ENABLE_INTERRUPT;
}

void TaskTerminal(TaskID_t taskID, int64_t data) {
	const auto* arg_ptr = reinterpret_cast<TerminalArgs*>(data);

	bool show_window = data ? arg_ptr->show_window : true;

	DISABLE_INTERRUPT;
	Task& task = task_manager->CurrentTask();
	Terminal* terminal = new Terminal(task, arg_ptr);

	if (show_window) {
		kLayerManager->SetPosAbsolute(terminal->LayerID(), {100, 200});
		active_layer->Activate(terminal->LayerID());
		layer_task_map->insert(std::make_pair(terminal->LayerID(), taskID));
		(*terminals)[taskID] = terminal;
	}
	ENABLE_INTERRUPT;
	
	if (data && !arg_ptr->command_line.empty()) {
		// send command to new terminal
		for (char c : arg_ptr->command_line) {
			terminal->InputKey(0, 0, c);
		}
		terminal->InputKey(0, 0, '\n');
	}

	if (data && arg_ptr->exit_after_command) {
		delete arg_ptr;
		DISABLE_INTERRUPT;
		task_manager->Finish(terminal->ExitCode());
		ENABLE_INTERRUPT;
	}

	auto add_blink_timer = [taskID](unsigned long t) {
		timer_manager->AddTimer(Timer{ t + static_cast<int>(kTimerFreq * 0.5), 1, taskID });	
	};
	add_blink_timer(timer_manager->CurrentTick());

	bool window_isactive = true;

	while (true) {
		DISABLE_INTERRUPT;
		auto msg_opt = task.ReceiveMsg();
		if (!msg_opt) {
			task.Sleep();
			ENABLE_INTERRUPT;
			continue;
		}
		ENABLE_INTERRUPT;

		Message msg = *msg_opt;

		switch (msg.type) {
			case Message::TimerTimeout: {
				switch (msg.arg.timer.value) {
					case 24: { // for debugging purposes
						auto file = FindCommand("rpn");
						char cmd[] = "rpn";
						char args[] = "2 3 +";

						terminal->ExecuteFile(file, cmd, args);
					} break;
					default: {
						add_blink_timer(msg.arg.timer.timeout);
						if (show_window && window_isactive) {
							terminal->BlinkCursor();
							auto area = terminal->GetCursorArea();
							msg = MakeLayerMessage(taskID, terminal->LayerID(), LayerOperation::DrawPartial, area);

							DISABLE_INTERRUPT;
							task_manager->SendMsg(MainTaskID, msg);
							ENABLE_INTERRUPT;
						}
					}
				}
			} break;
			case Message::WindowActive:
				window_isactive = msg.arg.window_active.activate;
				break;
			case Message::KeyPush: {
				auto& arg = msg.arg.keyboard;
				if (msg.arg.keyboard.press) {
					auto area = terminal->InputKey(arg.modifier, arg.keycode, arg.ascii);
					if (show_window) {
						msg = MakeLayerMessage(taskID, terminal->LayerID(), LayerOperation::DrawPartial, area);
						DISABLE_INTERRUPT;
						task_manager->SendMsg(MainTaskID, msg);
						ENABLE_INTERRUPT;
					}
				}
			} break;
			case Message::WindowClose:
				CloseLayer(msg.arg.window_close.layer_id);
				DISABLE_INTERRUPT;
				task_manager->Finish(terminal->ExitCode());
			default: break;
		}
	}
}

void ListAllEntries(FileDescriptor& fd, unsigned long cluster, bool is_verbose) {
	char base[9], ext[4]; // for short names
		
	char lfn[256]; // long file name entries (truncating odd-indexed bytes)
	for (fat::DirectoryEntryPointer entry_ptr = { cluster, 0 }; !entry_ptr.IsEndOfClusterchain(); ++entry_ptr) {
		auto entry = entry_ptr.get();
		fat::ReadName(entry, base, ext);
		if (base[0] == 0x00) {
			if (is_verbose) {
				PrintToFD(fd, "terminal entry\n");
			}
			break;
		}
		else if (static_cast<uint8_t>(base[0]) == 0xe5) {
			if (is_verbose) {
				PrintToFD(fd, "empty entry\n");
			}
			continue;
		}
		else if (entry->dir_Attr == fat::ATTR_LONG_NAME) {
			entry_ptr = fat::GetLongFileName(entry_ptr, lfn);
			if (strlen(lfn) > 62)
				PrintToFD(fd, "%.59s...\n", lfn);
			else
				PrintToFD(fd, "%s\n", lfn);
		}
		else {
			if (ext[0] == 0)
				PrintToFD(fd, "%s\n", base);
			else
				PrintToFD(fd, "%s.%s\n", base, ext);
		}
	}
}
