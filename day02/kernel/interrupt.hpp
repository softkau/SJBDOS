#pragma once
#include <cstdint>
#include <type_traits>
#include <array>
#include <deque>

#include "x86_descriptor.hpp"
#include "message.hpp"

#if 1
struct InterruptFrame {
	uint64_t rip;		// $00 ($rbp + 0x08)
	uint64_t cs;		// $08 ($rbp + 0x10)
	uint64_t rflags;	// $10 ($rbp + 0x18)
	uint64_t rsp;		// $18 ($rbp + 0x20)
	uint64_t ss;		// $20 ($rbp + 0x28)
};

#elif
/* Interrupt stack frame. */
struct gp_registers {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t r11;
	uint64_t r10;
	uint64_t r9;
	uint64_t r8;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t rbp;
	uint64_t rdx;
	uint64_t rcx;
	uint64_t rbx;
	uint64_t rax;
} __attribute__((packed));

struct InterruptFrame {
	/* Pushed by intr_entry in intr-stubs.S.
	   These are the interrupted task's saved registers. */
	struct gp_registers R; // 범용 레지스터 - 120 바이트 
	uint16_t es;
	uint16_t __pad1;
	uint32_t __pad2;
	uint16_t ds; // 세그먼트 관리 
	uint16_t __pad3;
	uint32_t __pad4;
	/* Pushed by intrNN_stub in intr-stubs.S. */
	uint64_t vec_no; /* Interrupt vector number. 인터럽트 종류 */
/* Sometimes pushed by the CPU,
   otherwise for consistency pushed as 0 by intrNN_stub.
   The CPU puts it just under `eip', but we move it here. */
	uint64_t error_code;
/* Pushed by the CPU.
   These are the interrupted task's saved registers. */
	uintptr_t rip;  // pc
	uint16_t cs; // 세그먼트 관리 
	uint16_t __pad5;
	uint32_t __pad6;
	uint64_t rflags;// cpu 상태를 나타내는 정보 
	uintptr_t rsp; // 스택 포인터 
	uint16_t ss; 
	uint16_t __pad7;
	uint32_t __pad8;
} __attribute__((packed));

#endif

using InterruptHandler = std::add_pointer_t<void(InterruptFrame*)>;

union InterruptDescriptorAttribute {
	uint16_t data;
	struct {
		uint16_t interrupt_stack_table : 3; // mostly zero (3 bits)
		uint16_t : 5;
		DescriptorType type : 4; // descriptor type (4 bits)
		uint16_t : 1;
		uint16_t descriptor_privilege_level : 2; // privilege level of interrupt handler (2 bits)
		uint16_t present : 1; // value 1 for vaild descriptor (1 bit)
	} __attribute__((packed)) bits;
	static_assert(sizeof(bits) == sizeof(data));
} __attribute__((packed));

struct InterruptDescriptor {
	uint16_t offset_low; // interrupt handler 16-bit address
	uint16_t segment_selector; // code segment
	InterruptDescriptorAttribute attrib; // descriptor attributes
	uint16_t offset_middle; // interrupt handler 32-bit address (upper)
	uint32_t offset_high; // interrupt handler 64-bit address (upper)
	uint32_t reserved; // unused
} __attribute__((packed));

/**
 * @brief initializes IDT attribute with given parameters
 * 
 * @param type type of handler (ex: InterruptGate)
 * @param descriptor_privilege_level privilege level of handler
 * @param present value 1 when it is a vaild IDT
 * @param interrupt_stack_table mostly zero...?
 * @return IDT attribute structure
 */
constexpr InterruptDescriptorAttribute MakeIDTAttr(
		DescriptorType type,
		uint8_t descriptor_privilege_level,
		bool present = true,
		uint8_t interrupt_stack_table = 0) {
	InterruptDescriptorAttribute attr{};
	attr.bits.interrupt_stack_table = interrupt_stack_table;
	attr.bits.type = type;
	attr.bits.descriptor_privilege_level = descriptor_privilege_level;
	attr.bits.present = present;
	return attr;
}

class InterruptVector {
public:
	enum Number : int {
		XHCI = 0x40,
		LAPICTImer = 0x41,
	};
	// Loads IDT vector into CPU (should be called once after all IDT entry has been set)
	static void Load();
};

/**
 * @brief Loads specified interrupt handler into IDT vector.
 * 
 * @param id IDT vector ID
 * @param attr IDT attribute structure
 * @param handler pointer to interrupt handler
 * @param segment_selector segment selector value(ex: cs(code segment register))
 */
void SetIDTEntry(InterruptVector::Number id,
				 InterruptDescriptorAttribute attr,
				 uint64_t handler,
				 uint16_t segment_selector);

// Notifies CPU [End of Interrupt]
void NotifyEOI();

#define DISABLE_INTERRUPT __asm__("cli")
#define ENABLE_INTERRUPT __asm__("sti")
#define ENABLE_INTERRUPT_AND_HALT __asm__("sti\n\thlt")

/* 인터럽트 핸들러를 설정합니다. */
void InitializeInterrupt();

template <class Func>
void InterruptGuard(Func f) {
	DISABLE_INTERRUPT;
	f();
	ENABLE_INTERRUPT;
}
