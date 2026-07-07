# 14 — Atomics & Memory Ordering Cheatsheet

Quick recall. Read the chapters first; this is the reference card.

---

## Declaring & core ops

```cpp
#include <atomic>
std::atomic<int> a{0};          // brace-init; NOT copyable/movable

int v = a.load();               // read           (seq_cst default)
a.store(1);                     // write
int old = a.exchange(2);        // swap, return old
a.fetch_add(5); a.fetch_sub(1); // RMW, return OLD value
a.fetch_and(x); a.fetch_or(x); a.fetch_xor(x);
++a; a++; --a; a--; a += 3;     // atomic RMW (++a returns new; a++ old)
// a = a + 1;  // BUG: two ops, not atomic. Use ++a / fetch_add.

int expected = 2;
a.compare_exchange_weak(expected, 9,   success_order, failure_order);
a.compare_exchange_strong(expected, 9);

constexpr bool lf = std::atomic<int>::is_always_lock_free;   // compile-time
```

## The six memory orders

```
  relaxed : atomicity + per-object coherence only. No cross-var ordering.
  consume : DON'T USE. Compilers promote to acquire.
  acquire : LOAD/RMW. Nothing after moves before; pulls in releaser's writes.
  release : STORE/RMW. Nothing before moves after; publishes prior writes.
  acq_rel : RMW. acquire + release.
  seq_cst : acquire+release + single GLOBAL total order. DEFAULT. Safest.

  loads -> acquire,  stores -> release,  RMW -> acq_rel.  Unsure -> seq_cst.
```

## Which order on which op

```
  load  : relaxed / acquire / seq_cst                (NOT release/acq_rel)
  store : relaxed / release / seq_cst                (NOT acquire/acq_rel)
  RMW   : relaxed / acquire / release / acq_rel / seq_cst
  CAS failure order: <= success, and never release/acq_rel (it's a load)
```

## The publishing handshake (the #1 pattern)

```cpp
// producer                          // consumer
data = ...;                          while(!ready.load(acquire)) {}
ready.store(true, release);          use(data);   // safe: happens-before
// one atomic (ready) safely publishes arbitrary non-atomic 'data'.
```

## Happens-before sources

```
  * mutex unlock -> lock (same mutex)
  * store-release -> load-acquire (reading that value)
  * thread ctor: parent's prior work -> new thread start
  * thread.join(): thread's work -> after join()
  * release fence + relaxed store/load + acquire fence
```

## Compare-exchange

```
  weak   : may fail spuriously -> use IN A LOOP (cheaper).
  strong : no spurious fail    -> use for a SINGLE attempt.

  CAS loop:
    T cur = a.load(relaxed);
    while(!a.compare_exchange_weak(cur, f(cur), release, relaxed)) {}
    // 'cur' auto-refreshed on failure.

  ABA: value A->B->A makes CAS wrongly succeed. Fix: tagged ptr / hazard ptr / RCU.
```

## Fences

```cpp
std::atomic_thread_fence(std::memory_order_release);  // before a relaxed store
std::atomic_thread_fence(std::memory_order_acquire);  // after  a relaxed load
std::atomic_thread_fence(std::memory_order_seq_cst);  // full barrier + global order
std::atomic_signal_fence(order);   // compiler-only; same-thread signal handlers
```

## Patterns

```cpp
// stop flag
std::atomic<bool> stop{false};
while(!stop.load(acquire)) work();          stop.store(true, release);

// spinlock
std::atomic_flag f{};
void lock(){ while(f.test_and_set(acquire)) /*spin: while(f.test())*/; }
void unlock(){ f.clear(release); }

// lazy init -> prefer:
Widget& get(){ static Widget w; return w; }  // thread-safe since C++11
// or std::call_once(once_flag, init);

// counter (relaxed OK)
cnt.fetch_add(1, relaxed);

// publish whole object (read-mostly)
std::atomic<std::shared_ptr<Cfg>> cfg;   // load()/store(), auto-reclaimed (C++20)
```

## C++20/23/26 features

```
  C++20: a.wait(old)/notify_one()/notify_all()   (block instead of spin)
         std::atomic_ref<T>(obj)                  (atomic view over plain object)
         std::atomic<std::shared_ptr<T>>          (easy safe reclamation)
         atomic<float/double> fetch_add
         atomic_flag test()/wait()
         std::latch, std::barrier, std::counting_semaphore
         std::jthread, std::stop_token            (cancellation)
  C++23: <stdatomic.h> interop, constexpr/wording polish
  C++26: std::hazard_pointer, std::rcu_*          (standard reclamation)
         std::execution (senders/receivers)
```

## Pitfalls checklist

```
  [ ] volatile is NOT for threads -> use std::atomic
  [ ] a=a+1 / if(a)... are NOT atomic composites -> RMW / CAS / mutex
  [ ] relaxed flag guarding data -> UB (need release/acquire)
  [ ] false sharing -> alignas(hardware_destructive_interference_size)
  [ ] never mix atomic + plain access to the same object
  [ ] acquire/release do NOT stop StoreLoad (SB litmus) -> seq_cst for that
  [ ] lock-free pop needs safe reclamation (hazard ptr / RCU / atomic shared_ptr)
  [ ] test on ARM + ThreadSanitizer; x86 hides ordering bugs
```

## Decision guide

```
  Shared mutable state?
    -> read-only after publish?          plain data + one release/acquire publish
    -> just a counter/flag value?         atomic + relaxed (counter) / acquire (flag)
    -> multi-step invariant?              std::mutex   (default, simplest)
    -> read-mostly whole-object swap?     std::atomic<std::shared_ptr<T>>
    -> element-wise atomic on an array?   std::atomic_ref<T>
    -> coordinate N threads?              latch / barrier / semaphore
    -> truly need lock-free structure?    library now / std::hazard_pointer (C++26)
    -> not sure?                          std::mutex + seq_cst atomics
```

## Golden rules

```
  1. Prefer a mutex. Atomics are the exception, not the rule.
  2. Default to seq_cst. Weaken only with proof + profiling.
  3. relaxed is for value-only counters, not for publishing data.
  4. Atomicity != algorithm correctness (check-then-act still races).
  5. Test with ThreadSanitizer and on weakly-ordered hardware.
```

---

Back to the [README](README.md).
