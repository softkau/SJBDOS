#pragma once
#include <cstddef>
#include <limits>
#include <bitset>
#include "error.hpp"
#include "memmap.h"

namespace {
	constexpr unsigned long long operator""_KiB(unsigned long long kib) {
		return kib * 1024;
	}
	constexpr unsigned long long operator""_MiB(unsigned long long mib) {
		return mib * 1024_KiB;
	}
	constexpr unsigned long long operator""_GiB(unsigned long long gib) {
		return gib * 1024_MiB;
	}
}

static constexpr auto BytesPerFrame{4_KiB};

class FrameID {
public:
	explicit constexpr FrameID(size_t id) : id{id} {}
	constexpr size_t ID() const { return id; }
	void* Frame() const { return reinterpret_cast<void*>(id * BytesPerFrame); }
private:
	size_t id;
};

static constexpr FrameID NullFrame(std::numeric_limits<size_t>::max());

struct MemoryStat {
	size_t allocated_frames;
	size_t total_frames;
};

class BitmapMemoryManager {
public:
	static constexpr auto MaxPhysicalMemoryBytes{128_GiB};
	static constexpr auto FrameCount{MaxPhysicalMemoryBytes / BytesPerFrame};
	using MapLineType = uint64_t;
	static constexpr auto BitsPerMapLine{8 * sizeof(MapLineType)};

	BitmapMemoryManager();
	
	Optional<FrameID> Allocate(size_t num_frames);
	Error Free(FrameID start_frame, size_t num_frames);
	void MarkAllocated(FrameID start_frame, size_t num_frames);

	void SetMemoryRange(FrameID start, FrameID last);
	MemoryStat Stat() const;
private:
	std::array<MapLineType, FrameCount / BitsPerMapLine> alloc_map;
	FrameID range_begin;
	FrameID range_end;

	bool GetBit(FrameID frame) const;
	void SetBit(FrameID frame, bool allocated);
};

extern BitmapMemoryManager* memory_manager;
void InitializeMemoryManager(const MemoryMap& memory_map);
