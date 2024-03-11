#include "layer.hpp"
#include "window.hpp"
#include "logger.hpp"
#include "console.hpp"
#include "task.hpp"
#include "interrupt.hpp"

void Layer::DrawTo(FrameBuffer& dst, const Rect<int>& area) const {
	if (window) window->DrawTo(dst, pos, area);
}

LayerManager::LayerManager() {
	layer_stack.reserve(16);
}

void LayerManager::SetScreen(FrameBuffer* screen) {
	this->screen = screen;

	FrameBufferConfig back_config = screen->Config();
	back_config.frame_buffer = nullptr;
	back_buffer.Init(back_config);
}

void LayerManager::Hide(LayerID_t id) {
	auto layer = FindLayer(id);
	auto pos = std::find(layer_stack.begin(), layer_stack.end(), layer);
	if (pos != layer_stack.end()) {
		layer_stack.erase(pos);
	}
}

void LayerManager::SetDepth(LayerID_t id, int new_depth) {
	if (new_depth < 0) {
		Hide(id);
		return;
	}
	if (new_depth > layer_stack.size()) {
		new_depth = layer_stack.size();
	}

	auto layer = FindLayer(id);
	auto old_pos = std::find(layer_stack.begin(), layer_stack.end(), layer);
	auto new_pos = layer_stack.begin() + new_depth;

	if (new_pos == layer_stack.end()) new_pos--;

	if (old_pos == layer_stack.end()) { // layer was hidden
		layer_stack.insert(new_pos+1, layer);
		return;
	}
	
	layer_stack.erase(old_pos);
	layer_stack.insert(new_pos, layer);
}

int LayerManager::GetDepth(LayerID_t id) const {
	auto it = std::find_if(layer_stack.begin(), layer_stack.end(), [id](const Layer* layer) { return layer->ID() == id; });
	if (it == layer_stack.end())
		return -1;
	return it - layer_stack.begin();
}

Layer* LayerManager::FindLayer(LayerID_t id) {
	auto it = std::find_if(
		layers.begin(), layers.end(),
		[id](const std::unique_ptr<Layer>& elem) { return elem->ID() == id;	}
	);
	
	if (it == layers.end())
		return nullptr;
	return it->get();
}

const Layer* LayerManager::FindLayer(LayerID_t id) const {
	auto it = std::find_if(
		layers.begin(), layers.end(),
		[id](const std::unique_ptr<Layer>& elem) { return elem->ID() == id;	}
	);
	
	if (it == layers.end())
		return nullptr;
	return it->get();
}

void LayerManager::SetPosAbsolute(LayerID_t id, Vector2D<int> pos_abs) {
	auto layer = FindLayer(id);
	const auto window_size = layer->GetWindow()->Size();
	const auto old_pos = layer->GetPos();
	layer->SetPosAbsolute(pos_abs);
	Draw( {old_pos, window_size} );
	Draw(id);
}

void LayerManager::SetPosRelative(LayerID_t id, Vector2D<int> pos_diff) {
	auto layer = FindLayer(id);
	const auto window_size = layer->GetWindow()->Size();
	const auto old_pos = layer->GetPos();
	layer->SetPosRelative(pos_diff);
	Draw( {old_pos, window_size} );
	Draw(id);
}

Vector2D<int> LayerManager::GetPos(LayerID_t id) const {
	auto layer = FindLayer(id);
	return layer->GetPos();
}

void LayerManager::Draw(const Rect<int>& area) const {
	for (auto layer : layer_stack)
		layer->DrawTo(back_buffer, area);
	screen->Copy(area.pos, back_buffer, area);
}

void LayerManager::Draw(LayerID_t id, const Rect<int>& area) const {
	bool draw = false;
	Rect<int> window_area = {{0,0}, {0,0}};
	for (auto layer : layer_stack) {
		if (layer->ID() == id) {
			window_area.size = layer->GetWindow()->Size();
			window_area.pos = layer->GetPos();
			if (area.size.x >= 0 || area.size.y >= 0) {
				window_area = window_area & Rect<int>{area.pos + window_area.pos, area.size};
			}
			draw = true;
		}
		if (draw)
			layer->DrawTo(back_buffer, window_area); // 백버퍼의 window_area 영역에 layer를 렌더링한다
	}
	screen->Copy(window_area.pos, back_buffer, window_area); // 모든 layer들의 렌더링이 끝난 백버퍼를 screen으로 카피한다
}

Layer* LayerManager::FindLayerByPosition(Vector2D<int> pos, LayerID_t exclude_id) const {
	auto pred = [pos, exclude_id](Layer* layer) {
		if (layer->ID() == exclude_id) return false;
		const auto& win = layer->GetWindow();
		if (!win) return false;
		const auto win_pos = layer->GetPos();
		const auto win_end_pos = win_pos + win->Size();
		return win_pos.x <= pos.x && pos.x < win_end_pos.x &&
		       win_pos.y <= pos.y && pos.y < win_end_pos.y;
	};
	// stack의 상단부터 탐색한다
	auto it = std::find_if(layer_stack.rbegin(), layer_stack.rend(), pred);
	if (it == layer_stack.rend()) return nullptr;
	return *it;
}

namespace {
	template <class T, class U>
	void EraseIf(T& c, const U& pred) {
		auto it = std::remove_if(c.begin(), c.end(), pred);
		c.erase(it, c.end());
	}
}

void LayerManager::RemoveLayer(unsigned int id) {
	Hide(id);

	auto pred = [id](const std::unique_ptr<Layer>& elem) {
		return elem->ID() == id;
	};

	EraseIf(layers, pred);
}

namespace {
	FrameBuffer* screen;
}

Error SendWindowActiveMsg(LayerID_t layerID, bool active) {
	auto task_it = layer_task_map->find(layerID);
	if (task_it == layer_task_map->end()) {
		return MakeError(Error::kNoSuchTask);
	}

	Message msg{Message::WindowActive};
	msg.arg.window_active.activate = active;
	return task_manager->SendMsg(task_it->second, msg);
}

ActiveLayer::ActiveLayer(LayerManager& layer_manager) : manager(layer_manager) {

}

void ActiveLayer::SetMouseLayer(LayerID_t layerID) {
	mouse_layer = layerID;
}

void ActiveLayer::Activate(LayerID_t layerID) {
	if (active_layer == layerID) return;

	if (active_layer > 0) {
		// deactivate layer
		manager.FindLayer(active_layer)->GetWindow()->Deactivate();
		manager.Draw(active_layer);
		SendWindowActiveMsg(active_layer, false);
	}

	active_layer = layerID;
	if (active_layer > 0) {
		// activate layer
		manager.FindLayer(active_layer)->GetWindow()->Activate();
		int top_depth = LayerManager::TOP_LAYER_DEPTH;
		if (mouse_layer) {
			top_depth = manager.GetDepth(mouse_layer) - 1;
		}
		manager.SetDepth(active_layer, top_depth);
		manager.Draw(active_layer);
		SendWindowActiveMsg(active_layer, true);
	}
}

LayerManager* kLayerManager;
ActiveLayer* active_layer;
std::map<LayerID_t, TaskID_t>* layer_task_map;

void InitializeLayer() {
	const auto screen_size = ScreenSize();

	screen = new FrameBuffer;
	if (auto err = screen->Init(kScreenConfig)) {
		Log(kError, "failed to initialize frame buffer: %s at %s:%d\n", err.Name(), err.File(), err.Line());
		exit(1);
	}
	kLayerManager = new LayerManager;
	kLayerManager->SetScreen(screen);

	auto win_bg = std::make_shared<Window>(screen_size.x, screen_size.y, kScreenConfig.pixel_format);
	DrawDesktop(*win_bg->Writer());
	
	auto win_console = std::make_shared<Window>(Console::cols * 8, Console::rows * 16, kScreenConfig.pixel_format);

	auto layerID_bg = kLayerManager->NewLayer()
		.SetWindow(win_bg)
		.SetPosAbsolute({0, 0})
		.ID();
	auto layerID_console = kLayerManager->NewLayer()
		.SetWindow(win_console)
		.SetPosAbsolute({0, 0})
		.ID();
	
	kLayerManager->SetDepth(layerID_bg, 0);
	kLayerManager->SetDepth(layerID_console, 1);

	kConsole->SetWindow(win_console);
	kConsole->SetOnDraw([=]() { kLayerManager->Draw(layerID_console); });

	active_layer = new ActiveLayer(*kLayerManager);
	layer_task_map = new std::map<LayerID_t, TaskID_t>;
}

void ProcessLayerMessage(const Message& msg) {
	const auto& arg = msg.arg.layer;
	switch (arg.op) {
		case LayerOperation::MovAbs:
			kLayerManager->SetPosAbsolute(arg.layerID, {arg.x, arg.y});
			break;
		case LayerOperation::MovRel:
			kLayerManager->SetPosRelative(arg.layerID, {arg.x, arg.y});
			break;
		case LayerOperation::Draw:
			kLayerManager->Draw(arg.layerID);
			break;
		case LayerOperation::DrawPartial:
			kLayerManager->Draw(arg.layerID, {{arg.x,arg.y}, {arg.w,arg.h}});
			break;
	}
}

void SendCloseMessage(LayerID_t layerID) {
	auto it = layer_task_map->find(layerID);
	if (it == layer_task_map->end() || it->second == MainTaskID) {
		return;
	}
	TaskID_t taskID = it->second;

	Message msg{Message::WindowClose};
	msg.arg.window_close.layer_id = layerID;
	task_manager->SendMsg(taskID, msg);
}

Error CloseLayer(LayerID_t layerID) {
	Layer* layer = kLayerManager->FindLayer(layerID);
	if (!layer) {
		return MakeError(Error::kNoSuchEntry);
	}

	const auto pos = layer->GetPos();
	const auto size = layer->GetWindow()->Size();

	DISABLE_INTERRUPT;
	active_layer->Activate(0);
	kLayerManager->RemoveLayer(layerID);
	kLayerManager->Draw({pos, size});
	layer_task_map->erase(layerID);
	ENABLE_INTERRUPT;

	return MakeError(Error::kSuccess);
}
