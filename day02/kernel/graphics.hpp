#pragma once
#include "frame_buffer_config.h"
#include <utility>
#include <algorithm>

struct PixelColor {
	uint8_t r, g, b;
	bool operator==(const PixelColor& rhs) const {
		return r == rhs.r && g == rhs.g && b == rhs.b;
	}
	bool operator!=(const PixelColor& rhs) const {
		return !(*this == rhs);
	}
};

constexpr PixelColor DESKTOP_BG_COLOR = { 45, 118, 237}; // #2d76ed
constexpr PixelColor DESKTOP_FG_COLOR = {255, 255, 255}; // #ffffff

template<typename T>
struct Vector2D {
	T x, y;

	template <typename U>
	Vector2D<T>& operator+=(const Vector2D<U>& rhs) {
		x += rhs.x;
		y += rhs.y;
		return *this;
	}

	template <typename U>
	Vector2D<T>& operator-=(const Vector2D<U>& rhs) {
		x -= rhs.x;
		y -= rhs.y;
		return *this;
	}

	template <typename X, typename Y>
	using promoted_t = decltype(std::declval<X>() + std::declval<Y>());

	template <typename U>
	auto operator+(const Vector2D<U>& rhs) const {
		return Vector2D<promoted_t<T, U>>{ x + rhs.x, y + rhs.y };
	}
	template <typename U>
	auto operator-(const Vector2D<U>& rhs) const {
		return Vector2D<promoted_t<T, U>>{ x - rhs.x, y - rhs.y };
	}
};

template<typename T>
constexpr Vector2D<T> vec_add(const Vector2D<T>& lhs, const Vector2D<T>& rhs) {
	return {
		lhs.x + rhs.x,
		lhs.y + rhs.y
	};
}

template<typename T>
constexpr Vector2D<T> vec_sub(const Vector2D<T>& lhs, const Vector2D<T>& rhs) {
	return {
		lhs.x - rhs.x,
		lhs.y - rhs.y
	};
}

template<typename T>
constexpr Vector2D<T> vec_multiply(const Vector2D<T>& lhs, const Vector2D<T>& rhs) {
	return {
		lhs.x * rhs.x,
		lhs.y * rhs.y
	};
}

template<typename T>
Vector2D<T> ElementMax(const Vector2D<T>& lhs, const Vector2D<T>& rhs) {
	return {std::max(lhs.x, rhs.x), std::max(lhs.y, rhs.y)};
}

template<typename T>
Vector2D<T> ElementMin(const Vector2D<T>& lhs, const Vector2D<T>& rhs) {
	return {std::min(lhs.x, rhs.x), std::min(lhs.y, rhs.y)};
}

template<typename T>
struct Rectangle {
	Vector2D<T> pos, size;
	Rectangle<T> operator&(const Rectangle<T>& rhs) const {
		const auto end = this->pos + this->size;
		const auto rhs_end = rhs.pos + rhs.size;
		if (end.x < rhs.pos.x || end.y < rhs.pos.y ||
		    rhs_end.x < pos.x || rhs_end.y < pos.y) {
				return {{0,0}, {0,0}};
		}

		auto new_pos = ElementMax(this->pos, rhs.pos);
		auto new_size = ElementMin(end, rhs_end) - new_pos;
		return { new_pos, new_size };
	}
};
// typename alias
#define Rect Rectangle

class PixelWriter {
public:
	virtual ~PixelWriter() = default;
	virtual void Write(Vector2D<int> pos, const PixelColor& c) = 0;
	virtual int Width() const = 0;
	virtual int Height() const = 0;
};

class FrameBufferWriter : public PixelWriter {
public:
	FrameBufferWriter(const FrameBufferConfig& config) : config{config} {}
	virtual ~FrameBufferWriter() = default;
	virtual int Width() const override { return config.horizontal_resolution; }
	virtual int Height() const override { return config.vertical_resolution; }

protected:
	uint8_t* PixelAt(int x, int y) {
		return config.frame_buffer + (config.pixels_per_scan_line*y + x)*4;
	}
private:
	const FrameBufferConfig& config;
};

class RGBPixelWriter : public FrameBufferWriter {
public:
	RGBPixelWriter(const FrameBufferConfig& config) : FrameBufferWriter(config) {}
	virtual void Write(Vector2D<int> pos, const PixelColor& c) override;
};

class BGRPixelWriter : public FrameBufferWriter {
public:
	BGRPixelWriter(const FrameBufferConfig& config) : FrameBufferWriter(config) {}
	virtual void Write(Vector2D<int> pos, const PixelColor& c) override;
};

inline constexpr PixelColor ToColor(uint32_t c) {
	return PixelColor{
		uint8_t(c >> 16),
		uint8_t(c >> 8),
		uint8_t(c)
	};
}

namespace gfx::color {
	constexpr PixelColor WHITE = ToColor(0xffffff);
	constexpr PixelColor BLACK = ToColor(0x000000);
	constexpr PixelColor RED   = ToColor(0xff0000);
};

void FillRect(PixelWriter& writer, const Vector2D<int>& pos, const Vector2D<int>& size, const PixelColor& c);
void DrawRect(PixelWriter& writer, const Vector2D<int>& pos, const Vector2D<int>& size, const PixelColor& c);
void FillRect(PixelWriter& writer, const Rectangle<int>& area, const PixelColor& c);
void DrawRect(PixelWriter& writer, const Rectangle<int>& area, const PixelColor& c);
void DrawDesktop(PixelWriter& writer);

extern FrameBufferConfig kScreenConfig;
extern PixelWriter* kScreenWriter;
Vector2D<int> ScreenSize();

/**
 * @brief 그래픽 렌더링 관련 코드를 초기화합니다.
 * @details kScreenConfig, kScreenWriter가 초기화됩니다. Draw/Fill* 함수가 이용가능해집니다.
 * @param screen_config 디바이스의 FrameBufferConfig
 */
void InitializeGraphics(const FrameBufferConfig& screen_config);
