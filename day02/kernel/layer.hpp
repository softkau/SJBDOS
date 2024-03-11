#pragma once

#include <memory>
#include <vector>
#include <limits>
#include <map>
#include "graphics.hpp"
#include "frame_buffer.hpp"
#include "message.hpp"
#include "task.hpp"

class Window;

using LayerID_t = unsigned int;

/**
 * @class Layer
 * @brief Window가 렌더링될 위치 정보, 속성들(Draggable 등)을 설정합니다.
 */
class Layer {
public:
	Layer(LayerID_t id = 0) : id{id}, pos{0, 0}, window{nullptr} {}
	LayerID_t ID() const { return id; }

	Layer& SetWindow(const std::shared_ptr<Window>& w) { window = w; return *this; }
	std::shared_ptr<Window> GetWindow() const { return window; }

	Layer& SetPosAbsolute(Vector2D<int> pos_abs) { pos = pos_abs; return *this; }
	Layer& SetPosRelative(Vector2D<int> pos_diff) { pos += pos_diff; return *this; }
	Layer& SetDraggable(bool value) { draggable = value; return *this; }

	Vector2D<int> GetPos() const { return pos; }
	bool IsDraggable() const { return draggable; }

	// dst의 area(dst 좌표계 기준) 영역에 Layer(의 Window)의 일부분을 렌더링합니다
	void DrawTo(FrameBuffer& dst, const Rect<int>& area) const;
private:
	LayerID_t id;
	Vector2D<int> pos;
	std::shared_ptr<Window> window;
	bool draggable{false};
};

class LayerManager {
public:
	static constexpr int TOP_LAYER_DEPTH = std::numeric_limits<int>::max();
	LayerManager();
	// 모니터에 해당하는 FrameBuffer를 지정합니다.
	void SetScreen(FrameBuffer* screen);

	// 새로운 Layer를 생성합니다(모든 Layer는 이 메소드를 통해 생성되어야 합니다).
	Layer& NewLayer() { return *layers.emplace_back(new Layer{++last_id}); }

	// 사각 영역 area 해당되는 모니터 픽셀을 렌더링합니다.
	void Draw(const Rect<int>& area) const;
	// id에 해당하는 Layer를 렌더링합니다. Layer의 한정된 영역 area(해당 Layer 기준 좌표) 안에서만 렌더링 할 수도 있습니다
	void Draw(LayerID_t id, const Rect<int>& area = {{0,0}, {-1, -1}}) const;

	// 절대좌표 pos_abs에 id에 해당하는 Layer를 이동시킵니다. 
	void SetPosAbsolute(LayerID_t id, Vector2D<int> pos_abs);
	// 현재 좌표에서 pos_diff만큼 id에 해당하는 Layer를 이동시킵니다.
	void SetPosRelative(LayerID_t id, Vector2D<int> pos_diff);
	Vector2D<int> GetPos(LayerID_t id) const;

	void SetDepth(LayerID_t id, int new_depth);
	int GetDepth(LayerID_t id) const;
	void Hide(LayerID_t id);

	// pos 위치에서 depth가 가장 상단인 Layer를 검색합니다. (exclude_id인 Layer는 제외됩니다)
	Layer* FindLayerByPosition(Vector2D<int> pos, LayerID_t exclude_id) const;
	Layer* FindLayer(LayerID_t id);
	const Layer* FindLayer(LayerID_t id) const;

	void RemoveLayer(unsigned int id);

private:
	FrameBuffer* screen{nullptr}; // OS 전체화면 그래픽
	mutable FrameBuffer back_buffer{}; // screen의 Double Buffer(Flickering 해결)
	std::vector<std::unique_ptr<Layer>> layers{};
	std::vector<Layer*> layer_stack{};
	LayerID_t last_id{0};
};

class ActiveLayer {
public:
	ActiveLayer(LayerManager& layer_manager);
	void SetMouseLayer(LayerID_t layerID);
	void Activate(LayerID_t layerID);
	LayerID_t GetActiveLayer() const { return active_layer; }

private:
	LayerManager& manager;
	LayerID_t active_layer {0};
	LayerID_t mouse_layer {0};
};

extern LayerManager* kLayerManager;
extern ActiveLayer* active_layer;
extern std::map<LayerID_t, TaskID_t>* layer_task_map;

void InitializeLayer();
void ProcessLayerMessage(const Message& msg);

constexpr Message MakeLayerMessage(TaskID_t task_id, LayerID_t layer_id, LayerOperation op, Rect<int> area) {
	Message msg{Message::Layer, task_id};
	auto& arg = msg.arg.layer;
	arg.op = op;
	arg.layerID = layer_id;
	arg.x = area.pos.x;
	arg.y = area.pos.y;
	arg.w = area.size.x;
	arg.h = area.size.y;
	return msg;
}

void SendCloseMessage(LayerID_t layerID);
Error CloseLayer(LayerID_t layerID);
