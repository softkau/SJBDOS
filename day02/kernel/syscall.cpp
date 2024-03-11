#include "error.hpp"
#include "asmfunc.h"
#include "logger.hpp"
#include "timer.hpp"
#include "terminal.hpp"
#include "layer.hpp"
#include "font.hpp"
#include "app_event.hpp"
#include "keyboard.hpp"
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <array>
#include <cmath>
#include <fcntl.h>

void InitializeSyscall() {
	WriteMSR(kIA32_EFER, 0x0501u); // enable syscall
	WriteMSR(kIA32_LSTAR, reinterpret_cast<uint64_t>(SyscallEntry)); // register syscalls
	WriteMSR(kIA32_STAR, static_cast<uint64_t>(8) << 32 | static_cast<uint64_t>(16 | 3) << 48); // set (CS, SS) to (8, 8+8) on syscall | (16+16 | 3, 16+8 | 3) on sysret
	WriteMSR(kIA32_FMASK, 0);
}

namespace syscall {
	struct Result {
		uint64_t value;
		int error;
	};
	#define SYSCALL(identifier) syscall::Result identifier(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6)

/*
Since the kernel is running at higher privilege than the user mode code calling it,
it is imperative to check everything. This is not merely paranoia for fear of malicious programs,
but also to protect your kernel from broken applications.

It is therefore necessary to check all arguments for being in range,
and all pointers for being actual user land pointers. The kernel can write anywhere,
but you would not want a specially crafted read() system call
to overwrite the credentials of some process with zeroes (thus giving it root access).

As for making sure that pointers are in range, checking if they point to user or kernel memory
can be difficult to do efficiently unless you are writing a Higher Half Kernel.
For checking all user space accesses for being valid, you can either check with your Virtual Memory Manager
to see if the requested bytes are mapped, or else you can just access them and handle the resulting page faults.
Linux switched to doing the latter from version 2.6 onwards.

*/
	bool VaildatePointer(const void* p) {
		return reinterpret_cast<uintptr_t>(p) >= 0xffff'8000'0000'0000; // cannoical address check (true if user space)
	}
	bool VaildatePointer(uintptr_t p) {
		return p >= 0xffff'8000'0000'0000;
	}

	SYSCALL(LogString) {
		if (arg1 != kError && arg1 != kWarn && arg1 != kInfo && arg1 != kDebug) {
			return { 0, EINVAL };
		}
		if (!VaildatePointer(arg2)) {
			return { 0, EFAULT };
		}
		const char* s = reinterpret_cast<const char*>(arg2);

		if (strlen(s) > 1024) {
			return { 0, E2BIG };
		}
		Log(static_cast<LogLevel>(arg1), "%s", s);
		return { 0, 0 };
	}

	//SYSCALL(HltLong) {
	//	for (int i = 1; i <= 100000000; i++) {
	//		Log(kWarn, "%x", i);
	//		if (i % 5 == 0) Log(kWarn, "\n");
	//	}
	//	return { 0, 0 };
	//}

	SYSCALL(PutString) {
		if (!VaildatePointer(arg2)) {
			return { 0, EFAULT };
		}

		const int fd = arg1;
		const char* s = reinterpret_cast<const char*>(arg2);
		const size_t len = arg3;		

		if (len > 1024) {
			return { 0, E2BIG };
		}

		__asm__("cli");
		auto& task = task_manager->CurrentTask();
		__asm__("sti");

		if (fd < 0 || fd >= task.files.size() || !task.files[fd]) {
			return { 0, EBADF };
		}

		auto n = task.files[fd]->Write(s, len);
		return { n, 0 };
	}

	SYSCALL(Exit) {
		__asm__("cli");
		auto& task = task_manager->CurrentTask();
		__asm__("sti");

		return { task.os_stack_ptr, static_cast<int>(arg1) };
	}

	SYSCALL(OpenWindow) {
		if (!VaildatePointer(arg5)) {
			return { 0, EFAULT };
		}

		const int w = arg1, h = arg2, x = arg3, y = arg4;
		const auto title = reinterpret_cast<const char*>(arg5);
		const auto win = std::make_shared<TitleBarWindow>(title, w, h, kScreenConfig.pixel_format);

		__asm__("cli");
		const auto layerID = kLayerManager->NewLayer().SetWindow(win).SetDraggable(true).SetPosAbsolute({x,y}).ID();
		active_layer->Activate(layerID);
		
		const auto taskID = task_manager->CurrentTask().ID();
		layer_task_map->insert(std::make_pair(layerID, taskID));
		__asm__("sti");

		return {layerID, 0};
	}

	namespace {
		template <class Func, class... Args>
		Result DoWinFunc(Func f, uint64_t layerID_w_flags, Args... args) {
			const auto layer_flags = static_cast<uint32_t>(layerID_w_flags >> 32);
			const auto layerID = static_cast<uint32_t>(layerID_w_flags);
			__asm__("cli");
			auto layer = kLayerManager->FindLayer(layerID);
			__asm__("sti");

			if (layer == nullptr) return { 0, EBADF };

			const auto res = f(*layer->GetWindow(), args...);
			if (res.error) {
				return res;
			}
			
			if ((layer_flags & 1) == 0) {
				__asm__("cli");
				kLayerManager->Draw(layerID);
				__asm__("sti");
			}

			return res;
		}
	}

	SYSCALL(WinWriteString) {
		return DoWinFunc([](Window& win, int x, int y, uint32_t color, const char* s) {
			if (!VaildatePointer(s)) return Result {0, EFAULT};
			font::WriteString(*win.Writer(), {x, y}, s, ToColor(color));
			return Result {0, 0};
		}, arg1, arg2, arg3, arg4, reinterpret_cast<const char*>(arg5));
	}

	SYSCALL(WinFillRect) {
		return DoWinFunc([](Window& win, int x, int y, int w, int h, uint32_t color) {
			FillRect(*win.Writer(), {Vector2D<int>{x,y}, Vector2D<int>{w,h}}, ToColor(color));
			return Result {0, 0};
		}, arg1, arg2, arg3, arg4, arg5, arg6);
	}

	SYSCALL(GetCurrentTick) {
		__asm__("cli");
		auto tick = timer_manager->CurrentTick();
		__asm__("sti");
		return { tick, kTimerFreq };
	}

	SYSCALL(WinRedraw) {
		return DoWinFunc([](Window&) { return Result { 0, 0 }; }, arg1);
	}

	SYSCALL(WinDrawLine) {
		return DoWinFunc([](Window& win, int x0, int y0, int x1, int y1, uint32_t color) {
			auto sign = [](int x) { return (x > 0) ? 1 : (x < 0) ? -1 : 0; };
			const int dx = x1 - x0 + sign(x1 - x0); // add 1px
			const int dy = y1 - y0 + sign(y1 - y0); // add 1px

			if (dx == 0 && dy == 0) { // draw dot
				win.Writer()->Write({x0, y0}, ToColor(color));
				return Result{0, 0};
			}

			const auto floord = static_cast<double(*)(double)>(floor);
			const auto ceild = static_cast<double(*)(double)>(ceil);

			if (abs(dx) >= abs(dy)) { // gentle slope
				if (dx < 0) {
					std::swap(x0, x1);
					std::swap(y0, y1);
				}
				const auto pxcrt = (y1 >= y0) ? floord : ceild; // y increment : y decrement
				const double m = static_cast<double>(dy) / dx;
				for (int x = x0; x <= x1; x++) {
					const int y = pxcrt(m * (x - x0) + y0);
					win.Writer()->Write({x, y}, ToColor(color));
				}
			}
			else { // steep slope
				if (dy < 0) {
					std::swap(x0, x1);
					std::swap(y0, y1);
				}
				const auto pxcrt = (x1 >= x0) ? floord : ceild; // x increment : x decrement
				const double m = static_cast<double>(dx) / dy;
				for (int y = y0; y <= y1; y++) {
					const int x = pxcrt(m * (y - y0) + x0);
					win.Writer()->Write({x, y}, ToColor(color));
				}
			}

			return Result {0, 0};
		}, arg1, arg2, arg3, arg4, arg5, arg6);
	}

	SYSCALL(CloseWindow) {
		const unsigned int layer_id = static_cast<uint32_t>(arg1);
		const auto layer = kLayerManager->FindLayer(layer_id);

		if (layer == nullptr) {
			return { 0, EBADF };
		}

		const auto layer_pos = layer->GetPos();
		const auto win_size = layer->GetWindow()->Size();

		__asm__("cli");
		active_layer->Activate(0);
		kLayerManager->RemoveLayer(layer_id);
		kLayerManager->Draw({ layer_pos, win_size });
		layer_task_map->erase(layer_id);
		__asm__("sti");

		return { 0, 0 };
	}

	SYSCALL(ReadEvent) {
		if (!VaildatePointer(arg1)) {
			return { 0, EFAULT };
		}

		const auto app_events = reinterpret_cast<AppEvent*>(arg1);
		const size_t len = arg2;

		__asm__("cli");
		auto& task = task_manager->CurrentTask();
		__asm__("sti");

		size_t i = 0;
		while (i < len) {
			auto msg = task.Wait();

			switch (msg.type) {
				case Message::KeyPush:
					if (msg.arg.keyboard.keycode == 20 /* Q key */ && msg.arg.keyboard.modifier & (kLControlBitMask | kRControlBitMask)) {
						app_events[i++].type = AppEvent::kQuit;
					}
					else {
						app_events[i].type = AppEvent::kKeyPush;
						app_events[i].arg.keypush.modifier = msg.arg.keyboard.modifier;
						app_events[i].arg.keypush.keycode = msg.arg.keyboard.keycode;
						app_events[i].arg.keypush.ascii = msg.arg.keyboard.ascii;
						app_events[i].arg.keypush.press = msg.arg.keyboard.press;
						++i;
					}
					break;
				case Message::MouseMove:
					app_events[i].type = AppEvent::kMouseMove;
					app_events[i].arg.mouse_move.x = msg.arg.mouse_move.x;
					app_events[i].arg.mouse_move.y = msg.arg.mouse_move.y;
					app_events[i].arg.mouse_move.dx = msg.arg.mouse_move.dx;
					app_events[i].arg.mouse_move.dy = msg.arg.mouse_move.dy;
					app_events[i].arg.mouse_move.buttons = msg.arg.mouse_move.buttons;
					++i;
					break;
				case Message::MouseButton:
					app_events[i].type = AppEvent::kMouseButton;
					app_events[i].arg.mouse_button.x = msg.arg.mouse_button.x;
					app_events[i].arg.mouse_button.y = msg.arg.mouse_button.y;
					app_events[i].arg.mouse_button.press = msg.arg.mouse_button.press;
					app_events[i].arg.mouse_button.button = msg.arg.mouse_button.button;
					++i;
					break;
				case Message::TimerTimeout:
					if (msg.arg.timer.value < 0) {
						app_events[i].type = AppEvent::kTimerTimeout;
						app_events[i].arg.timer.timeout = msg.arg.timer.timeout;
						app_events[i].arg.timer.value = -msg.arg.timer.value;
						++i;
					}
					break;
				case Message::WindowClose:
					app_events[i].type = AppEvent::kQuit;
					++i;
					break;
				default:
					Log(kInfo, "uncaught event type: %u\n", msg.type);
			}
		}

		return { i, 0 };
	}

	SYSCALL(CreateTimer) {
		const unsigned int mode = arg1;
		const int timer_value = arg2;
		if (timer_value <= 0) {
			return { 0, EINVAL };
		}

		__asm__("cli");
		const uint64_t task_id = task_manager->CurrentTask().ID();
		__asm__("sti");

		unsigned long timeout = arg3 * kTimerFreq / 1000;
		if (mode & 1) {
			timeout += timer_manager->CurrentTick();
		}

		__asm__("cli");
		timer_manager->AddTimer(Timer{timeout, -timer_value, task_id});
		__asm__("sti");
		return { timeout * 1000 / kTimerFreq, 0 };
	}

	size_t AllocateFD(Task& task) {
		const size_t num_files = task.files.size();
		for (size_t i = 0; i < num_files; ++i) {
			if (!task.files[i]) {
				return i;
			}
		}
		task.files.emplace_back();
		return num_files;
	}

	std::pair<fat::DirectoryEntry*, int> CreateFile(const char* path) {
		auto [ file, err ] = fat::CreateFile(path);
		switch (err.GetCode()) {
			case Error::kIsDirectory: return { file, EISDIR };
			case Error::kNoSuchEntry: return { file, ENOENT };
			case Error::kNoEnoughMemory: return { file, ENOSPC };
			default: return { file, 0 };
		}
	}

	SYSCALL(OpenFile) {
		if (!VaildatePointer(arg1)) {
			return { 0, EFAULT };
		}
		const auto path = reinterpret_cast<const char*>(arg1);
		const int flags = arg2;
		__asm__("cli");
		auto& task = task_manager->CurrentTask();
		__asm__("sti");

		if (strcmp(path, "@stdin") == 0) {
			return { 0, 0 };
		}

		auto [ file, post_slash ] = fat::FindFile(path);
		if (file == nullptr) {
			if ((flags & O_CREAT) == 0) {
				return { 0, ENOENT };
			}
			auto [ new_file, err ] = CreateFile(path);
			if (err) return { 0, err };
			file = new_file;
		}
		else if (file->dir_Attr != fat::ATTR_DIRECTORY && post_slash) {
			return { 0, ENOENT };
		}

		size_t fd = AllocateFD(task);
		task.files[fd] = std::make_unique<fat::FileDescriptor>(*file);
		return { fd, 0 };
	}

	SYSCALL(ReadFile) {
		if (!VaildatePointer(arg2)) {
			return { 0, EFAULT };
		}
		const int fd = arg1;
		const auto buf = reinterpret_cast<void*>(arg2);
		size_t count = arg3;

		__asm__("cli");
		auto& task = task_manager->CurrentTask();
		__asm__("sti");

		if (fd < 0 || fd >= task.files.size() || !task.files[fd])
			return { 0, EBADF };
		
		size_t n = task.files[fd]->Read(buf, count);
		return { n, 0 };
	}

	SYSCALL(DemandPages) {
		const size_t num_pages = arg1;
		__asm__("cli");
		auto& task = task_manager->CurrentTask();
		__asm__("sti");

		const uint64_t dp_end = task.DPagingEnd();
		task.SetDPagingEnd(dp_end + 4096 * num_pages);
		return { dp_end, 0 };
	}

	SYSCALL(MapFile) {
		const int fd = arg1;
		size_t* file_size = reinterpret_cast<size_t*>(arg2);
		__asm__("cli");
		auto& task = task_manager->CurrentTask();
		__asm__("sti");

		if (fd < 0 || fd >= task.files.size() || !task.files[fd]) {
			return { 0, EBADF };
		}

		*file_size = task.files[fd]->Size();
		const uint64_t vaddr_end = task.FileMapEnd();
		const uint64_t vaddr_begin = (vaddr_end - *file_size) & ~static_cast<uint64_t>(0xfff);
		task.SetFileMapEnd(vaddr_begin);
		task.FileMaps().push_back(FileMapping{ fd, vaddr_begin, vaddr_end });
		return { vaddr_begin, 0 };
	}

	#undef SYSCALL
}

using SyscallType = syscall::Result (uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

extern "C" std::array<SyscallType*, 0x10> syscall_table {
	/* 0x00 */ syscall::LogString,
	/* 0x01 */ syscall::PutString,
	/* 0x02 */ syscall::Exit,
	/* 0x03 */ syscall::OpenWindow,
	/* 0x04 */ syscall::WinWriteString,
	/* 0x05 */ syscall::WinFillRect,
	/* 0x06 */ syscall::GetCurrentTick,
	/* 0x07 */ syscall::WinRedraw,
	/* 0x08 */ syscall::WinDrawLine,
	/* 0x09 */ syscall::CloseWindow,
	/* 0x0a */ syscall::ReadEvent,
	/* 0x0b */ syscall::CreateTimer,
	/* 0x0c */ syscall::OpenFile,
	/* 0x0d */ syscall::ReadFile,
	/* 0x0e */ syscall::DemandPages,
	/* 0x0f */ syscall::MapFile,
};