; asmfunc.asm
;
; System V AMD64 Calling Convention
; Registers: rdi, rsi, rdx, rcx, r8, r9

bits 64
section .text
; ---------------------------------------------------------------
extern kernel_main_stack
extern KernelMain
global KernelEntryPoint	; void KernelEntryPoint(const FrameBufferConfig*, const MemoryMap*);
KernelEntryPoint:
	mov rsp, kernel_main_stack + 1024 * 1024 ; change stack area (trying to reference kernel_main_stack_begin cause weird compiler bug, so i decided to hard-code stack size here)
	call KernelMain		; go to kernel main (never returns)
.fin:					; safety loop (just in case)
	hlt
	jmp .fin
; ---------------------------------------------------------------
global AtaSoftReset   ; AtaSoftReset(uint16_t control_base);
AtaSoftReset:
  push rax
  mov al, 4       ; SRST bit
  out dx, al      ; trigger reset
  xor eax, eax
  out dx, al      ; clear SRST bit
  in al, dx       ; do 400ns delay
  in al, dx
  in al, dx
  in al, dx
.rdylp:
  in al, dx
  and al, 0xc0
  cmp al, 0x40
  jne short .rdylp
  pop rax
  ret
; ---------------------------------------------------------------
global IoOut8
IoOut8:
  mov dx, di
  mov eax, esi
  out dx, al
; ---------------------------------------------------------------
global IoIn8
IoIn8:
  mov dx, di
  in al, dx
  ret
; ---------------------------------------------------------------
global IoOut32			; void IoOut32(uint16_t addr, uint32_t data);
IoOut32:
	mov dx, di			; dx = addr
	mov eax, esi		; eax = data
	out dx, eax			; write eax(=data) to dx(=addr)
	ret
; ---------------------------------------------------------------
global IoIn32			; uint32_t IoIn32(uint16_t addr);
IoIn32:
	mov dx, di			; dx = addr
	in eax, dx			; read from dx(=addr) -> eax(return value)
	ret
; ---------------------------------------------------------------
global GetCS			; uint16_t GetCS(void);
GetCS:
	xor eax, eax		; zeros rax register
	mov ax, cs			; ax = cs
	ret
; ---------------------------------------------------------------
global GetCR3			; uint64_t GetCR3(void);
GetCR3:
	mov rax, cr3
	ret
; ---------------------------------------------------------------
global GetCR2			; uint64_t GetCR2(void);
GetCR2:
	mov rax, cr2
	ret
; ---------------------------------------------------------------
global GetCR0			; uint64_t GetCR0(void);
GetCR0:
	mov rax, cr0
	ret
; ---------------------------------------------------------------
global LoadIDT			; void LoadIDT(uint16_t limit, uint64_t offset);
LoadIDT:
	push rbp
	mov rbp, rsp
	sub rsp, 10			; calling LIDT needs 10 bytes of space
	mov [rsp], di		; limit (2 bytes)
	mov [rsp + 2], rsi	; offset (8 bytes)
	lidt [rsp]			; loads IDT (region where IDT limit, offset is stored)
	mov rsp, rbp
	pop rbp
	ret
; ---------------------------------------------------------------
global LoadGDT			; void LoadGDT(uint16_t limit, uint64_t offset);
LoadGDT:
	push rbp
	mov rbp, rsp
	sub rsp, 10			; calling LGDT needs 10 bytes of space
	mov [rsp], di		; limit (2 bytes)
	mov [rsp + 2], rsi	; offset (8 bytes)
	lgdt [rsp]			; load limit, offset to GDTR register
	mov rsp, rbp
	pop rbp
	ret
; ---------------------------------------------------------------
global LoadTR			; void LoadTR(uint16_t sel);
LoadTR:
	ltr di
	ret
; ---------------------------------------------------------------
global SetSegRegs		; void SetSegRegs(uint16_t ss, uint16_t cs);
SetSegRegs:
	push rbp
	mov rbp, rsp
	xor eax, eax
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, di
	mov rax, .next
	push rsi
	push rax
	o64 retf
.next:
	mov rsp, rbp
	pop rbp
	ret
; ---------------------------------------------------------------
global SetDS			; void SetDS(uint16_t);
global SetES			; void SetES(uint16_t);
global SetFS			; void SetFS(uint16_t);
global SetGS			; void SetGS(uint16_t);
global SetSS			; void SetSS(uint16_t);
global SetCS			; void SetCS(uint16_t);
SetDS:
	mov ds, di
	ret
SetES:
	mov es, di
	ret
SetFS:
	mov fs, di
	ret
SetGS:
	mov gs, di
	ret
SetSS:
	mov ss, di
	ret
SetCS:
	push rbp
	mov rbp, rsp
	mov rax, .next
	push rdi			; (CS)-> [???]stack
	push rax			; (RIP)-> [CS ???]stack
	o64 retf			; [(RIP CS) ???]stack (retf == far_return) small hack to update CS register using far return
.next:
	mov rsp, rbp
	pop rbp
	ret
; ---------------------------------------------------------------
global SetCR3			; void SetCR3(uint64_t x);
SetCR3:
	mov cr3, rdi
	ret
; ---------------------------------------------------------------
global SetCR0			; void SetCR0(uint64_t x);
SetCR0:
	mov cr0, rdi
	ret
; ---------------------------------------------------------------
global SwitchContext		; void SwitchContext(void* next_ctx, void* cur_ctx);
global RestoreContext		; void RestoreContext(void* next_ctx);
SwitchContext:
	; bakcup current context
	mov [rsi + 0x40], rax
	mov [rsi + 0x48], rbx
	mov [rsi + 0x50], rcx
	mov [rsi + 0x58], rdx
	mov [rsi + 0x60], rdi
	mov [rsi + 0x68], rsi

	lea rax, [rsp + 8]		; get value of cur_ctx::rsp (SwitchContext를 호출하기 전의 콘텍스트를 백업해야 하므로 +8)
	mov [rsi + 0x70], rax	; backup rsp
	mov [rsi + 0x78], rbp

	mov [rsi + 0x80], r8
	mov [rsi + 0x88], r9
	mov [rsi + 0x90], r10
	mov [rsi + 0x98], r11
	mov [rsi + 0xA0], r12
	mov [rsi + 0xA8], r13
	mov [rsi + 0xB0], r14
	mov [rsi + 0xB8], r15

	mov rax, cr3			; get value of cr3
	mov [rsi + 0x00], rax	; backup cr3
	mov rax, [rsp]			; get value of cur_ctx::rip (복귀 어드레스 = SwitchContext가 호출한 시점 + 1)
	mov [rsi + 0x08], rax	; backup rip
	pushfq					; get value of RFLAGS
	pop qword [rsi + 0x10]	; backup RFLAGS

	xor eax, eax
	xor ebx, ebx
	xor ecx, ecx
	xor edx, edx
	mov ax, cs
	mov [rsi + 0x20], rax
	mov bx, ss
	mov [rsi + 0x28], rbx
	mov cx, fs
	mov [rsi + 0x30], rcx
	mov dx, gs
	mov [rsi + 0x38], rdx

	fxsave [rsi + 0xc0]
RestoreContext:
	; iret stack frame
	push qword [rdi + 0x28]	; restore ss
	push qword [rdi + 0x70] ; restore rsp
	push qword [rdi + 0x10] ; restore rflags
	push qword [rdi + 0x20]	; restore cs
	push qword [rdi + 0x08]	; restore rip

	; restore next context
	fxrstor [rdi + 0xc0]
	
	mov rax, [rdi + 0x00]
	mov cr3, rax
	mov rax, [rdi + 0x30]
	mov fs, ax
	mov rax, [rdi + 0x38]
	mov gs, ax

	mov rax, [rdi + 0x40]
	mov rbx, [rdi + 0x48]
	mov rcx, [rdi + 0x50]
	mov rdx, [rdi + 0x58]
;	mov rdi, [rdi + 0x60]
	mov rsi, [rdi + 0x68]
;	mov rsp, [rdi + 0x70]
	mov rbp, [rdi + 0x78]
	mov r8,  [rdi + 0x80]
	mov r9,  [rdi + 0x88]
	mov r10, [rdi + 0x90]
	mov r11, [rdi + 0x98]
	mov r12, [rdi + 0xA0]
	mov r13, [rdi + 0xA8]
	mov r14, [rdi + 0xB0]
	mov r15, [rdi + 0xB8]

	mov rdi, [rdi + 0x60]

	o64 iret				; restore ss, rsp, rflags, cs, rip at once
; ---------------------------------------------------------------
extern LAPICTimerOnInterrupt
global IntHandlerLAPICTimer	; void IntHandlerLAPICTimer(InterruptFrame* frame);
IntHandlerLAPICTimer:
	push rbp
	mov rbp, rsp

	; create TaskContext Structure in the stack frame
	sub rsp, 512
	fxsave [rsp]
	push r15
	push r14
	push r13
	push r12
	push r11
	push r10
	push r9
	push r8
	push qword [rbp]		; rbp (from interrupt frame)
	push qword [rbp + 0x20] ; rsp (from interrupt frame)
	push rsi
	push rdi
	push rdx
	push rcx
	push rbx
	push rax

	xor rax, rax
	xor rbx, rbx
	xor rdx, rdx
	mov ax, fs
	mov bx, gs
	mov rcx, cr3

	push rbx				; gs
	push rax				; fs
	push qword [rbp + 0x28]	; ss (from interrupt frame)
	push qword [rbp + 0x10]	; cs (from interrupt frame)
	push rdx				; reserved
	push qword [rbp + 0x18] ; rflags (from interrupt frame)
	push qword [rbp + 0x08] ; rip (from interrupt frame)
	push rcx				; cr3

	mov rdi, rsp			; arg1 = TaskContext that was just created on the stack
	call LAPICTimerOnInterrupt
	
	add rsp, 0x40			; ignore cr3 ~ gs
	pop rax
	pop rbx
	pop rcx
	pop rdx
	pop rdi
	pop rsi
	add rsp, 16				; ignore rsp, rbp
	pop r8
	pop r9
	pop r10
	pop r11
	pop r12
	pop r13
	pop r14
	pop r15
	fxrstor [rsp]

	mov rsp, rbp
	pop rbp
	iretq
; ---------------------------------------------------------------
global WriteMSR	; void WriteMSR(uint32_t msr, uint64_t value);
WriteMSR:
	mov rdx, rsi
	shr rdx, 32
	mov eax, esi
	mov ecx, edi
	wrmsr
	ret
; ---------------------------------------------------------------
global CallApp				; int CallApp(int argc, char** argv, uint16_t ss, uint64_t rip, uint64_t rsp, uint64_t* os_stack_ptr);
CallApp:
	push rbx
	push rbp
	push r12
	push r13
	push r14
	push r15
	mov [r9], rsp			; backup os stack

	push rbp
	mov rbp, rsp
	push rdx				; SS
	push r8					; RSP
	add rdx, 8				; CS=SS+8
	push rdx				; CS
	push rcx				; RIP
	o64 retf
; ---------------------------------------------------------------
extern GetCurrentTaskOSStackPointer
extern syscall_table
global SyscallEntry		; void SyscallEntry(void);
SyscallEntry:
	push rbp
	push rcx
	push r11

	push rax					; backup syscall index
	mov rcx, r10
	and eax, 0x7fffffff			; change syscall index 0x8000'0000 ~ -> 0x0000'0000 ~
	mov rbp, rsp

	and rsp, 0xfffffffffffffff0	; stack alignment
	push rax
	push rdx
	cli
	call GetCurrentTaskOSStackPointer		; no callee-saved registers (except rax & rdx)
	sti
	mov rdx, [rsp + 0]			; rdx
	mov [rax - 16], rdx
	mov rdx, [rsp + 8]			; rax
	mov [rax - 8], rdx

	lea rsp, [rax - 16]
	pop rdx
	pop rax
	and rsp, 0xfffffffffffffff0	; stack alignment

	call [syscall_table + 8 * eax]

	mov rsp, rbp				; recover rsp
	pop rsi						; recover syscall index
	cmp esi, 0x80000002			; if syscall::exit
	je .exit
	pop r11
	pop rcx
	pop rbp
	o64 sysret
.exit:
	mov rsp, rax				; recover os stack
	mov eax, edx				; get return value from syscall

	pop r15
	pop r14
	pop r13
	pop r12
	pop rbp
	pop rbx

	ret				; jump after call app
global ExitApp					; void ExitApp(uint64_t rsp, int32_t ret_val);
ExitApp:
	mov rsp, rdi
	mov eax, esi

	pop r15
	pop r14
	pop r13
	pop r12
	pop rbp
	pop rbx

	ret				; jump after call app
; ---------------------------------------------------------------
global InvalidateTLB			; void InvalidateTLB(uint64_t addr);
InvalidateTLB:
	invlpg [rdi]
	ret