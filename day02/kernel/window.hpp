#pragma once

#include "graphics.hpp"
#include "frame_buffer.hpp"
#include <optional>
#include <vector>
#include <string>

enum class WindowRegion {
	TitleBar, CloseButton, Border, Other
};

/**
 * @class Window
 * @brief Layer의 그래픽적 요소를 담당합니다
 */
class Window {
public:
	class WindowWriter : public PixelWriter {
	public:
		WindowWriter(Window& window) : window{window} {}
		virtual void Write(Vector2D<int> pos, const PixelColor& c) override { window.Write(pos, c); }
		virtual int Width() const override { return window.Width(); }
		virtual int Height() const override { return window.Height(); }
	private:
		Window& window;
	};

	Window(int width, int height, PixelFormat shadow_format);
	virtual ~Window() = default;
	Window(const Window& rhs) = delete;
	Window& operator=(const Window& rhs) = delete;

	// 객체를 FrameBuffer dst의 pos(x, y)위치에 area(dst 기준 좌표)만큼만 렌더링합니다.
	void DrawTo(FrameBuffer& dst, Vector2D<int> pos, const Rectangle<int>& area);
	// 객체의 투명색을 지정합니다. (c = std::nullopt인 경우, 투명색을 사용하지 않는 Window로 간주됩니다; 렌더링 속도에 도움을 줄 수 있습니다)
	void SetTransparentColor(std::optional<PixelColor> c) { transparent_color = c; };
	
	WindowWriter* Writer() { return &writer; }
	
	PixelColor& At(int x, int y) { return data[y * width + x]; }
	const PixelColor& At(int x, int y) const { return data[y * width + x]; }

	// pos 픽셀에 색깔 c를 찍습니다.
	void Write(Vector2D<int> pos, const PixelColor& c);

	// 사각형 영역 src에 그려진 픽셀을 dst_pos(x, y) 위치로 이동합니다.
	void Shift(Vector2D<int> dst_pos, const Rectangle<int>& src);

	int Width() const { return width; }
	int Height() const { return height; }
	Vector2D<int> Size() const { return { width, height }; }

	// Window를 비활성화된 모습의 그래픽으로 변경합니다
	virtual void Deactivate();
	// Window를 활성화된 모습의 그래픽으로 변경합니다
	virtual void Activate();
	virtual WindowRegion GetWindowRegion(Vector2D<int> pos) const;
private:
	int width, height;
	std::vector<PixelColor> data{};
	WindowWriter writer{*this};
	std::optional<PixelColor> transparent_color{std::nullopt};

	FrameBuffer shadow_buffer{};
};

class TitleBarWindow : public Window {
public:
	static constexpr Vector2D<int> TopLeftMargin{4, 24};
	static constexpr Vector2D<int> BottomRightMargin{4, 4};
	static constexpr int MarginX{TopLeftMargin.x + BottomRightMargin.x};
	static constexpr int MarginY{TopLeftMargin.y + BottomRightMargin.y};

	class InnerAreaWriter : public PixelWriter {
	public:
		InnerAreaWriter(TitleBarWindow& window) : window{window} {}
		virtual void Write(Vector2D<int> pos, const PixelColor& c) override { window.Write(pos + TopLeftMargin, c); }
		virtual int Width() const override { return window.Width() - TopLeftMargin.x - BottomRightMargin.x; }
		virtual int Height() const override { return window.Height() - TopLeftMargin.y - BottomRightMargin.y; }
	private:
		TitleBarWindow& window;
	};
	TitleBarWindow(const std::string& title, int width, int height, PixelFormat shadow_format);

	virtual void Activate() override;
	virtual void Deactivate() override;
	virtual WindowRegion GetWindowRegion(Vector2D<int> pos) const override;

	// TitleBar와 Margin을 제외한 Window 내부영역 좌표계를 가진 Writer를 반환합니다
	InnerAreaWriter* InnerWriter() { return &inner_writer; }
	// TitleBar와 Margin을 제외한 Window 내부영역 Dimension을 반환합니다
	Vector2D<int> InnerSize() const { return Size() - TopLeftMargin - BottomRightMargin; }
private:
	std::string title;
	InnerAreaWriter inner_writer{*this};
};

void DrawWindow(PixelWriter& writer, const char* title);
void DrawWindowTitle(PixelWriter& writer, const char* title, bool active);
void DrawTextbox(PixelWriter& writer, Vector2D<int> pos, Vector2D<int> size, uint32_t bg_color, uint32_t light_color, uint32_t shadow_color);
void DrawTextbox(PixelWriter& writer, Vector2D<int> pos, Vector2D<int> size);
