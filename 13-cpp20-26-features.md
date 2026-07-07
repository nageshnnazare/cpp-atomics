# 13 — C++20/23/26 Atomic Features

Modern C++ added major atomic capabilities. C++20 was huge (`wait`/`notify`,
`atomic_ref`, `atomic<shared_ptr>`, `atomic<floating>`). C++23/26 continue with
reclamation primitives and more. This chapter surveys them with examples.

---

## 1. C++20: `wait` / `notify` — efficient blocking on atomics

Before C++20, waiting for an atomic to change meant a busy-spin. C++20 adds
futex-style blocking directly on atomics.

```cpp
std::atomic<int> state{0};

// Waiter: blocks (OS sleep) while state == 0, wakes when it changes.
state.wait(0, std::memory_order_acquire);   // "wait while value == 0"

// Notifier:
state.store(1, std::memory_order_release);
state.notify_one();    // wake one waiter
// state.notify_all(); // wake all waiters
```

```
   .wait(old, order): if value == old, block until notified AND value != old.
                      (spurious wakeups handled internally; it re-checks.)
   .notify_one() / .notify_all(): wake waiter(s). Call AFTER the store.

   Beats spinning: the OS parks the thread (no CPU burn) and wakes it on notify.
```

```
   Producer                         Consumer
   ...produce...                    ready.wait(false, acquire);  // sleeps
   ready.store(true, release);      // (wakes here)
   ready.notify_one();      ─────▶  use(data);
```

Works on `atomic<T>` and `atomic_flag` (`flag.wait(false)`, `flag.notify_one()`).

Runnable: [`examples/ch13_wait_notify.cpp`](examples/ch13_wait_notify.cpp).

---

## 2. C++20: `std::atomic_ref<T>` — atomic access to a NON-atomic object

Sometimes you have a plain object (or an array element) and want *some* accesses
to be atomic without changing its type. `atomic_ref` provides atomic operations
over existing storage.

```cpp
#include <atomic>

void parallel_add(int* data, std::size_t n, int idx_shared) {
    // Suppose many threads increment data[idx_shared]:
    std::atomic_ref<int> ref(data[idx_shared]);   // atomic view over an int
    ref.fetch_add(1, std::memory_order_relaxed);  // atomic RMW on plain int
}
```

```
   atomic_ref<T> wraps an EXISTING T object and gives it load/store/RMW/CAS.
   Requirements & rules:
     * The referenced object must be suitably ALIGNED (check
       atomic_ref<T>::required_alignment) to be lock-free.
     * While ANY atomic_ref to an object exists, ALL accesses to that object
       (by any thread) must go through atomic_ref — no plain access. Otherwise UB.
     * Great for: atomically updating elements of a big array/struct you can't
       (or don't want to) declare as atomic<T>[].
```

```
   std::atomic<int> arr[N];   // every element always atomic (heavier type)
        vs.
   int arr[N]; ... std::atomic_ref<int>(arr[i]).fetch_add(1);  // opt-in atomicity
```

Runnable: [`examples/ch13_atomic_ref.cpp`](examples/ch13_atomic_ref.cpp).

---

## 3. C++20: `std::atomic<std::shared_ptr<T>>` and `weak_ptr`

The old free functions `std::atomic_load(&sp)` / `atomic_store(&sp, ...)` on
`shared_ptr` were clunky and error-prone. C++20 provides a proper
`atomic<shared_ptr<T>>` specialization — the easiest correct path to lock-free-ish
shared data structures with automatic reclamation.

```cpp
#include <atomic>
#include <memory>

std::atomic<std::shared_ptr<Config>> current_config;

// Reader (never sees a torn/half-freed object):
void use_config() {
    std::shared_ptr<Config> cfg = current_config.load();   // atomic, refcounted
    if (cfg) cfg->apply();                                  // safe to use
}

// Writer (atomic swap; old config freed when last reader drops it):
void update_config(std::shared_ptr<Config> new_cfg) {
    current_config.store(std::move(new_cfg));
    // or: current_config.compare_exchange_strong(expected, desired);
}
```

```
   This solves the RECLAMATION problem (chapter 11) for you: the shared_ptr
   refcount ensures the old Config isn't freed until every reader that loaded it
   has released it. No hazard pointers, no ABA on the object.

   Caveat: atomic<shared_ptr> may NOT be lock-free (often uses a lock/split-ref
   internally). Check is_lock_free(). It's correct and convenient, not always
   the fastest. Perfect for read-mostly config/state swapping.
```

This is the recommended pattern for "swap out a whole immutable data structure
atomically" (copy-on-write config, routing tables, etc.).

---

## 4. C++20: atomic arithmetic on floating point & better `atomic_flag`

```cpp
std::atomic<double> sum{0.0};
sum.fetch_add(1.5, std::memory_order_relaxed);   // C++20: atomic float add

std::atomic_flag f{};        // C++20: default-initialized to clear (false)
bool v = f.test();           // C++20: read WITHOUT setting (new)
f.wait(false);               // C++20: wait/notify on atomic_flag
f.notify_one();
```

```
   Pre-C++20: atomic_flag had ONLY test_and_set / clear (no way to just read).
   C++20 adds test(), wait(), notify_* -> atomic_flag is now a usable primitive.
```

---

## 5. C++20: `std::atomic<T>::is_always_lock_free` & `<version>` macros

Already covered in chapter 03. Plus feature-test macros:

```cpp
#include <version>
#if defined(__cpp_lib_atomic_wait)         // wait/notify available
#endif
#if defined(__cpp_lib_atomic_ref)          // atomic_ref available
#endif
#if defined(__cpp_lib_atomic_shared_ptr)   // atomic<shared_ptr> available
#endif
```

---

## 6. C++20 (adjacent): `std::latch`, `std::barrier`, `std::counting_semaphore`

Not atomics per se, but atomic-based higher-level sync you should prefer over
hand-rolled atomic coordination:

```cpp
#include <latch>
#include <barrier>
#include <semaphore>

std::latch start_gate{1};       // one-shot countdown; threads wait() then it opens
std::barrier sync_point{N};     // reusable: N threads arrive_and_wait() each phase
std::counting_semaphore<> sem{3}; // permits: acquire()/release()
```

```
   latch:     count down to zero once, then all waiters proceed. (start signal)
   barrier:   phased rendezvous for N threads, reusable across rounds.
   semaphore: N permits; acquire blocks if none left, release adds one.
   -> Reach for THESE before building coordination out of raw atomics.
```

---

## 7. C++23: small atomic refinements

```
   * std::atomic / atomic_ref usable in more constexpr-friendly contexts.
   * <stdatomic.h> (C++23): the C atomics header is now provided in C++ for
     interop; std::atomic<T> and _Atomic interoperate more cleanly.
   * Improved specification/wording; atomic_ref for more types.
   * std::atomic_ref support and lock-free guarantees clarified.
```

C++23 is mostly polish for atomics; the big library concurrency addition was
elsewhere (e.g. `std::jthread`/`stop_token` came in C++20, `std::expected`, etc.).

---

## 8. C++26: standard safe memory reclamation & more

C++26 tackles the hardest lock-free problem from chapter 11 by standardizing
**reclamation primitives**, plus other concurrency items.

```
   * std::hazard_pointer / std::hazard_pointer_obj_base  (P2530)
       -> standard hazard pointers for safe lock-free reclamation.
   * std::rcu_domain, rcu_synchronize, rcu_retire        (P2545)
       -> standard Read-Copy-Update.
   * Refinements to the memory model wording (ongoing) and to atomic_ref.
   * std::execution (senders/receivers, P2300): a whole async framework built on
     these foundations.
   * Continued constexpr-ification and interop.
```

### Hazard pointers (conceptual C++26 usage)

```cpp
// Conceptual — names/spelling may shift before publication.
struct Node : std::hazard_pointer_obj_base<Node> { /* ... */ };

// Reader protects a node while using it:
std::hazard_pointer h = std::make_hazard_pointer();
Node* p = h.protect(head);        // publishes p as a hazard; safe to deref p
// ... use p ...

// Reclaimer retires instead of delete:
old->retire();                     // freed only when no hazard protects it
```

```
   This bakes the chapter-11 hazard-pointer scheme into the standard library, so
   you get correct reclamation without hand-writing the scanning logic. Big deal
   for anyone doing serious lock-free work.
```

### RCU (conceptual)

```cpp
// Readers are cheap; writers copy-update then defer reclamation.
std::rcu_domain& dom = std::rcu_default_domain();
// reader:  rcu_read region; access shared pointer
// writer:  install new version; rcu_retire(old); rcu_synchronize() before free
```

Availability: **experimental / not widely shipped yet in 2026.** Treat as the
future direction; check your standard library.

---

## 9. What to actually use today (2026)

```
   Everyday:
     * std::atomic<int/bool/ptr> with seq_cst or acquire/release (chapters 5-6)
     * std::atomic<bool> + wait/notify, or std::stop_token/jthread, for signaling
     * std::mutex / lock_guard for anything non-trivial
     * std::latch / barrier / semaphore for coordination
   Read-mostly shared object swaps:
     * std::atomic<std::shared_ptr<T>>  (correct, easy; check lock-free)
   Element-wise atomics over existing arrays/structs:
     * std::atomic_ref<T>
   Serious lock-free with reclamation:
     * a vetted library now; std::hazard_pointer / std::rcu when your stdlib ships it
```

---

## 10. Summary

<!--diagram
title: C++20–26 features summary
box[blue] C++20
  text: wait/notify (blocking, no spin), `atomic_ref` (atomic view over plain objects), `atomic<shared_ptr>` (solves reclamation!)
  text: `atomic<float>` arithmetic, `atomic_flag` `test()`/`wait()`
  text: + latch/barrier/semaphore for coordination
box[teal] C++23
  text: `<stdatomic.h>` interop, constexpr/wording polish
box[purple] C++26
  text: `std::hazard_pointer` & `std::rcu` (standard safe reclamation), `std::execution`, more. (Experimental in 2026.)
box[green] Practical
  text: Prefer wait/notify over spin; `atomic<shared_ptr>` for read-mostly swaps; a library (or C++26 hazard/rcu) for lock-free
-->

```
 +------------------------------------------------------------------+
 | C++20: wait/notify (blocking, no spin), atomic_ref (atomic view  |
 |   over plain objects), atomic<shared_ptr> (solves reclamation!),  |
 |   atomic<float> arithmetic, atomic_flag test()/wait().           |
 |   + latch/barrier/semaphore for coordination.                    |
 | C++23: <stdatomic.h> interop, constexpr/wording polish.          |
 | C++26: std::hazard_pointer & std::rcu (standard safe reclamation),|
 |   std::execution, more. (Experimental in 2026.)                 |
 |                                                                  |
 | Practical: prefer wait/notify over spin; atomic<shared_ptr> for  |
 | read-mostly swaps; a library (or C++26 hazard/rcu) for lock-free.|
 +------------------------------------------------------------------+
```

Next: [14-cheatsheet.md](14-cheatsheet.md).
