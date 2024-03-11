#include "keyboard.hpp"

#include <cstdint>
#include "task.hpp"
#include "usb/classdriver/keyboard.hpp"

const char keycode_map[256] = {
	0, 0, 0, 0, 'a', 'b', 'c', 'd',
	'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
	'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
	'u', 'v', 'w', 'x', 'y', 'z', '1', '2',
	'3', '4', '5', '6', '7', '8', '9', '0',
	'\n', '\b', 0x08, '\t', ' ', '-', '=', '[',
	']', '\\', '#', ';', '\'', '`', ',', '.',
	'/', 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, '/', '*', '-', '+',
	'\n', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', '0', '.', '\\', 0, 0, '=',
};

const char keycode_map_shifted[256] = {
	0, 0, 0, 0, 'A', 'B', 'C', 'D',
	'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
	'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
	'U', 'V', 'W', 'X', 'Y', 'Z', '!', '@',
	'#', '$', '%', '^', '&', '*', '(', ')',
	'\n', '\b', 0x08, '\t', ' ', '_', '+', '{',
	'}', '|', '~', ':', '"', '~', '<', '>',
	'?', 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, '/', '*', '-', '+',
	'\n', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', '0', '.', '\\', 0, 0, '=',
};

void InitializeKeyboard() {
	usb::HIDKeyboardDriver::default_observer = [](uint8_t modifier, uint8_t keycode, bool pressed) {
		const bool shift = (modifier & (kLShiftBitMask | kRShiftBitMask)) != 0;
		char ascii = (shift) ? keycode_map_shifted[keycode] : keycode_map[keycode];
		Message msg{Message::KeyPush};
		msg.arg.keyboard.modifier = modifier;
		msg.arg.keyboard.keycode = keycode;
		msg.arg.keyboard.ascii = ascii;
		msg.arg.keyboard.press = pressed;
		task_manager->SendMsg(1, msg);
	};
}