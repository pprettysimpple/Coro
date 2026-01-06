#include "coro.h"

#include <assert.h>
#include <stdint.h>

static void CoroStartWrapper();

static void SetupCtx(Coro* coro) {
    size_t* sp = (size_t*)(coro->stack_begin + coro->stack_size);
    sp = (size_t*)((uintptr_t)(sp) & ~0xF);   // align stack to 16 bytes
    assert(sp - 3 >= (size_t*)coro->stack_begin && "Stack is to small for initial frame");
    sp -= 1; *sp = 0xDEADBEEFDEADBEEF;        // for debugger viewing
    sp -= 1; *sp = (size_t)&CoroStartWrapper; // ret addr
    sp -= 1; *sp = 0xDEADBEEFDEADBEEF;        // rbp

    coro->ctx = (Ctx){(size_t)sp};
}

// mark with "used" to keep symbol in the binary even for LTO builds
// This is an actual implementation of context switching, that is going to happen
// each time we call Suspend/Resume
void __attribute__((naked)) __attribute__((used)) SwitchCtxImpl() {
    asm volatile (
        "push %%rbp\n\t"
        "mov %%rsp, (%%rdi)\n\t" // save rsp into save->ptr
        "mov (%%rsi), %%rsp\n\t" // load rsp from to->ptr
        "pop %%rbp\n\t"
        "ret\n\t"
        :
        :
        : "memory"
    );
}

// This implementation clobbers every register available, so we need to manually specify every register there is
#define SWITCH_XMM8_VEC_REGS\
    "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"

#define SWITCH_YMM16_VEC_REGS\
    "ymm0", "ymm1", "ymm2", "ymm3", "ymm4", "ymm5", "ymm6", "ymm7", "ymm8", "ymm9", "ymm10", "ymm11", "ymm12", "ymm13", "ymm14", "ymm15"

#define SWITCH_ZMM32_VEC_REGS\
    "zmm0", "zmm1", "zmm2", "zmm3", "zmm4", "zmm5", "zmm6", "zmm7", "zmm8", "zmm9",\
    "zmm10", "zmm11", "zmm12", "zmm13", "zmm14", "zmm15", "zmm16", "zmm17", "zmm18", "zmm19",\
    "zmm20", "zmm21", "zmm22", "zmm23", "zmm24", "zmm25", "zmm26", "zmm27", "zmm28", "zmm29",\
    "zmm30", "zmm31"

#if defined(__AVX512F__)
#define SWITCH_CURRENT_VEC_REGS SWITCH_ZMM32_VEC_REGS
#elif defined(__AVX__)
#define SWITCH_CURRENT_VEC_REGS SWITCH_YMM16_VEC_REGS
#else
#define SWITCH_CURRENT_VEC_REGS SWITCH_XMM8_VEC_REGS
#endif

// Switch from one stack to another
// This inline asm tells compiler that this call shits basically everything in the environment
// And it's compiler job to figure out what to do with this information
// So compiler becomes THE builder of context we gonna switch back to
// We basically need to specify ALL registers here, not just caller or callee saved. All of them
// Other solutions for context switch save everything that is callee-saved and live with it,
//  but here I'm trying another approach
#define SwitchCtx(from, to) \
    Ctx* from_var##__LINE__ = (from); \
    Ctx* to_var##__LINE__ = (to); \
    asm volatile ( \
        "call SwitchCtxImpl\n\t" \
        : "+D"((from_var##__LINE__)), "+S"((to_var##__LINE__)) \
        : \
        : "memory" \
        , "rax", "rbx", "rcx", "rdx" \
        , "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" \
        , SWITCH_CURRENT_VEC_REGS \
        , "cc" \
    )
