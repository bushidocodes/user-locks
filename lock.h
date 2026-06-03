#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdbool.h>

/* ── public constants ─────────────────────────────────────────────────── */

#define MAX_NUM_LOCKS      16
#define LOCK_ADAPTIVE_SPIN 1024

typedef enum {
    LOCK_SPIN     = 0,   /* busy-wait using atomic exchange              */
    LOCK_BLOCK    = 1,   /* sleep via WaitOnAddress / WakeByAddressAll   */
    LOCK_ADAPTIVE = 2,   /* spin LOCK_ADAPTIVE_SPIN iterations, then sleep */
} lock_type_t;

/* ── internal slot ────────────────────────────────────────────────────── */

typedef struct {
    lock_type_t   type;
    volatile LONG locked;   /* 0 = free, 1 = held; used with Interlocked* */
    DWORD         owner;    /* thread ID of current holder; 0 when free   */
    int           in_use;   /* 1 if this slot is allocated                */
} _lk_slot_t;

/* ── global lock table ────────────────────────────────────────────────── */

static struct {
    CRITICAL_SECTION guard;
    _lk_slot_t       slots[MAX_NUM_LOCKS];
} _lktable;

static INIT_ONCE _lk_init_once = INIT_ONCE_STATIC_INIT;

static BOOL WINAPI _lk_init_fn(PINIT_ONCE io, PVOID p, PVOID *ctx) {
    (void)io; (void)p; (void)ctx;
    InitializeCriticalSection(&_lktable.guard);
    return TRUE;
}

static inline void _lk_ensure_init(void) {
    InitOnceExecuteOnce(&_lk_init_once, _lk_init_fn, NULL, NULL);
}

/*
 * Test helper: reset every slot to unallocated.
 * Compiled only when LOCK_TESTING is defined (i.e. in the test binary).
 */
#ifdef LOCK_TESTING
static inline void _lock_reset_table(void) {
    _lk_ensure_init();
    EnterCriticalSection(&_lktable.guard);
    for (int i = 0; i < MAX_NUM_LOCKS; i++) {
        _lktable.slots[i].locked = 0;
        _lktable.slots[i].owner  = 0;
        _lktable.slots[i].in_use = 0;
    }
    LeaveCriticalSection(&_lktable.guard);
}
#endif

/* ── lock_create ──────────────────────────────────────────────────────── */
/*
 * Allocate a new lock of the given type.
 * Returns a lockid in [0, MAX_NUM_LOCKS) on success, -1 on error.
 */
static inline int lock_create(lock_type_t type) {
    _lk_ensure_init();
    if (type != LOCK_SPIN && type != LOCK_BLOCK && type != LOCK_ADAPTIVE)
        return -1;

    EnterCriticalSection(&_lktable.guard);
    int id = -1;
    for (int i = 0; i < MAX_NUM_LOCKS; i++) {
        if (!_lktable.slots[i].in_use) {
            _lktable.slots[i].type   = type;
            _lktable.slots[i].locked = 0;
            _lktable.slots[i].owner  = 0;
            _lktable.slots[i].in_use = 1;
            id = i;
            break;
        }
    }
    LeaveCriticalSection(&_lktable.guard);
    return id;
}

/* ── lock_take ────────────────────────────────────────────────────────── */
/*
 * Acquire the lock identified by lockid.
 *
 * LOCK_SPIN     – busy-waits with InterlockedExchange, mirrors xv6 xchg loop.
 * LOCK_BLOCK    – blocks via WaitOnAddress (xv6 equivalent: sleep/wakeup).
 * LOCK_ADAPTIVE – spins up to LOCK_ADAPTIVE_SPIN times, then falls back to
 *                 WaitOnAddress (mirrors the xv6 adaptive lock algorithm).
 *
 * Returns 0 on success, -1 on error (invalid id, deleted lock, double-take).
 */
static inline int lock_take(int lockid) {
    _lk_ensure_init();
    if (lockid < 0 || lockid >= MAX_NUM_LOCKS) return -1;

    _lk_slot_t *s = &_lktable.slots[lockid];

    EnterCriticalSection(&_lktable.guard);
    if (!s->in_use) {
        LeaveCriticalSection(&_lktable.guard);
        return -1;
    }
    /* Prevent deadlock: same thread must not take a lock it already holds. */
    if (s->owner == GetCurrentThreadId()) {
        LeaveCriticalSection(&_lktable.guard);
        return -1;
    }
    lock_type_t type = s->type;
    LeaveCriticalSection(&_lktable.guard);

    switch (type) {

    case LOCK_SPIN:
        /* Mirrors xv6: while (xchg(&lk->locked, 1) != 0) {} */
        while (InterlockedExchange(&s->locked, 1) != 0)
            YieldProcessor();   /* reduce bus traffic during spin */
        break;

    case LOCK_BLOCK: {
        /*
         * WaitOnAddress blocks while *Address == *CompareAddress.
         * We wait while locked == 1 (i.e. held by someone else).
         * WakeByAddressAll in lock_release wakes all waiters.
         * Spurious wakeups are safe: the outer CAS loop re-checks.
         *
         * Mirrors xv6: while (lk->locked) sleep(lk, &locktable.lk);
         */
        LONG one = 1;
        while (InterlockedCompareExchange(&s->locked, 1, 0) != 0)
            WaitOnAddress(&s->locked, &one, sizeof(LONG), INFINITE);
        break;
    }

    case LOCK_ADAPTIVE: {
        /*
         * Spin phase: try LOCK_ADAPTIVE_SPIN times before blocking.
         * Mirrors xv6: spin loop with iters < LOCK_ADAPTIVE_SPIN, then sleep.
         */
        bool got = false;
        for (int i = 0; i < LOCK_ADAPTIVE_SPIN && !got; i++) {
            if (InterlockedCompareExchange(&s->locked, 1, 0) == 0)
                got = true;
            else
                YieldProcessor();
        }
        if (!got) {
            LONG one = 1;
            while (InterlockedCompareExchange(&s->locked, 1, 0) != 0)
                WaitOnAddress(&s->locked, &one, sizeof(LONG), INFINITE);
        }
        break;
    }
    }

    s->owner = GetCurrentThreadId();
    return 0;
}

/* ── lock_release ─────────────────────────────────────────────────────── */
/*
 * Release the lock identified by lockid.
 * Only the thread that called lock_take may call lock_release.
 * Returns 0 on success, -1 on error (invalid id, not held by caller).
 */
static inline int lock_release(int lockid) {
    _lk_ensure_init();
    if (lockid < 0 || lockid >= MAX_NUM_LOCKS) return -1;

    _lk_slot_t *s = &_lktable.slots[lockid];

    EnterCriticalSection(&_lktable.guard);
    if (!s->in_use) {
        LeaveCriticalSection(&_lktable.guard);
        return -1;
    }
    if (s->owner != GetCurrentThreadId()) {
        LeaveCriticalSection(&_lktable.guard);
        return -1;
    }
    lock_type_t type = s->type;
    LeaveCriticalSection(&_lktable.guard);

    /* Clear owner before releasing the lock so waiters see a clean state. */
    s->owner = 0;
    InterlockedExchange(&s->locked, 0);

    /* Wake all threads blocked in WaitOnAddress for BLOCK and ADAPTIVE. */
    if (type == LOCK_BLOCK || type == LOCK_ADAPTIVE)
        WakeByAddressAll((PVOID)&s->locked);

    return 0;
}

/* ── lock_delete ──────────────────────────────────────────────────────── */
/*
 * Free the lock slot, making it available for reuse by lock_create.
 * Mirrors xv6 lockclose / lockclose_ref (without reference counting,
 * which was needed for fork/exec but is unnecessary in userspace).
 */
static inline void lock_delete(int lockid) {
    _lk_ensure_init();
    if (lockid < 0 || lockid >= MAX_NUM_LOCKS) return;

    EnterCriticalSection(&_lktable.guard);
    _lktable.slots[lockid].locked = 0;
    _lktable.slots[lockid].owner  = 0;
    _lktable.slots[lockid].in_use = 0;
    LeaveCriticalSection(&_lktable.guard);
}
