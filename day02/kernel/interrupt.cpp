#include "graphics.hpp"
#include "interrupt.hpp"
#include "asmfunc.h"
#include "segment.hpp"
#include "timer.hpp"
#include "task.hpp"
#include "font.hpp"
#include "paging.hpp"
#include <string_view>
#include <csignal>

void KillApp(InterruptFrame* frame) {
	auto& task = task_manager->CurrentTask();
	__asm__("sti"); // enable interrupt
	ExitApp(task.os_stack_ptr, 128 + SIGSEGV);
}

void PrintHex(uint64_t value, int width, Vector2D<int> pos) {
	const char* hex = "0123456789abcdef";
	for (int i = 0; i < width; i++) {
		int x = (value >> 4 * (width - i - 1)) & 0xfu;
		font::WriteASCII(*kScreenWriter, pos + Vector2D<int>{font::FONT_WIDTH * i,0}, hex[x], gfx::color::RED);
	}
}

void PrintFrame(InterruptFrame* frame, std::string_view sv_exp) {
	auto pos = [](int x, int y) {
		return Vector2D<int> {
			500 + font::FONT_WIDTH * x,
			font::FONT_HEIGHT * y
		};
	};

	for (size_t i = 0; i < sv_exp.size(); i++) {
		font::WriteASCII(*kScreenWriter, pos(i, 0), sv_exp.data()[i], gfx::color::RED);
	}
	font::WriteString(*kScreenWriter, pos(0,1), "CS:RIP", gfx::color::RED);
	PrintHex(frame->cs, 4, pos(7,1));
	PrintHex(frame->rip, 16, pos(12,1));
	font::WriteString(*kScreenWriter, pos(0,2), "RFLAGS", gfx::color::RED);
	PrintHex(frame->rflags, 16, pos(7,2));
	font::WriteString(*kScreenWriter, pos(0,3), "SS:RSP", gfx::color::RED);
	PrintHex(frame->ss, 4, pos(7,3));
	PrintHex(frame->rsp, 16, pos(12,3));
}

namespace fault {
	#define DefineFault(fault_name, with_error) \
	struct fault_name { \
		constexpr static std::string_view str = #fault_name; \
	}
	
	DefineFault(DE, 0);
	DefineFault(DB, 0);
	DefineFault(BP, 0);
	DefineFault(OF, 0);
	DefineFault(BR, 0);
	DefineFault(UD, 0);
	DefineFault(NM, 0);
	DefineFault(DF, 1);
	DefineFault(TS, 1);
	DefineFault(NP, 1);
	DefineFault(SS, 1);
	DefineFault(GP, 1);
	// DefineFault(PF, 1);
	DefineFault(MF, 0);
	DefineFault(AC, 1);
	DefineFault(MC, 0);
	DefineFault(XM, 0);
	DefineFault(VE, 0);
}

template <class FaultType>
__attribute__((interrupt)) void IntHandlerWE(InterruptFrame* frame, uint64_t error_code) {
	if ((frame->cs & 0b11) == 0b11) {
		KillApp(frame);
	}
	PrintFrame(frame, FaultType::str);

	font::WriteString(*kScreenWriter, {500, font::FONT_HEIGHT * 4}, "ERR", gfx::color::RED);
	PrintHex(error_code, 16, { Vector2D<int> { 500, 0 } + vec_multiply(font::FONT_SIZE, { 4, 4 }) });
	while (true) __asm__("hlt");
}

template <class FaultType>
__attribute__((interrupt)) void IntHandlerNE(InterruptFrame* frame) {
	if ((frame->cs & 0b11) == 0b11) {
		KillApp(frame);
	}
	PrintFrame(frame, FaultType::str);
	while (true) __asm__("hlt");
}

__attribute__((interrupt)) void IntHandlerPF(InterruptFrame* frame, uint64_t error_code) {
	uint64_t cr2 = GetCR2();
	if (auto err = HandlePageFault(error_code, cr2); !err) {
		return;
	}
	if ((frame->cs & 0b11) == 0b11) { // cpl = 3
		KillApp(frame);
	}
	PrintFrame(frame, "PF");
	font::WriteString(*kScreenWriter, {500, font::FONT_HEIGHT * 4}, "ERR", gfx::color::RED);
	PrintHex(error_code, 16, { Vector2D<int> { 500, 0 } + vec_multiply(font::FONT_SIZE, { 4, 4 }) });
	while (true) __asm__("hlt");
}

namespace g {
std::array<InterruptDescriptor, 256> idt;
}

void InterruptVector::Load() {
	LoadIDT(sizeof g::idt - 1, reinterpret_cast<uintptr_t>(&g::idt[0]));
}

void SetIDTEntry(InterruptVector::Number id,
				 InterruptDescriptorAttribute attr,
				 uint64_t handler,
				 uint16_t segment_selector) {
	auto& desc = g::idt[id];
	auto offset = handler;
	desc.attrib = attr;
	desc.offset_low = offset & 0xFFFFu;
	desc.offset_middle = (offset >> 16) & 0xFFFFu;
	desc.offset_high = (offset >> 32) & 0xFFFFu;
	desc.segment_selector = segment_selector;
}

void NotifyEOI() {
	volatile auto eoi_register = reinterpret_cast<uint32_t*>(0xFEE000B0u);
	*eoi_register = 0;
}

namespace {
	__attribute__((interrupt))
	void IntHandlerXHCI(InterruptFrame* frame) {
		task_manager->SendMsg(1, Message{Message::InterruptXHCI});
		NotifyEOI();
	}

}

extern "C" void IntHandlerLAPICTimer(InterruptFrame* frame);

void InitializeInterrupt() {
	auto set_idt_entry = [](int irq, auto handler) {
		auto num = static_cast<InterruptVector::Number>(irq);
		SetIDTEntry(num, MakeIDTAttr(DescriptorType::InterruptGate, 0), reinterpret_cast<uint64_t>(handler), kKernelCS);
	};
	set_idt_entry(InterruptVector::XHCI, IntHandlerXHCI);
	SetIDTEntry(
		InterruptVector::LAPICTImer,
		MakeIDTAttr(DescriptorType::InterruptGate, 0, true, kISTForTimer),
		reinterpret_cast<uint64_t>(IntHandlerLAPICTimer),
		kKernelCS
	);
	set_idt_entry(0, IntHandlerNE<fault::DE>);
	set_idt_entry(1, IntHandlerNE<fault::DB>);
	set_idt_entry(3, IntHandlerNE<fault::BP>);
	set_idt_entry(4, IntHandlerNE<fault::OF>);
	set_idt_entry(5, IntHandlerNE<fault::BR>);
	set_idt_entry(6, IntHandlerNE<fault::UD>);
	set_idt_entry(7, IntHandlerNE<fault::NM>);
	set_idt_entry(8, IntHandlerWE<fault::DF>);
	set_idt_entry(10, IntHandlerWE<fault::TS>);
	set_idt_entry(11, IntHandlerWE<fault::NP>);
	set_idt_entry(12, IntHandlerWE<fault::SS>);
	set_idt_entry(13, IntHandlerWE<fault::GP>);
	set_idt_entry(14, IntHandlerPF);
	set_idt_entry(16, IntHandlerNE<fault::MF>);
	set_idt_entry(17, IntHandlerWE<fault::AC>);
	set_idt_entry(18, IntHandlerNE<fault::MC>);
	set_idt_entry(19, IntHandlerNE<fault::XM>);
	set_idt_entry(20, IntHandlerNE<fault::VE>);
	InterruptVector::Load();
}
