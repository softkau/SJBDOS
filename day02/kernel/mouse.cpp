#include "mouse.hpp"

#include <memory>
#include <limits>
#include "window.hpp"
#include "layer.hpp"

#include "usb/memory.hpp"
#include "usb/device.hpp"
#include "usb/classdriver/mouse.hpp"
#include "usb/xhci/xhci.hpp"
#include "usb/xhci/trb.hpp"

namespace {
const char MOUSE_GFX[MOUSE_HEIGHT][MOUSE_WIDTH+1] = {
	"@              ",
	"@@             ",
	"@.@            ",
	"@..@           ",
	"@...@          ",
	"@....@         ",
	"@.....@        ",
	"@......@       ",
	"@.......@      ",
	"@........@     ",
	"@.........@    ",
	"@..........@   ",
	"@...........@  ",
	"@............@ ",
	"@......@@@@@@@@",
	"@......@       ",
	"@....@@.@      ",
	"@...@ @.@      ",
	"@..@   @.@     ",
	"@.@    @.@     ",
	"@@      @.@    ",
	"@       @.@    ",
	"         @.@   ",
	"         @@@   "
};
}

void DrawMouseCursor(PixelWriter* writer, const Vector2D<int>& pos, const PixelColor& outline, const PixelColor& fill) {
	for (int dy = 0; dy < MOUSE_HEIGHT; dy++)
		for (int dx = 0; dx < MOUSE_WIDTH; dx++)
			if (MOUSE_GFX[dy][dx] == '@')
				writer->Write(pos + Vector2D<int>{ dx, dy }, outline);
			else if (MOUSE_GFX[dy][dx] == '.')
				writer->Write(pos + Vector2D<int>{ dx, dy }, fill);	
};

void DrawMouseCursor(PixelWriter* writer, const PixelColor& outline, const PixelColor& fill_1, const PixelColor& fill_2) {
	for (int dy = 0; dy < MOUSE_HEIGHT; dy++)
		for (int dx = 0; dx < MOUSE_WIDTH; dx++)
			switch (MOUSE_GFX[dy][dx]) {
				case '@': writer->Write({dx, dy}, outline); break;
				case '.': writer->Write({dx, dy}, fill_1);  break;
				default:  writer->Write({dx, dy}, fill_2);
			}
}

void SendMouseMsg(Vector2D<int> newpos, Vector2D<int> posdiff, uint8_t buttons, uint8_t prv_buttons) {
	const auto act = active_layer->GetActiveLayer();
	if (!act) return;

	const auto layer = kLayerManager->FindLayer(act);

	const auto task_it = layer_task_map->find(act);
	if (task_it == layer_task_map->end()) return;

	const auto relpos = newpos - layer->GetPos();
	if (posdiff.x || posdiff.y) {
		Message msg{Message::MouseMove};
		msg.arg.mouse_move.x = relpos.x;
		msg.arg.mouse_move.y = relpos.y;
		msg.arg.mouse_move.dx = posdiff.x;
		msg.arg.mouse_move.dy = posdiff.y;
		msg.arg.mouse_move.buttons = buttons;
		task_manager->SendMsg(task_it->second, msg);
	}

	if (prv_buttons != buttons) {
		const auto diff = prv_buttons ^ buttons;
		for (int i = 0; i < 8; i++) {
			if ((diff >> i) & 1) {
				Message msg{Message::MouseButton};
				msg.arg.mouse_button.x = relpos.x;
				msg.arg.mouse_button.y = relpos.y;
				msg.arg.mouse_button.press = (buttons >> i) & 1;
				msg.arg.mouse_button.button = i;
				task_manager->SendMsg(task_it->second, msg);
			}
		}
	}
}

unsigned InitializeMouse() {
	static Vector2D<int> mouse_position = { 100, 200 };
	static LayerID_t layerID_dragging = 0;
	static uint8_t prev_mouse_btns = 0;

	auto win_mouse = std::make_shared<Window>(MOUSE_WIDTH, MOUSE_HEIGHT, kScreenConfig.pixel_format);
	win_mouse->SetTransparentColor(PixelColor{123, 123, 123});
	DrawMouseCursor(win_mouse->Writer(), {0,0,0}, {255,255,255}, {123,123,123});

	auto layerID_mouse = kLayerManager->NewLayer()
		.SetWindow(win_mouse)
		.SetPosAbsolute(mouse_position)
		.ID();

	kLayerManager->SetDepth(layerID_mouse, LayerManager::TOP_LAYER_DEPTH);
	active_layer->SetMouseLayer(layerID_mouse);

	usb::HIDMouseDriver::default_observer = [layerID_mouse](uint8_t btns, int8_t dx, int8_t dy) {
		const auto old_pos = mouse_position;
		auto new_pos = mouse_position + Vector2D<int8_t>{ dx, dy };
		new_pos = ElementMax({0,0}, new_pos);
		new_pos = ElementMin(ScreenSize(), new_pos);

		mouse_position = new_pos;
		const auto posdiff = mouse_position - old_pos;
		kLayerManager->SetPosAbsolute(layerID_mouse, new_pos);

		LayerID_t layerID_close = 0;

		const bool prev_left_pressed = (prev_mouse_btns & 0x01);
		const bool left_pressed = (btns & 0x01);
		if (!prev_left_pressed && left_pressed) {
			auto layer = kLayerManager->FindLayerByPosition(mouse_position, layerID_mouse);
			if (layer && layer->IsDraggable()) {
				const auto pos_layer = new_pos - layer->GetPos();
				auto region_type = layer->GetWindow()->GetWindowRegion(pos_layer);
				switch (region_type) {
					case WindowRegion::TitleBar: layerID_dragging = layer->ID(); break;
					case WindowRegion::CloseButton: layerID_close = layer->ID(); break;
				}

				active_layer->Activate(layer->ID());
			} else {
				active_layer->Activate(0);
			}
		}
		else if (prev_left_pressed && left_pressed) {
			if (layerID_dragging > 0)
				kLayerManager->SetPosRelative(layerID_dragging, posdiff);
		}
		else if (prev_left_pressed && !left_pressed) {
			layerID_dragging = 0;
		}

		if (layerID_dragging == 0) {
			if (layerID_close == 0) {
				SendMouseMsg(new_pos, posdiff, btns, prev_mouse_btns);
			} else {
				SendCloseMessage(layerID_close);
			}
		}

		prev_mouse_btns = btns;
	};

	return layerID_mouse;
}
