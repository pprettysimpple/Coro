#pragma once

#include <threads.h>

/*
    Simple coroutine library for C

    Features:
    - Simple, explorable and straightforward implementation
    - No dependencies. Context switching implemented in pure assembly/C

    Limitations:
    - DO NOT USE IT IN PRODUCTION CODE. This is just an educational example.
    - Only works on x86_64 Linux (SystemV ABI) currently.
    - Coroutine allocation is done in the most dumb way possible.

    Usage:
    1. Allocate a coroutine using CoroAlloc. (Or allocate it yourself and attach stack for it)
    2. Initialize the coroutine with CoroInit, providing the entry function and arguments.
    3. Use CoroResume to start or resume the coroutine.
    4. Inside the coroutine, use CoroSuspend to yield control back.
    5. Free the coroutine using CoroFree when done.
*/

// Functions with this signature can be used as coroutines
typedef void (*CoroFn)(void* args);

typedef struct { size_t data; } Ctx;

typedef enum : char {
    CORO_RUNNING   = 0,
    CORO_SUSPENDED = 1,
    CORO_DONE      = 2,
} CoroState;

typedef struct {
    // When coro is created, its state is "CORO_SUSPENDED".
    // When "Resume" is called on coro, its state becomes "CORO_RUNNING".
    // When the coro calls "Suspend", its state becomes "CORO_SUSPENDED".
    // When the coro function returns, its state becomes "CORO_DONE".
    CoroState state;

    CoroFn entry; // for startup
    void* args;   // for startup

    Ctx* return_ctx; // active when coro is running. pointing to where to return
    Ctx ctx;         // active when coro is suspended. pointing to next suspension point

    // If you cllocate coroutine yourself, set it's stack by yourself as well
    size_t stack_size;
    char* stack_begin; // lowest address of the stack memory region
} Coro;

// Can be accessed to free/resume the current coroutine
extern thread_local Coro* current_coro;

//
// "Initialize" allocated coroutine
// @entry - entry point function
// @args  - argument to pass to entry point (see examples)
// State will be "CORO_SUSPENDED"
// 
// This function does not do any allocation itself.
// Expects coro to be allocated beforehand with stack set. See convenience function "CoroAlloc".
//
void CoroInit(Coro* c, CoroFn entry, void* args);

//
// "Suspend" execution of the current coroutine and transfer control back to the original 'Resume' call.
//
void CoroSuspend();

//
// "Resume" execution of the given coroutine.
// Coroutine must be in "CORO_SUSPENDED" state.
// This function both starts and resumes coroutine execution. There is no difference.
// If the coroutine has already finished execution, abort() is called.
//
void CoroResume(Coro* c);

//
// Allocate coroutine state + stack for it in one memory block.
// This is really dumb function, that just calls mmap under the hood.
// Deallocate underlying memory with "CoroFree"
//
Coro* CoroAlloc(size_t stack_size);

//
// Free coroutine state previously allocated with "CoroAlloc".
//
void CoroFree(Coro* coro);

//
// Implementation
//

#ifdef CORO_IMPLEMENTATION

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include <sys/mman.h>

// Zero-initialized coroutine can be used as the initial coroutine
thread_local Coro init_coro = {0};
thread_local Coro* current_coro = NULL;

// Trampoline for coro startup/teardown
static void CoroStartWrapper() {
    current_coro->entry(current_coro->args);
    current_coro->state = CORO_DONE;
    CoroSuspend();
    __builtin_unreachable();
}

// All right, in order to switch context, you need to implement this two bad boys:
// 1) SetupCtx : Must setup coroutine for the first "SwitchCtx" of "Resume" function to 
//      switch into "CoroStartWrapper"
// 2) SwitchCtx: Must switch to second ctx and save current context into first ctx
// This functions do "ALMOST" the same thing as libc's makecontext/swapcontext, but
// they do not save signal mask, don't do syscalls and in general give less guarantees, but work faster
#if defined(__x86_64__) && defined(__linux__)

    static void SetupCtx(Coro* coro) {
        size_t* sp = (size_t*)(coro->stack_begin + coro->stack_size);
        sp = (size_t*)((uintptr_t)(sp) & ~0xF);   // align stack to 16 bytes
        assert(sp - 3 >= (size_t*)coro->stack_begin && "Stack is to small for initial frame");
        sp -= 1; *sp = 0xDEADBEEFDEADBEEF;        // for debugger viewing
        sp -= 1; *sp = (size_t)&CoroStartWrapper; // ret addr
        sp -= 1; *sp = 0xDEADBEEFDEADBEEF;        // rbp

        coro->ctx = (Ctx){(size_t)sp};
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
    // TODO: redzone clobber
    #define CoroSwitchCtx(from, to) \
        Ctx* from_var##__LINE__ = (from); \
        Ctx* to_var##__LINE__ = (to); \
        asm volatile ( \
            "sub $128, %%rsp\n\t" \
            "lea .switch_label_%=(%%rip), %%rax\n\t" \
            "push %%rax\n\t" \
            "push %%rbp\n\t" \
            "mov %%rsp, (%%rdi)\n\t" \
            "mov (%%rsi), %%rsp\n\t" \
            "pop %%rbp\n\t" \
            "ret\n\t" \
            ".switch_label_%=:" \
            "add $128, %%rsp\n\t" \
            : "+D"((from_var##__LINE__)), "+S"((to_var##__LINE__)) \
            : \
            : "memory" \
            , "rax", "rbx", "rcx", "rdx" \
            , "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" \
            , SWITCH_CURRENT_VEC_REGS \
        )

#elif defined(__i386__) && defined(__linux__)
    #error "TODO: 32bit linux support"
#else
    #error "Unsupported architecture for coroutine switching"
#endif

//
// Here goes coroutine management logic
//

Coro* CoroAlloc(size_t stack_size) {
    assert(stack_size >= 8 * 1024 && "Stack size too small");
    stack_size = (stack_size + 0xFFF) & ~0xFFF; // align to page size
    void* ptr = mmap(0, sizeof(Coro) + stack_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK | MAP_POPULATE, -1, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap failed");
        abort();
    }
    for (size_t i = 0; i < sizeof(Coro) + stack_size; i++) {
        ((char*)ptr)[i] = 0xDA;
    }
    Coro* coro = (Coro*)ptr;
    coro->stack_size = stack_size;
    coro->stack_begin = (char*)ptr + sizeof(Coro);
    return coro;
}

void CoroFree(Coro* coro) {
    munmap(coro, sizeof(Coro) + coro->stack_size);
}

void CoroInit(Coro* coro, CoroFn entry, void* args) {
    // prepare return addresses for startup + exit handling
    SetupCtx(coro);
    coro->entry = entry;
    coro->args = args;
    coro->state = CORO_SUSPENDED;
}

void CoroSuspend() {
    assert(current_coro->state != CORO_SUSPENDED && "Suspend called on already suspended coroutine");
    CoroSwitchCtx(&current_coro->ctx, current_coro->return_ctx);
}

void CoroResume(Coro* coro) {
    assert(coro->state == CORO_SUSPENDED && "Resume called on coroutine that is not suspended");

    // Nasty hack. Compiler does not like putting initialization into thread_local variable
    // But, you know, it's actually cooly to have initialization with zeroes
    if (current_coro == NULL) current_coro = &init_coro;

    current_coro->state = CORO_SUSPENDED;
    coro->state = CORO_RUNNING;
    coro->return_ctx = &current_coro->ctx;

    Coro* saved_coro = current_coro;
    current_coro = coro;
    CoroSwitchCtx(coro->return_ctx, &coro->ctx);
    current_coro = saved_coro;

    current_coro->state = CORO_RUNNING;
    coro->state = (coro->state == CORO_RUNNING) ? CORO_SUSPENDED : coro->state;
}


#endif

//
// Utility strip for suspend/resume. other functions are just fine
//

#ifdef CORO_STRIP_PREFIX
#define Suspend CoroSuspend
#define Resume  CoroResume
#endif
