# user-locks

A Windows userspace port of the three-lock-type API from the
xv6 synchronization homework.
Follows the same structure as [user-threads](https://github.com/bushidocodes/user-threads).

## Background

The homework extended the xv6 teaching kernel with three user-visible lock types:

| Type | xv6 mechanism | Windows equivalent |
|---|---|---|
| `LOCK_SPIN` | `xchg`-based busy loop | `InterlockedExchange` busy loop |
| `LOCK_BLOCK` | `sleep` / `wakeup` condition vars | `WaitOnAddress` / `WakeByAddressAll` |
| `LOCK_ADAPTIVE` | spin ≤ 1024 times, then `sleep` | spin ≤ 1024 times, then `WaitOnAddress` |

The kernel also needed fork/exec reference counting (`lockdup`, `lockclose_ref`) so
that child processes could safely share lock handles inherited across `fork()`.
That bookkeeping is unnecessary in a single-address-space userspace program, so it
is omitted here.

## API

Declarations live in `lock.h`; definitions and the shared lock table live in
`lock.c`. Link both translation units — no header-only / static-inline design.

### `int lock_create(lock_type_t type)`

Allocate a new lock of the given type.

```c
int lid = lock_create(LOCK_SPIN);     // 0..15 on success, -1 on error
```

Returns a lockid in `[0, MAX_NUM_LOCKS)` on success, `-1` if the type is invalid
or the 16-slot table is full.

### `int lock_take(int lockid)`

Acquire the lock. Blocks (or spins) until the lock is available.

```c
lock_take(lid);
// critical section
lock_release(lid);
```

Returns `0` on success, `-1` on error (invalid id, deleted lock, or double-take
by the same thread — preventing deadlock).

### `int lock_release(int lockid)`

Release the lock. Only the thread that called `lock_take` may release it.

Returns `0` on success, `-1` on error (invalid id, not held by calling thread).

### `int lock_delete(int lockid)`

Free the lock slot for reuse by `lock_create`. The lock must not be held:
release it first, as with `pthread_mutex_destroy`.

Returns `0` on success, `-1` on error (invalid id, slot not in use, or lock
currently held).

## Building

### MSVC (auto-detected on Windows)

```
cmake -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### MinGW / GCC + Ninja

```
cmake -B build -G Ninja ^
  -DCMAKE_C_COMPILER=<path\to\gcc.exe> ^
  -DCMAKE_MAKE_PROGRAM=<path\to\ninja.exe>
cmake --build build
ctest --test-dir build --output-on-failure
```

> **Note:** `WaitOnAddress` and `WakeByAddressAll` require Windows 8 or later.
> The linker must link `synchronization.lib` (MSVC) or `-lsynchronization`
> (MinGW). CMakeLists.txt handles this automatically.

## Tests

33 unit tests using the vendored [Unity](https://github.com/ThrowTheSwitch/Unity)
framework (v2.5.2), organized to mirror the homework's level progression:

| Group | Tests | Mirrors |
|---|---|---|
| `lock_create` | valid ids, invalid type, full table, slot reuse | lvl 0 |
| `lock_take` / `lock_release` | success, invalid id, deleted lock, double-take, release without take | lvl 6 |
| `LOCK_SPIN` concurrent | counter (4 threads × 25 000 iters), race detection | lvl 2 |
| `LOCK_BLOCK` concurrent | counter, race detection | lvl 1 |
| `LOCK_ADAPTIVE` concurrent | counter, race detection | lvl 3 |
| multiple locks | three independent locks held simultaneously | lvl 4 |
| resource exhaustion | 200 sequential create/delete cycles never exhaust 16 slots | — |
| multi-TU correctness | lock ID visible across translation units; cross-TU counter tests for all three lock types | — |
| ownership protocol under contention | take/double-take/release protocol correct under heavy concurrency (regression for issues #1/#4) | — |
| `lock_delete` safety | invalid id, unused slot, delete-while-held refused, delete held by other thread refused (regression for issue #2) | — |

## Architecture comparison

| | xv6 kernel | user-locks |
|---|---|---|
| Lock storage | `locktable` in kernel BSS | `_lktable` static in `lock.c` |
| Table guard | kernel `spinlock` | `CRITICAL_SECTION` |
| Spin primitive | `xchg` inline asm | `InterlockedExchange` |
| Block primitive | `sleep(chan, lk)` / `wakeup(chan)` | `WaitOnAddress` / `WakeByAddressAll` |
| Thread handle | process (xv6 has no user threads in base) | `DWORD` thread ID from `GetCurrentThreadId()` |
| Fork safety | `lockdup` / `lockclose_ref` reference counting | not needed |
| Max locks | 16 per system (`param.h`) | 16 per process (`MAX_NUM_LOCKS`) |
| Parallelism | true (kernel scheduler, multiple CPUs) | true (Windows thread scheduler) |
