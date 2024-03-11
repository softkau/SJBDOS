#include "frame_buffer.hpp"
#include <cstring>

namespace {	
	constexpr int Bits2Bytes(int bits) { return (bits + 0b111) >> 3; }
	constexpr int BytesPerPixel(PixelFormat format) {
		switch (format) {
			case PixelFormat::kPixelRGBResv8BitPerColor: return 4;
			case PixelFormat::kPixelBGRResv8BitPerColor: return 4;
			default: return -1;
		}
	}
	constexpr int BytesPerScanLine(const FrameBufferConfig& config) {
		return BytesPerPixel(config.pixel_format) * config.pixels_per_scan_line;
	}
	constexpr uint8_t* FrameBufPtr(Vector2D<int> pos, const FrameBufferConfig& config) {
		return config.frame_buffer + BytesPerScanLine(config) * pos.y + BytesPerPixel(config.pixel_format) * pos.x;
	}
	Vector2D<int> FrameBufferSize(const FrameBufferConfig& config) {
		return {
			static_cast<int>(config.horizontal_resolution),
			static_cast<int>(config.vertical_resolution)
		};
	}
}

Error FrameBuffer::Init(const FrameBufferConfig& config) {
	this->config = config;

	const auto bytes_per_pixel = BytesPerPixel(config.pixel_format);
	if (bytes_per_pixel <= 0) {
		return MakeError(Error::kUnknownPixelFormat);
	}

	if (this->config.frame_buffer) {
		buffer.clear();
		buffer.shrink_to_fit();
	}
	else {
		buffer.resize(bytes_per_pixel * config.horizontal_resolution * config.vertical_resolution);
		this->config.frame_buffer = buffer.data();
		this->config.pixels_per_scan_line = config.horizontal_resolution;
	}

	switch (config.pixel_format) {
		case kPixelRGBResv8BitPerColor:
			writer = std::make_unique<RGBPixelWriter>(this->config);
			break;
		case kPixelBGRResv8BitPerColor:
			writer = std::make_unique<BGRPixelWriter>(this->config);
			break;
		default:
			return MakeError(Error::kUnknownPixelFormat);
	}

	return MakeError(Error::kSuccess);
}

Error FrameBuffer::Copy(Vector2D<int> pos, const FrameBuffer& src, const Rectangle<int>& src_area) {
	if (config.pixel_format != src.config.pixel_format) {
		return MakeError(Error::kUnknownPixelFormat);
	}

	const auto bytes_per_pixel = BytesPerPixel(config.pixel_format);
	if (bytes_per_pixel <= 0)
		return MakeError(Error::kUnknownPixelFormat);

	// calculates area to copy (excluding out of bounds) v v v
	const Rect<int> src_area_shifted{ pos, src_area.size };
	const Rect<int> src_outline{ pos - src_area.pos, FrameBufferSize(src.config) };
	const Rect<int> dst_outline{ {0, 0}, FrameBufferSize(config) };
	const Rect<int> copy_area = dst_outline & src_outline & src_area_shifted;
	const auto start_pos = copy_area.pos - (pos - src_area.pos);
	// calculates area to copy (excluding out of bounds) ^ ^ ^

	uint8_t* dst_buf = FrameBufPtr(copy_area.pos, config);
	const uint8_t* src_buf = FrameBufPtr(start_pos, src.config);

	for (int dy = 0; dy < copy_area.size.y; dy++) {
		memcpy(dst_buf, src_buf, bytes_per_pixel * copy_area.size.x);
		dst_buf += BytesPerScanLine(config);
		src_buf += BytesPerScanLine(src.config);
	}

	return MakeError(Error::kSuccess);
}

void FrameBuffer::Shift(Vector2D<int> dst_pos, const Rectangle<int>& src_area) {
	if (dst_pos.y == src_area.pos.y) return;

	const auto bytes_per_pixel = BytesPerPixel(config.pixel_format);
	const auto bytes_per_scan_line = BytesPerScanLine(config);

	if (dst_pos.y < src_area.pos.y) { // shift up
		auto dst_buf = FrameBufPtr(dst_pos, config);
		auto src_buf = FrameBufPtr(src_area.pos, config);

		for (int y = 0; y < src_area.size.y; y++) {
			memcpy(dst_buf, src_buf, bytes_per_pixel * src_area.size.x);
			dst_buf += bytes_per_scan_line;
			src_buf += bytes_per_scan_line;
		}
	}
	else { // shift down
		auto dst_buf = FrameBufPtr(     dst_pos + Vector2D<int>{0,src_area.size.y-1}, config);
		auto src_buf = FrameBufPtr(src_area.pos + Vector2D<int>{0,src_area.size.y-1}, config);

		for (int y = 0; y < src_area.size.y; y++) {
			memcpy(dst_buf, src_buf, bytes_per_pixel * src_area.size.x);
			dst_buf -= bytes_per_scan_line;
			src_buf -= bytes_per_scan_line;
		}
	}
}