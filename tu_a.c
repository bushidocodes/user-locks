/*
 * tu_a.c — Translation-unit A for multi-TU correctness tests.
 *
 * This file includes lock.h and calls the lock API exactly as any real
 * source file would.  When linked together with tu_b.c and test_lock.c,
 * all three TUs must share the single _lktable defined in lock.c.  If
 * the table were still a static-in-the-header variable, each TU would
 * get its own copy and the cross-TU counter test would produce the wrong
 * total (or deadlock).
 */

#include "lock.h"
#include <windows.h>

/* ── single-shot helpers used by the visibility test ─────────────────── */

int tu_a_take(int lockid)    { return lock_take(lockid);    }
int tu_a_release(int lockid) { return lock_release(lockid); }

/* ── concurrent worker used by the cross-TU counter tests ────────────── */

typedef struct { int lockid; volatile int *counter; int iters; } _tu_a_arg_t;
static _tu_a_arg_t _tu_a_arg;

void tu_a_set_work(int lockid, volatile int *counter, int iters) {
    _tu_a_arg.lockid  = lockid;
    _tu_a_arg.counter = counter;
    _tu_a_arg.iters   = iters;
}

DWORD WINAPI tu_a_worker(LPVOID p) {
    (void)p;
    for (int i = 0; i < _tu_a_arg.iters; i++) {
        lock_take(_tu_a_arg.lockid);
        (*_tu_a_arg.counter)++;
        lock_release(_tu_a_arg.lockid);
    }
    return 0;
}
