#pragma once

#ifdef __cplusplus
#include <cstdint>
#define EXTERN_C_BEG extern "C" {
#define EXTERN_C_END }
#elif
#include <stdint.h>
#define EXTERN_C_BEG
#define EXTERN_C_END
#endif

/**
 * @file Implementations are in ./asmfunc.asm
 */

EXTERN_C_BEG
void AtaSoftReset(uint16_t control_base);
void IoOut8(uint16_t addr, uint8_t data);
uint8_t IoIn8(uint16_t addr);
void IoOut32(uint16_t addr, uint32_t data); // writes data at addr
uint32_t IoIn32(uint16_t addr); // reads 32bit integer value from addr
uint16_t GetCS(void); // reads code segment register value
uint64_t GetCR3(void);
uint64_t GetCR2(void);
uint64_t GetCR0(void);
void LoadIDT(uint16_t limit, uint64_t offset);
void LoadGDT(uint16_t limit, uint64_t offset);
void LoadTR(uint16_t sel);
void SetDS(uint16_t x);
void SetES(uint16_t x);
void SetFS(uint16_t x);
void SetGS(uint16_t x);
void SetSS(uint16_t x);
void SetCS(uint16_t x);
void SetCR3(uint64_t x);
void SetCR0(uint64_t x);
void SetSegRegs(uint16_t ss, uint16_t cs);

/**
 * @brief 함수 호출 시 cur_ctx에서 next_ctx로 콘텍스트를 스위칭합니다.
 * 
 * @param next_ctx 타깃 콘텍스트
 * @param cur_ctx 현재 콘텍스트
 */
void SwitchContext(void* next_ctx, void* cur_ctx);
void RestoreContext(void* next_ctx);
/**
 * @brief ELF 애플리케이션을 실행합니다
 * 
 * @param argc argument 개수
 * @param argv argument vector
 * @param ss 애플리케이션 ss 값 (cs 값은 ss + 8로 계산됨)
 * @param rip 애플리케이션 entry point
 * @param rsp 애플리케이션 스택 포인터
 * @param os_stack_ptr OS 스택 포인터가 저장될 위치
 */
int CallApp(int argc, char** argv, uint16_t ss, uint64_t rip, uint64_t rsp, uint64_t* os_stack_ptr);

// from: https://sites.uclouvain.be/SystInfo/usr/include/asm/msr-index.h.html
#define kIA32_EFER  0xC0000080 // Extended Feature Enable Register
#define kIA32_STAR  0xc0000081 // (legacy mode) SYSCALL TARget
#define kIA32_LSTAR 0xc0000082 // Long mode SYSCALL TARget
#define kIA32_CSTAR 0xc0000083 // Compat mode SYSCALL TARget
#define kIA32_FMASK 0xc0000084 // EFLAGS mask for syscall

void WriteMSR(uint32_t msr, uint64_t value);
void SyscallEntry(void);
void ExitApp(uint64_t rsp, int32_t ret_val);
void InvalidateTLB(uint64_t addr);
EXTERN_C_END
