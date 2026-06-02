#include "lock.h"
#include "unity/unity.h"
#include <windows.h>

/* ── test parameters ──────────────────────────────────────────────────── */

#define N_THREADS  4
#define N_ITERS    25000   /* per thread; total = N_THREADS * N_ITERS */
#define RACE_N     6       /* threads in race-detection tests           */

/* ── shared globals for concurrent tests ─────────────────────────────── */

static volatile int  g_counter;
static volatile DWORD g_race_val;
static volatile LONG  g_race_ok;

/* ── setUp / tearDown ─────────────────────────────────────────────────── */

void setUp(void) {
    _lock_reset_table();
    g_counter  = 0;
    g_race_val = 0;
    g_race_ok  = 0;
}

void tearDown(void) {}

/* ── thread helpers ───────────────────────────────────────────────────── */

typedef struct { int lockid; int iters; } count_arg_t;

/* Increments g_counter inside the lock N_ITERS times. */
DWORD WINAPI count_thread(LPVOID p) {
    count_arg_t *a = (count_arg_t *)p;
    for (int i = 0; i < a->iters; i++) {
        lock_take(a->lockid);
        g_counter++;
        lock_release(a->lockid);
    }
    return 0;
}

/*
 * Writes its own thread ID to g_race_val, sleeps 5 ms while holding the
 * lock, then verifies no other thread overwrote the value.
 * If the lock is working correctly, g_race_ok == RACE_N at the end.
 */
DWORD WINAPI race_thread(LPVOID p) {
    int lockid = *(int *)p;
    lock_take(lockid);
    DWORD my_id = GetCurrentThreadId();
    g_race_val = my_id;
    Sleep(5);
    if (g_race_val == my_id)
        InterlockedIncrement(&g_race_ok);
    lock_release(lockid);
    return 0;
}

/* ── test helpers ─────────────────────────────────────────────────────── */

static void run_counter_test(lock_type_t type) {
    int lockid = lock_create(type);
    TEST_ASSERT_NOT_EQUAL(-1, lockid);

    count_arg_t args[N_THREADS];
    HANDLE      handles[N_THREADS];
    for (int i = 0; i < N_THREADS; i++) {
        args[i]    = (count_arg_t){lockid, N_ITERS};
        handles[i] = CreateThread(NULL, 0, count_thread, &args[i], 0, NULL);
        TEST_ASSERT_NOT_NULL(handles[i]);
    }
    WaitForMultipleObjects(N_THREADS, handles, TRUE, INFINITE);
    for (int i = 0; i < N_THREADS; i++) CloseHandle(handles[i]);

    TEST_ASSERT_EQUAL(N_THREADS * N_ITERS, g_counter);
    lock_delete(lockid);
}

static void run_race_test(lock_type_t type) {
    int lockid = lock_create(type);
    TEST_ASSERT_NOT_EQUAL(-1, lockid);

    HANDLE handles[RACE_N];
    for (int i = 0; i < RACE_N; i++)
        handles[i] = CreateThread(NULL, 0, race_thread, &lockid, 0, NULL);
    WaitForMultipleObjects(RACE_N, handles, TRUE, INFINITE);
    for (int i = 0; i < RACE_N; i++) CloseHandle(handles[i]);

    TEST_ASSERT_EQUAL(RACE_N, (int)g_race_ok);
    lock_delete(lockid);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Group 1 – lock_create
 * ══════════════════════════════════════════════════════════════════════ */

void test_create_spin_returns_valid_id(void) {
    int id = lock_create(LOCK_SPIN);
    TEST_ASSERT_GREATER_THAN(-1, id);
    TEST_ASSERT_TRUE(id < MAX_NUM_LOCKS);
    lock_delete(id);
}

void test_create_block_returns_valid_id(void) {
    int id = lock_create(LOCK_BLOCK);
    TEST_ASSERT_GREATER_THAN(-1, id);
    lock_delete(id);
}

void test_create_adaptive_returns_valid_id(void) {
    int id = lock_create(LOCK_ADAPTIVE);
    TEST_ASSERT_GREATER_THAN(-1, id);
    lock_delete(id);
}

void test_create_invalid_type_fails(void) {
    TEST_ASSERT_EQUAL(-1, lock_create((lock_type_t)99));
}

/* Fill the entire table then verify the next allocation fails. */
void test_create_when_table_full_fails(void) {
    int ids[MAX_NUM_LOCKS];
    for (int i = 0; i < MAX_NUM_LOCKS; i++) {
        ids[i] = lock_create(LOCK_SPIN);
        TEST_ASSERT_NOT_EQUAL(-1, ids[i]);
    }
    TEST_ASSERT_EQUAL(-1, lock_create(LOCK_SPIN));
    for (int i = 0; i < MAX_NUM_LOCKS; i++)
        lock_delete(ids[i]);
}

/* Slot freed by lock_delete must be immediately reusable. */
void test_slot_reuse_after_delete(void) {
    int id1 = lock_create(LOCK_SPIN);
    lock_delete(id1);
    int id2 = lock_create(LOCK_SPIN);
    TEST_ASSERT_NOT_EQUAL(-1, id2);
    lock_delete(id2);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Group 2 – lock_take / lock_release (single-threaded)
 * ══════════════════════════════════════════════════════════════════════ */

void test_take_and_release_spin(void) {
    int id = lock_create(LOCK_SPIN);
    TEST_ASSERT_EQUAL(0, lock_take(id));
    TEST_ASSERT_EQUAL(0, lock_release(id));
    lock_delete(id);
}

void test_take_and_release_block(void) {
    int id = lock_create(LOCK_BLOCK);
    TEST_ASSERT_EQUAL(0, lock_take(id));
    TEST_ASSERT_EQUAL(0, lock_release(id));
    lock_delete(id);
}

void test_take_and_release_adaptive(void) {
    int id = lock_create(LOCK_ADAPTIVE);
    TEST_ASSERT_EQUAL(0, lock_take(id));
    TEST_ASSERT_EQUAL(0, lock_release(id));
    lock_delete(id);
}

void test_take_invalid_id_fails(void) {
    TEST_ASSERT_EQUAL(-1, lock_take(-1));
    TEST_ASSERT_EQUAL(-1, lock_take(MAX_NUM_LOCKS));
    TEST_ASSERT_EQUAL(-1, lock_take(9999));
}

void test_release_invalid_id_fails(void) {
    TEST_ASSERT_EQUAL(-1, lock_release(-1));
    TEST_ASSERT_EQUAL(-1, lock_release(MAX_NUM_LOCKS));
}

void test_take_deleted_lock_fails(void) {
    int id = lock_create(LOCK_SPIN);
    lock_delete(id);
    TEST_ASSERT_EQUAL(-1, lock_take(id));
}

/* Taking a lock twice on the same thread must fail, not deadlock. */
void test_double_take_same_thread_fails(void) {
    int id = lock_create(LOCK_SPIN);
    TEST_ASSERT_EQUAL(0,  lock_take(id));
    TEST_ASSERT_EQUAL(-1, lock_take(id));
    lock_release(id);
    lock_delete(id);
}

void test_release_without_take_fails(void) {
    int id = lock_create(LOCK_SPIN);
    TEST_ASSERT_EQUAL(-1, lock_release(id));
    lock_delete(id);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Group 3 – LOCK_SPIN concurrent correctness
 *
 * Mirrors lock_lvl2.c: N threads each increment a shared counter
 * M times; the final value must equal N*M with no lost updates.
 * ══════════════════════════════════════════════════════════════════════ */

void test_spin_lock_protects_counter(void) {
    run_counter_test(LOCK_SPIN);
}

/* Mirrors lock_lvl7.c: write own ID, hold lock, verify no overwrite. */
void test_spin_lock_prevents_race(void) {
    run_race_test(LOCK_SPIN);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Group 4 – LOCK_BLOCK concurrent correctness
 *
 * Mirrors lock_lvl1.c: blocking lock protects shared state across
 * multiple truly-parallel Windows threads.
 * ══════════════════════════════════════════════════════════════════════ */

void test_block_lock_protects_counter(void) {
    run_counter_test(LOCK_BLOCK);
}

void test_block_lock_prevents_race(void) {
    run_race_test(LOCK_BLOCK);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Group 5 – LOCK_ADAPTIVE concurrent correctness
 *
 * Mirrors lock_lvl3.c: hybrid lock spins then sleeps under contention.
 * ══════════════════════════════════════════════════════════════════════ */

void test_adaptive_lock_protects_counter(void) {
    run_counter_test(LOCK_ADAPTIVE);
}

void test_adaptive_lock_prevents_race(void) {
    run_race_test(LOCK_ADAPTIVE);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Group 6 – multiple independent locks
 *
 * Mirrors lock_lvl4.c: three separate locks can all be held
 * simultaneously by the same thread.
 * ══════════════════════════════════════════════════════════════════════ */

void test_multiple_locks_are_independent(void) {
    int a = lock_create(LOCK_SPIN);
    int b = lock_create(LOCK_BLOCK);
    int c = lock_create(LOCK_ADAPTIVE);

    TEST_ASSERT_EQUAL(0, lock_take(a));
    TEST_ASSERT_EQUAL(0, lock_take(b));
    TEST_ASSERT_EQUAL(0, lock_take(c));

    TEST_ASSERT_EQUAL(0, lock_release(c));
    TEST_ASSERT_EQUAL(0, lock_release(b));
    TEST_ASSERT_EQUAL(0, lock_release(a));

    lock_delete(a);
    lock_delete(b);
    lock_delete(c);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Group 7 – resource exhaustion
 *
 * 200 sequential create/delete cycles must never exhaust the 16-slot
 * table, confirming that lock_delete properly recycles slots.
 * ══════════════════════════════════════════════════════════════════════ */

void test_200_sequential_create_delete_cycles(void) {
    for (int i = 0; i < 200; i++) {
        int id = lock_create(LOCK_SPIN);
        TEST_ASSERT_NOT_EQUAL(-1, id);
        lock_delete(id);
    }
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void) {
    UNITY_BEGIN();

    /* lock_create */
    RUN_TEST(test_create_spin_returns_valid_id);
    RUN_TEST(test_create_block_returns_valid_id);
    RUN_TEST(test_create_adaptive_returns_valid_id);
    RUN_TEST(test_create_invalid_type_fails);
    RUN_TEST(test_create_when_table_full_fails);
    RUN_TEST(test_slot_reuse_after_delete);

    /* lock_take / lock_release */
    RUN_TEST(test_take_and_release_spin);
    RUN_TEST(test_take_and_release_block);
    RUN_TEST(test_take_and_release_adaptive);
    RUN_TEST(test_take_invalid_id_fails);
    RUN_TEST(test_release_invalid_id_fails);
    RUN_TEST(test_take_deleted_lock_fails);
    RUN_TEST(test_double_take_same_thread_fails);
    RUN_TEST(test_release_without_take_fails);

    /* LOCK_SPIN */
    RUN_TEST(test_spin_lock_protects_counter);
    RUN_TEST(test_spin_lock_prevents_race);

    /* LOCK_BLOCK */
    RUN_TEST(test_block_lock_protects_counter);
    RUN_TEST(test_block_lock_prevents_race);

    /* LOCK_ADAPTIVE */
    RUN_TEST(test_adaptive_lock_protects_counter);
    RUN_TEST(test_adaptive_lock_prevents_race);

    /* multiple locks */
    RUN_TEST(test_multiple_locks_are_independent);

    /* resource exhaustion */
    RUN_TEST(test_200_sequential_create_delete_cycles);

    return UNITY_END();
}
