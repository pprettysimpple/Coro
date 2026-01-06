# Coro - Simple coroutines in Pure C

This is a simple stb-style library, that implements yet another stackfull coroutines.

Interface is as minimal as possible:
```C
// This is main logic
void CoroInit(Coro* c, CoroFn entry, void* args);
void CoroSuspend();
void CoroResume(Coro* c);
// And mmap wrappers
Coro* CoroAlloc(size_t stack_size);
void CoroFree(Coro* coro);
```

This project is done for explorational, recreational and research purposes.

Usually, people just use `ucontext` or similar. This library uses it's own context switch login for as little unavoidable assembly as possible.
Main switch is done with just 6 instructions:
```asm
    ...
    ; rdi - from context
    ; rsi - to context
    call ContextSwitch
    ...

ContextSwitch:
    push rbp
    mov [rdi], rsp
    mov rsp, [rsi]
    pop rbp
    ret
```

You may ask: "But what happens with all the registers after this switch?"

Good question! All other registers are listed as clobbers of ContextSwitch invokation. Compiler manages them and in real function, where switch is happening, only a couple actually needs to be saved across switches.

And that's it, you know everything you need to know about this lib.
