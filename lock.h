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

/* ── public API ───────────────────────────────────────────────────────── */

/*
 * Allocate a new lock of the given type.
 * Returns a lockid in [0, MAX_NUM_LOCKS) on success, -1 on error.
 */
int lock_create(lock_type_t type);

/*
 * Acquire the lock identified by lockid.
 * Returns 0 on success, -1 on error (invalid id, deleted lock, double-take).
 */
int lock_take(int lockid);

/*
 * Release the lock identified by lockid.
 * Only the thread that called lock_take may call lock_release.
 * Returns 0 on success, -1 on error (invalid id, not held by caller).
 */
int lock_release(int lockid);

/*
 * Free the lock slot, making it available for reuse by lock_create.
 */
void lock_delete(int lockid);

/*
 * Test helper: reset every slot to unallocated.
 * Only compiled when LOCK_TESTING is defined (i.e. in the test binary).
 */
#ifdef LOCK_TESTING
void _lock_reset_table(void);
#endif
