/*
 * tu_b.c — Translation-unit B for multi-TU correctness tests.
 *
 * Symmetric counterpart to tu_a.c.  See tu_a.c for the design rationale.
 */

#include "lock.h"
#include <windows.h>

/* ── single-shot helpers used by the visibility test ─────────────────── */

int tu_b_take(int lockid)    { return lock_take(lockid);    }
int tu_b_release(int lockid) { return lock_release(lockid); }

/* ── concurrent worker used by the cross-TU counter tests ────────────── */

typedef struct { int lockid; volatile int *counter; int iters; } _tu_b_arg_t;
static _tu_b_arg_t _tu_b_arg;

void tu_b_set_work(int lockid, volatile int *counter, int iters) {
    _tu_b_arg.lockid  = lockid;
    _tu_b_arg.counter = counter;
    _tu_b_arg.iters   = iters;
}

DWORD WINAPI tu_b_worker(LPVOID p) {
    (void)p;
    for (int i = 0; i < _tu_b_arg.iters; i++) {
        lock_take(_tu_b_arg.lockid);
        (*_tu_b_arg.counter)++;
        lock_release(_tu_b_arg.lockid);
    }
    return 0;
}
