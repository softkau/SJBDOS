#include "memory_manager.hpp"
#include "logger.hpp"

extern "C" void* prog_brk, *prog_brk_end;

BitmapMemoryManager::BitmapMemoryManager() : alloc_map{}, range_begin{FrameID(0)}, range_end{FrameCount} {}

Optional<FrameID> BitmapMemoryManager::Allocate(size_t num_frames) {
	size_t start_frame_id = range_begin.ID();
	
	while (true) {
		size_t i = 0;
		for (; i < num_frames; i++) {
			if (start_frame_id + i >= range_end.ID())
				return MakeError(Error::kNoEnoughMemory); // end condition 1

			if (GetBit(FrameID(start_frame_id + i)))
				break;
		}
		if (i == num_frames) { // found needed free spaces
			MarkAllocated(FrameID(start_frame_id), num_frames);
			return FrameID(start_frame_id); // end condition 2
		}
		// failed, retry finding free spaces
		start_frame_id += i + 1;
	}
}

Error BitmapMemoryManager::Free(FrameID start_frame, size_t num_frames) {
	for (size_t i = 0; i < num_frames; i++)
		SetBit(FrameID(start_frame.ID() + i), 0);
	return MakeError(Error::kSuccess);
}

void BitmapMemoryManager::MarkAllocated(FrameID start_frame, size_t num_frames) {
	for (size_t i = 0; i < num_frames; i++) {
		SetBit(FrameID(start_frame.ID() + i), 1);
	}
}

void BitmapMemoryManager::SetMemoryRange(FrameID first, FrameID last) {
	this->range_begin = first;
	this->range_end = last;
}

bool BitmapMemoryManager::GetBit(FrameID frame) const {
	auto line_idx = frame.ID() / BitsPerMapLine;
	auto bit_idx = frame.ID() % BitsPerMapLine;
	return (alloc_map[line_idx] & (static_cast<MapLineType>(1) << bit_idx)) != 0;
}

void BitmapMemoryManager::SetBit(FrameID frame, bool allocated) {
	auto line_idx = frame.ID() / BitsPerMapLine;
	auto bit_idx = frame.ID() % BitsPerMapLine;
	if (allocated) {
		alloc_map[line_idx] |= (static_cast<MapLineType>(1) << bit_idx);
	} else {
		alloc_map[line_idx] &= ~(static_cast<MapLineType>(1) << bit_idx);
	}
}

MemoryStat BitmapMemoryManager::Stat() const {
	size_t sum = 0;
	for (int i = range_begin.ID() / BitsPerMapLine; i < range_end.ID() / BitsPerMapLine; ++i) {
		sum += std::bitset<BitsPerMapLine>(alloc_map[i]).count();
	}
	return { sum, range_end.ID() - range_begin.ID() };
}

namespace {
	char memory_manager_buf[sizeof(BitmapMemoryManager)];

	Error InitHeap(BitmapMemoryManager& memory_manager) {
		const size_t HeapFrames = 128_MiB / BytesPerFrame;
		const auto heap_start = memory_manager.Allocate(HeapFrames);
		if (!heap_start.has_value)
			return heap_start.error;

		prog_brk = reinterpret_cast<void*>(heap_start.value.ID() * BytesPerFrame);
		prog_brk_end = reinterpret_cast<void*>(
			reinterpret_cast<char*>(prog_brk) + HeapFrames * BytesPerFrame
		);
		return MakeError(Error::kSuccess);
	}
}

BitmapMemoryManager* memory_manager;

void InitializeMemoryManager(const MemoryMap& memory_map) {
	::memory_manager = new(memory_manager_buf) BitmapMemoryManager;

	uint64_t available_end = 0;
	const auto memmap = reinterpret_cast<uintptr_t>(memory_map.buffer);
	for (uintptr_t iter = memmap; iter < memmap + memory_map.map_size; iter += memory_map.descriptor_size) {
		const auto desc = reinterpret_cast<const MemoryDescriptor*>(iter);
		if (available_end < desc->physical_start) {
			memory_manager->MarkAllocated(
				FrameID(available_end / BytesPerFrame),
				(desc->physical_start - available_end) / BytesPerFrame
			);
		}

		const auto physical_end = desc->physical_start + desc->number_of_pages * UEFI_PAGE_SIZE;
		if (IsAvailable(static_cast<MemoryType>(desc->type))) {
			available_end = physical_end;
		}
		else { // already in use
			memory_manager->MarkAllocated(
				FrameID(desc->physical_start / BytesPerFrame),
				desc->number_of_pages * UEFI_PAGE_SIZE / BytesPerFrame
			);
		}
	}
	memory_manager->SetMemoryRange(FrameID(1), FrameID(available_end / BytesPerFrame));

	if (auto err = InitHeap(*memory_manager)) {
		Log(kError, "failed to allocate pages: %s at %s:%d\n", err.Name(), err.File(), err.Line());
		exit(1);
	}
}
