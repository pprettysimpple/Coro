#define CORO_STRIP_PREFIX
#define CORO_IMPLEMENTATION
#include "coro.h"

#include <stdio.h>

typedef struct {
    int arg1;
    int arg2;
} MainArgs;
static void ArgsExample(MainArgs* arg) {
    // printf("ArgsExample args: arg1=%d arg2=%d\n", arg->arg1, arg->arg2);
    Suspend();
    // printf("ArgsExample resumed after first suspend\n");
}

void Counter() {
    printf("Counter started\n");
    for (int i = 1; i <= 5; i++) {
        printf("Counter %d\n", i);
        Suspend();
    }
    printf("Counter exiting\n");
}

void TestSimple() {
    Coro* coro_state = CoroAlloc(16 * 1024);

    Coro* args_example = coro_state;
    MainArgs main_args = { .arg1 = 42, .arg2 = 84 };
    CoroInit(args_example, (CoroFn)ArgsExample, &main_args);
    printf("ArgsExample coro initialized ctx=%p\n", &args_example->ctx);
    printf("ArgsExample coro=%p\n", args_example);
    Resume(args_example);
    printf("Back in main after args_example coro start, state=%d\n", args_example->state);
    Resume(args_example);
    printf("Back in main after args_example coro resumes, state=%d\n", args_example->state);

    Coro* counter = coro_state;
    CoroInit(counter, (CoroFn)Counter, NULL);
    printf("Counter coro initialized ctx=%p\n", &counter->ctx);
    printf("Counter coro=%p\n", counter);
    while (1) {
        Resume(counter);
        if (counter->state == CORO_DONE) break;
    }
}

void TestInterleaved() {
    Coro* coro_state_1 = CoroAlloc(16 * 1024);
    Coro* coro_state_2 = CoroAlloc(16 * 1024);

    CoroInit(coro_state_1, Counter, NULL);
    CoroInit(coro_state_2, Counter, NULL);

    Coro* stash[2] = { coro_state_1, coro_state_2 };
    while (1) {
        int all_done = 1;
        for (int i = 0; i < 2; i++) {
            Coro* c = stash[i];
            if (c->state != CORO_DONE) {
                Resume(c);
                if (c->state != CORO_DONE) {
                    all_done = 0;
                } else {
                    continue;
                }
                printf("Back in main after counter %d yield, state=%d\n", i, c->state);
            }
        }
        if (all_done) break;
    }
}

int main() {
    printf("=== TestSimple === Begin\n");
    TestSimple();
    printf("=== TestSimple === End\n\n");
    printf("=== TestInterleaved === Begin\n");
    TestInterleaved();
    printf("=== TestInterleaved === End\n");
    return 0;
}
