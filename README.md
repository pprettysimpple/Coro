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

Usually, people just use `ucontext` or similar. This library uses it's own context switch logic for as little unavoidable assembly as possible.
Main switch is done with just a few instructions:
```asm
    ; some code above
    sub rsp, 128
    lea rax, [.switch_label] ; save return address
    push rax
    push rbp
    mov [rdi], rsp
    mov rsp, [rsi]
    pop rbp
    ret
.switch_label:
    add rsp, 128
    ; some code below
```

You may ask: "But what happens with all the registers after this switch?"

Good question! All other registers are listed as clobbers of ContextSwitch invocation. Compiler manages them and in real function, where switch is happening, only a couple actually needs to be saved across switches.

The undesirable implications of this approach is unknown to humanity. It may break something in unintuitive way.

And that's it, you know everything you need to know about this library!
