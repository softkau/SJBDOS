#pragma once

#include <cstdint>

enum class LayerOperation {
	MovAbs, MovRel, Draw, DrawPartial
};

struct Message {
	enum Type {
		InterruptXHCI,
		TimerTimeout,
		KeyPush,
		Layer,
		LayerFinish,
		MouseMove,
		MouseButton,
		WindowActive,
		Pipe,
		WindowClose,
	} type;
	uint64_t src_task;
	union {
		struct {
			unsigned long timeout;
			int value;
		} timer;
		struct {
			uint8_t modifier;
			uint8_t keycode;
			char ascii;
			bool press;
		} keyboard;
		struct {
			LayerOperation op;
			unsigned int layerID;
			int x, y;
			int w, h; // only used for draw parital
		} layer;
		struct {
			int x, y;
			int dx, dy;
			uint8_t buttons;
		} mouse_move;
		struct {
			int x, y;
			int press;
			int button;
		} mouse_button;
		struct {
			bool activate;
		} window_active;
		struct {
			char data[16];
			uint8_t len;
		} pipe;
		struct {
			uint32_t layer_id;
		} window_close;
	} arg;
};