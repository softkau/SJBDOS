#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <deque>
#include <cstring>

#include <numeric>
#include <vector>
#include <type_traits>

#include "memmap.h"
#include "segment.hpp"
#include "paging.hpp"
#include "memory_manager.hpp"

#include "frame_buffer_config.h"
#include "graphics.hpp"
#include "mouse.hpp"
#include "keyboard.hpp"
#include "font.hpp"
#include "console.hpp"
#include "layer.hpp"
#include "window.hpp"

#include "pci.hpp"
#include "acpi.hpp"
#include "logger.hpp"
#include "interrupt.hpp"
#include "asmfunc.h"
#include "queue.hpp"
#include "timer.hpp"
#include "terminal.hpp"
#include "fat.hpp"
#include "ide.hpp"

#include "task.hpp"

#include "usb/xhci/xhci.hpp"
#include "syscall.hpp"

//void* operator new(size_t size, void* buffer) noexcept { return buffer; }
void operator delete(void* obj) noexcept {}
const int kTextboxCursorTimer = 1;

extern "C" int printk(const char* format, ...) {
	va_list ap;
	int result;
	char s[1024];

	va_start(ap, format);
	result = vsprintf(s, format, ap);
	va_end(ap);

	kConsole->puts(s);
	return result;
}

inline void Halt() { while (true)__asm__("hlt"); }

#define OPERATING_SYSTEM_STACK_SIZE (1024 * 1024)
alignas(16) uint8_t kernel_main_stack[OPERATING_SYSTEM_STACK_SIZE];
// uint64_t kernel_main_stack_begin = reinterpret_cast<uint64_t>(kernel_main_stack) + OPERATING_SYSTEM_STACK_SIZE; <- cause weird bug :(

std::shared_ptr<TitleBarWindow> win_mainwindow; unsigned layerID_mainwindow;
void InitializeMainWindow() {
	win_mainwindow = std::make_shared<TitleBarWindow>("Hello Window", 160, 52, kScreenConfig.pixel_format);

	layerID_mainwindow = kLayerManager->NewLayer()
		.SetWindow(win_mainwindow)
		.SetPosAbsolute({300, 100})
		.SetDraggable(true)
		.ID();

	kLayerManager->SetDepth(layerID_mainwindow, 2);
}

std::shared_ptr<TitleBarWindow> win_text_window; unsigned int layerID_text_window;
void InitializeTextWindow() {
	const int win_w = 160;
	const int win_h = 52;
	win_text_window = std::make_shared<TitleBarWindow>("Text Box Test", win_w, win_h, kScreenConfig.pixel_format);
	DrawTextbox(*win_text_window->InnerWriter(), {0, 0}, win_text_window->InnerSize());

	layerID_text_window = kLayerManager->NewLayer()
		.SetWindow(win_text_window)
		.SetPosAbsolute({500, 100})
		.SetDraggable(true)
		.ID();

	kLayerManager->SetDepth(layerID_text_window, LayerManager::TOP_LAYER_DEPTH);
}

int text_window_index;
void DrawTextCursor(bool visible) {
	auto pos = []() { return Vector2D<int>{4 + font::FONT_WIDTH * text_window_index, 6}; };
	FillRect(*win_text_window->InnerWriter(),
		pos(),
		font::FONT_SIZE,
		ToColor((visible ? 0 : 0xFFFFFF))
	);
}

void InputTextWindow(char c) {
	if (c == 0) return;

	auto pos = []() { return Vector2D<int>{8 + font::FONT_WIDTH * text_window_index, 24 + 6}; };

	const int max_chars = (win_text_window->Width() - 16) / font::FONT_WIDTH - 1;
	if (c == '\b' && text_window_index > 0) {
		DrawTextCursor(false);
		--text_window_index;
		FillRect(*win_text_window->Writer(), pos(), font::FONT_SIZE, ToColor(0xFFFFFF));
		DrawTextCursor(true);
	}
	else if (c >= ' ' && text_window_index < max_chars) {
		DrawTextCursor(false);
		font::WriteASCII(*win_text_window->Writer(), pos(), c, ToColor(0));
		++text_window_index;
		DrawTextCursor(true);
	}
	kLayerManager->Draw(layerID_text_window);
}

void TaskTextWindow(TaskID_t taskID, int64_t data) {
	DISABLE_INTERRUPT;
	win_text_window = std::make_shared<TitleBarWindow>("Text Box Test", 160, 52, kScreenConfig.pixel_format);
	DrawTextbox(*win_text_window->InnerWriter(), {0, 0}, win_text_window->InnerSize());
	layerID_text_window = kLayerManager->NewLayer()
		.SetWindow(win_text_window)
		.SetPosAbsolute({500, 100})
		.SetDraggable(true)
		.ID();
	active_layer->Activate(layerID_text_window);
	auto& task = task_manager->CurrentTask();
	layer_task_map->insert(std::make_pair(layerID_text_window, taskID));
	ENABLE_INTERRUPT;
  printk("wtf?");
	bool cursor_visible = 1;
  bool is_active = true;
  auto add_blink_timer = [taskID](unsigned long t) {
    timer_manager->AddTimer(Timer{ t + static_cast<int>(kTimerFreq * 0.5), 1, taskID });
  };
  add_blink_timer(timer_manager->CurrentTick());

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
			case Message::TimerTimeout:
        if (is_active) {
          add_blink_timer(msg.arg.timer.timeout);
          cursor_visible = !cursor_visible;
          DrawTextCursor(cursor_visible);
          DISABLE_INTERRUPT;
		      task_manager->SendMsg(MainTaskID, MakeLayerMessage(taskID, layerID_text_window, LayerOperation::Draw, {}));
          ENABLE_INTERRUPT;
        }
				break;
			case Message::KeyPush:
				if (msg.arg.keyboard.press)
					InputTextWindow(msg.arg.keyboard.ascii);
          DISABLE_INTERRUPT;
		      task_manager->SendMsg(MainTaskID, MakeLayerMessage(taskID, layerID_text_window, LayerOperation::Draw, {}));
          ENABLE_INTERRUPT;
				break;
      case Message::WindowActive:
        is_active = msg.arg.window_active.activate;
        if (is_active)
          add_blink_timer(timer_manager->CurrentTick());
        else {
          DrawTextCursor(false);
          DISABLE_INTERRUPT;
		      task_manager->SendMsg(MainTaskID, MakeLayerMessage(taskID, layerID_text_window, LayerOperation::Draw, {}));
          ENABLE_INTERRUPT;
        }
        break;
			case Message::WindowClose:
				CloseLayer(msg.arg.window_close.layer_id);
				DISABLE_INTERRUPT;
				task_manager->Finish(0);
				break;
			default: break;
		}
	}
}

uint8_t buf[512];

extern "C" void KernelMain(const FrameBufferConfig* frame_buffer_config_ptr, const MemoryMap* memory_map_ptr, const acpi::RSDP* acpi_table, void* volume_image) {
	FrameBufferConfig frame_buffer_config{*frame_buffer_config_ptr};
	MemoryMap memory_map{*memory_map_ptr};

	/* Initialize Graphic Buffer */
	InitializeGraphics(frame_buffer_config);

	/* Initialize Console */
	InitializeConsole();
	printk("SJBD World\n");
	SetLogLevel(kWarn);
	
	/* Initialize Memory Manager */
	InitializeSegmentation();
	InitializePaging();
	InitializeMemoryManager(memory_map);

	InitializeTSS();
	InitializeSyscall();

	/* Initialize Interrupt Handler */
	InitializeInterrupt();
	
	/* Initialize Timer */
	acpi::Initialize(*acpi_table);
	InitLAPICTimer(acpi::fadt);
  ide::initIDE(0x1f0, 0x3f4, 0x170, 0x374, 0x000);
  printk("IDE: init finished!");

	fat::Initialize(volume_image);
	font::InitFont();
	InitializePCI();

	/* Initialize (GUI) Layer Manager */
	InitializeLayer(); // kLayerManager Enabled
	InitializeMainWindow();
	//InitializeTextWindow();
	kLayerManager->Draw({{0,0}, ScreenSize() });

	const int kTimerHalfSec = kTimerFreq * 0.5;
	DISABLE_INTERRUPT;
	// timer_manager->AddTimer(Timer(kTimerHalfSec * 2, kTextboxCursorTimer, MainTaskID));
	//timer_manager->AddTimer(Timer(kTimerFreq * 1, 24));
	ENABLE_INTERRUPT;

	InitTask();
	Task& main_task = task_manager->CurrentTask();
  
	const uint64_t task_textwindow_id = task_manager->NewTask()
		.InitContext(TaskTextWindow, 0)
		.Wakeup()
		.ID();

	terminals = new std::map<uint64_t, Terminal*>;

	const uint64_t task_terminal_id = task_manager->NewTask()
		.InitContext(TaskTerminal, 0)
		.Wakeup()
		.ID();

	usb::xhci::Initialize(); // pci interrupts are enabled after this point
	LayerID_t layerID_mouse = InitializeMouse();
	InitializeKeyboard();

	char str[128];

	/* Interrupt Event Loop */
	while (1) {
		DISABLE_INTERRUPT;
		const auto tick = timer_manager->CurrentTick();
		ENABLE_INTERRUPT;

		sprintf(str, "%010lu", tick);
		FillRect(*win_mainwindow->InnerWriter(), {0, 0}, {font::FONT_WIDTH*10, font::FONT_HEIGHT}, {0xc6, 0xc6, 0xc6});
		font::WriteString(*win_mainwindow->InnerWriter(), 0, 0, str, {0, 0, 0});
		kLayerManager->Draw(layerID_mainwindow);

		DISABLE_INTERRUPT;
		auto msg = main_task.ReceiveMsg();
		if (!msg) {
			main_task.Sleep();
			ENABLE_INTERRUPT;
			continue;
		}
		ENABLE_INTERRUPT;

		switch (msg->type) {
			case Message::InterruptXHCI:
				usb::xhci::ProcessEvents();
				break;
			case Message::TimerTimeout:
				switch (msg->arg.timer.value) {
					case kTextboxCursorTimer:
						InterruptGuard([](){ timer_manager->AddTimer(Timer(timer_manager->CurrentTick() + kTimerHalfSec * 2, kTextboxCursorTimer, MainTaskID)); });
						DISABLE_INTERRUPT;
						task_manager->SendMsg(task_textwindow_id, *msg);
						ENABLE_INTERRUPT;
						break;
					default:
						printk("[TIMEOUT] %lu tick(s) elapsed. value(%d) received.\n", msg->arg.timer.timeout, msg->arg.timer.value);
						break;
				} break;
			case Message::KeyPush: {
				if (msg->arg.keyboard.press && msg->arg.keyboard.keycode == 59 /* F2 */) {
					task_manager->NewTask().InitContext(TaskTerminal, 0).Wakeup();
				} 

				DISABLE_INTERRUPT;
				auto task_it = layer_task_map->find(active_layer->GetActiveLayer());
				ENABLE_INTERRUPT;
				if (task_it != layer_task_map->end()) {
					DISABLE_INTERRUPT;
					task_manager->SendMsg(task_it->second, *msg);
					ENABLE_INTERRUPT;
				} else {
					if (msg->arg.keyboard.ascii == 's') {
						DISABLE_INTERRUPT;
						auto pos = kLayerManager->GetPos(layerID_mouse);
						auto layer = kLayerManager->FindLayerByPosition(pos, layerID_mouse);
						auto layer_pos = layer->GetPos();
						ENABLE_INTERRUPT;
						auto px_pos_wincoord = pos - layer_pos;
						auto color = layer->GetWindow()->At(px_pos_wincoord.x, px_pos_wincoord.y);

						printk("color at (%d, %d): #%02x%02x%02x\n", pos.x, pos.y, color.r, color.g, color.b);
					} else {
						printk("unhandled key push: keycode %02x, ascii %02x\n",
							msg->arg.keyboard.keycode,
							msg->arg.keyboard.ascii);
					}
					
				}
			} break;
			case Message::Layer:
				ProcessLayerMessage(*msg);
				DISABLE_INTERRUPT;
				task_manager->SendMsg(msg->src_task, Message{Message::LayerFinish});
				ENABLE_INTERRUPT;
				break;
			default:
				Log(kError, "Unknown message type: %d\n", msg->type);
		}
	}
}

// wtf?
extern "C" void __cxa_pure_virtual() {
	while (1) __asm__("hlt");
}