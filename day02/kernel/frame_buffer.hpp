#pragma once

#include <vector>
#include <memory>

#include "frame_buffer_config.h"
#include "graphics.hpp"
#include "error.hpp"

class FrameBuffer {
public:
	/**
	 * @brief Initializes FrameBuffer object
	 * @param config FrameBuffConfig struct
	 * @return [Error::kUnknownPixelFormat] when unsupported pixel format has been specified
	 */
	Error Init(const FrameBufferConfig& config);

	/**
	 * @brief Copies contents from [src] to specified position[=pos]
	 * @param pos position where contents will be copied
	 * @param src contents to be copied
	 * @param src_area region to be copied inside [src]
	 * @return [Error::kUnknownPixelFormat] when pixel format mismatches, or unsupported pixel format has been specified
	 */
	Error Copy(Vector2D<int> pos, const FrameBuffer& src, const Rectangle<int>& src_area);

	/**
	 * @brief Shifts selected area[=src_area] to specified position[=dst_pos].
	 * Undefined behavior when dst_pos is out of bounds, or part of the moved area gets out of bounds.
	 * Make sure moved area stays inbound.
	 * @param dst_pos top-left position where the contents of [src_area] will be located
	 * @param src_area rectangluar area to be moved
	 */
	void Shift(Vector2D<int> dst_pos, const Rectangle<int>& src_area);

	FrameBufferWriter* Writer() { return writer.get(); }
	const FrameBufferConfig& Config() const { return config; }

private:
	FrameBufferConfig config {};
	std::vector<uint8_t> buffer {};
	std::unique_ptr<FrameBufferWriter> writer {};
};