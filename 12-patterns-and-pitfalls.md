# 12 — Patterns & Pitfalls

A field guide of ready-to-use atomic patterns (spinlock, one-shot flag,
double-checked init, seqlock) and the traps that bite everyone (false sharing,
volatile confusion, non-atomic composite ops).

---

## 1. Pattern: stop flag / one-shot signal

The simplest correct atomic pattern. A worker polls a flag another thread sets.

```cpp
std::atomic<bool> stop{false};

// worker
while (!stop.load(std::memory_order_acquire)) {
    do_chunk();
}

// controller
stop.store(true, std::memory_order_release);
```

```
   acquire on the load / release on the store so that any data the controller
   set BEFORE requesting stop is visible to the worker after it observes stop.
   (If the flag guards no other data, relaxed would technically do — but acquire/
    release is the safe habit and near-free.)
```

C++20: use `stop.wait(false)` + `stop.notify_all()` to avoid busy-polling, or
better, `std::stop_token`/`std::jthread` for cancellation (chapter 13).

---

## 2. Pattern: spinlock (built from atomic_flag)

A minimal mutual-exclusion lock for *very short* critical sections.

```cpp
#include <atomic>
#include <thread>

class SpinLock {
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;   // C++20: = {} works too
public:
    void lock() {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // optional: reduce contention while spinning
            #if defined(__cpp_lib_atomic_flag_test)
            while (flag_.test(std::memory_order_relaxed))   // C++20 read-only spin
            #endif
                std::this_thread::yield();                   // or _mm_pause()
        }
    }
    void unlock() {
        flag_.clear(std::memory_order_release);
    }
};
```

```
   test_and_set: atomically set true, return OLD value.
     old==false -> we acquired the lock (loop exits).
     old==true  -> someone holds it -> spin.
   acquire on lock / release on unlock = the critical-section handshake.

   The inner "test() then yield" spin ("test-and-test-and-set") avoids hammering
   the cache line with writes while waiting -> less coherence traffic.
```

```
   WARNING: spinlocks burn CPU and can cause priority inversion / livelock. Use
   ONLY for critical sections of a few instructions with low contention. When in
   doubt, use std::mutex (it spins briefly then sleeps — best of both).
```

Runnable: [`examples/ch12_spinlock.cpp`](examples/ch12_spinlock.cpp).

---

## 3. Pattern: double-checked locking / lazy init

Initialize a shared resource once, cheaply, on first use.

```cpp
std::atomic<Widget*> instance{nullptr};
std::mutex init_mutex;

Widget* get() {
    Widget* p = instance.load(std::memory_order_acquire);   // fast path
    if (!p) {                                                // maybe uninit
        std::lock_guard<std::mutex> lock(init_mutex);
        p = instance.load(std::memory_order_relaxed);        // re-check
        if (!p) {
            p = new Widget();                                // construct
            instance.store(p, std::memory_order_release);    // publish
        }
    }
    return p;
}
```

```
   Fast path: one acquire load, no lock, when already initialized.
   Slow path: lock, double-check, construct, release-publish.
   The acquire/release ensures the constructed Widget is fully visible to any
   thread that sees the non-null pointer (no half-constructed object).
```

```
   BUT: prefer the language features that do this for you correctly:
     * static local: "Meyers singleton" is thread-safe since C++11.
         Widget& get() { static Widget w; return w; }   // simplest & correct
     * std::call_once + std::once_flag for non-static one-time init.
   Hand-rolled double-checked locking is a classic source of subtle bugs; use
   it only when the standard tools don't fit.
```

---

## 4. Pattern: seqlock (fast reads of a small struct)

For data written rarely and read often (e.g. a timestamp/config), a *seqlock*
lets readers proceed without locking, retrying if a write intervened.

```cpp
std::atomic<unsigned> seq{0};
Data data;   // small POD

// writer (single writer, or externally serialized):
void write(const Data& d) {
    seq.fetch_add(1, std::memory_order_release);   // odd -> "write in progress"
    data = d;                                       // non-atomic write
    seq.fetch_add(1, std::memory_order_release);   // even -> "done"
}

// reader (lock-free, may retry):
Data read() {
    unsigned s1, s2; Data local;
    do {
        s1 = seq.load(std::memory_order_acquire);
        if (s1 & 1) continue;                       // writer active -> retry
        local = data;                               // tentative read
        s2 = seq.load(std::memory_order_acquire);
    } while (s1 != s2);                             // changed mid-read -> retry
    return local;
}
```

```
   seq even  = stable;  seq odd = write in progress.
   Reader accepts the read only if seq was even AND unchanged across the read.

   Timeline:  seq=2 [reader copies] seq=2  -> accept
              seq=2 [reader copies] seq=4  -> retry (writer ran)
   Great for many-readers/rare-writer of a small value. (Technically the plain
   'data' read races with the writer per the standard; in practice used with
   atomics/relaxed per field or is a known low-level idiom — for strict
   correctness use per-field atomics or std::atomic<Data> if trivially copyable.)
```

---

## 5. Pitfall: false sharing (padding & alignment)

Independent atomics on the same 64-byte cache line ping-pong between cores.

```cpp
#include <new>

// BAD: both counters share a line -> cores fight
struct Bad { std::atomic<long> a{0}; std::atomic<long> b{0}; };

// GOOD: each on its own line
struct Good {
    alignas(std::hardware_destructive_interference_size) std::atomic<long> a{0};
    alignas(std::hardware_destructive_interference_size) std::atomic<long> b{0};
};
```

```
   hardware_destructive_interference_size ~ 64 (or 128) bytes: the minimum
   alignment to keep two objects OFF the same cache line.
   Also: hardware_CONSTRUCTIVE_interference_size = max size to keep things
   TOGETHER on one line (for data you WANT shared).
```

```
   Symptom: a multithreaded counter that gets SLOWER with more threads.
   Diagnosis: perf shows cache-line bouncing (HITM). Fix: pad/align.
```

Runnable: [`examples/ch12_false_sharing.cpp`](examples/ch12_false_sharing.cpp)
(measures the slowdown).

---

## 6. Pitfall: `volatile` is NOT for threading

```
   volatile in C++ means "this memory may change outside the program's control"
   (memory-mapped I/O, signal handlers). It does NOT provide:
     * atomicity
     * cross-thread ordering / happens-before
     * visibility guarantees between threads
   Using 'volatile int' for thread communication is a BUG (works by accident on
   some compilers/x86, breaks on ARM / with optimization).

   Use std::atomic for threads. Use volatile ONLY for hardware registers / signal
   flags (and even then prefer std::atomic<T> with appropriate use).
```

```
   volatile bool stop;   // WRONG for threads (no ordering/atomicity guarantee)
   std::atomic<bool> stop;// RIGHT
```

---

## 7. Pitfall: composite ops aren't atomic

```cpp
std::atomic<int> a{0};
a = a + 1;                 // TWO ops: load then store -> lost updates
if (a > 0) a--;            // check-then-act race
int x = a; int y = a;      // x and y may differ (two loads)
```

```
   ATOMIC means each individual load/store/RMW is indivisible. Combining them is
   NOT. Use a single RMW (++a, fetch_add) or a CAS loop (chapter 10), or a mutex
   for multi-step invariants.
```

---

## 8. Pitfall: forgetting the memory order argument's default is seq_cst

```
   a.store(1);              // seq_cst (safe, maybe slower than you think)
   a.store(1, relaxed);     // explicit, cheaper — but only if correct

   Not a bug per se, but people assume "atomic == fast." A hot seq_cst store on
   ARM is a full barrier. Know your defaults; profile before weakening.
```

---

## 9. Pitfall: mixing atomic and non-atomic access to the same object

```
   Accessing the same object atomically in one place and non-atomically in
   another (without synchronization) is UB. Either it's always atomic, or the
   non-atomic accesses are fully ordered by other synchronization.
   (std::atomic_ref, chapter 13, exists precisely to atomically access an object
   that is otherwise plain — but ALL concurrent accesses must go through the ref.)
```

---

## 10. Quick pattern selector

```
   Need                                  Use
   -----------------------------------   ---------------------------------
   cancel/stop a worker                  atomic<bool> flag / std::stop_token
   count events                          atomic<int> fetch_add(relaxed)
   one-time init                         static local / std::call_once
   short critical section, low contention spinlock (or just std::mutex)
   publish a built object                atomic<T*> store(release)/load(acquire)
   many readers, rare small writes       seqlock (carefully) / atomic<T>
   multi-step invariant                  mutex (simplest & correct)
   avoid contention on separate counters alignas(64) padding (false sharing)
```

---

## 11. Summary

<!--diagram
title: Patterns and pitfalls summary
box[green] Patterns
  text: Stop flag, spinlock (`atomic_flag` `test_and_set`), double-checked init (prefer static local / `call_once`), seqlock
box[red] Pitfalls
  text: false sharing -> `alignas(hardware_destructive_...)`
  text: `volatile` is **NOT** threading -> use `std::atomic`
  text: `a=a+1` / check-then-act are **NOT** atomic -> RMW or CAS or mutex
  text: default order is `seq_cst` (fine, but know the cost)
  text: never mix atomic & non-atomic access to one object
-->

```
 +------------------------------------------------------------------+
 | Patterns: stop flag, spinlock (atomic_flag test_and_set),        |
 |   double-checked init (prefer static local / call_once), seqlock.|
 |                                                                  |
 | Pitfalls:                                                        |
 |   * false sharing -> alignas(hardware_destructive_...)           |
 |   * volatile is NOT threading -> use std::atomic                 |
 |   * a=a+1 / check-then-act are NOT atomic -> RMW or CAS or mutex |
 |   * default order is seq_cst (fine, but know the cost)           |
 |   * never mix atomic & non-atomic access to one object           |
 +------------------------------------------------------------------+
```

Next: [13-cpp20-26-features.md](13-cpp20-26-features.md).
