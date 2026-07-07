# 09 — Fences (`std::atomic_thread_fence`)

A **fence** (a.k.a. memory barrier) is a *standalone* ordering operation — it
constrains reordering without being attached to a particular atomic load/store.
Fences let you separate the "atomic access" from the "ordering," which can be
more efficient and sometimes clearer.

Prereq: chapters 05–08.

---

## 1. Operation-order vs standalone fence

```
   OPERATION ORDER (what we've used so far):
       x.store(1, memory_order_release);     // ordering rides ON the store

   STANDALONE FENCE:
       do_relaxed_writes();
       std::atomic_thread_fence(memory_order_release);  // a barrier by itself
       flag.store(1, memory_order_relaxed);             // relaxed store
```

```
   A fence orders the operations AROUND it in the current thread, and can
   participate in synchronizes-with when paired with another fence or atomic.
   It is NOT tied to one variable -> it can order MANY relaxed ops at once.
```

---

## 2. The fence flavors

```cpp
std::atomic_thread_fence(std::memory_order_acquire);  // acquire fence
std::atomic_thread_fence(std::memory_order_release);  // release fence
std::atomic_thread_fence(std::memory_order_acq_rel);  // both
std::atomic_thread_fence(std::memory_order_seq_cst);  // full + global order
// (relaxed fence is a no-op)
```

```
   RELEASE fence: no read/write BEFORE the fence can move AFTER any store that
                  follows it. Placed BEFORE a relaxed store to "upgrade" it.
   ACQUIRE fence: no read/write AFTER the fence can move BEFORE any load that
                  precedes it. Placed AFTER a relaxed load to "upgrade" it.
```

```
   RELEASE fence layout:            ACQUIRE fence layout:
     ...prior reads/writes...          relaxed load(flag)
     atomic_thread_fence(release)      atomic_thread_fence(acquire)
     flag.store(v, relaxed)            ...subsequent reads/writes...
        (prior ops sealed below)          (subsequent ops sealed above)
```

---

## 3. Fence-based message passing (equivalent to op-order)

```cpp
int data = 0;
std::atomic<bool> ready{false};

// Producer
data = 42;
std::atomic_thread_fence(std::memory_order_release);  // (F1)
ready.store(true, std::memory_order_relaxed);         // relaxed store

// Consumer
while (!ready.load(std::memory_order_relaxed)) {}     // relaxed load
std::atomic_thread_fence(std::memory_order_acquire);  // (F2)
use(data);                                            // safe
```

```
   (F1) release fence + relaxed store   ~ behaves like   store(release)
   relaxed load + (F2) acquire fence    ~ behaves like   load(acquire)

   Synchronization: producer's release fence (before the store the consumer
   reads) synchronizes-with the consumer's acquire fence (after that load).
   => data=42 happens-before use(data). No race.
```

```
   Producer                          Consumer
   data=42                           while(!ready.load(relaxed));
   ─fence(release)─┐                 ─fence(acquire)─┐
   ready.store(relaxed) ~~sync~~▶    use(data)  ◀────┘
```

Runnable: [`examples/ch09_fence.cpp`](examples/ch09_fence.cpp).

---

## 4. Why use a fence instead of an ordered op?

```
   1. AMORTIZE ordering over many relaxed ops:
        write a[0..n] with relaxed stores;
        atomic_thread_fence(release);   // ONE barrier for all of them
        published.store(true, relaxed);
      vs. paying release on every store.

   2. Separate concerns: keep the atomic access relaxed (cheap) and place the
      barrier exactly where the algorithm needs it.

   3. Fence semantics are slightly STRONGER/broader than a single op's order,
      because they order ALL surrounding accesses, not just the one atomic.
```

```
   Trade-off: fences are broader (order everything around them), so they can be
   LESS precise/more costly than a targeted ordered op in simple cases. Prefer
   ordered ops for single-variable handshakes; use fences for batch publishing.
```

---

## 5. The `seq_cst` fence

```cpp
std::atomic_thread_fence(std::memory_order_seq_cst);
```

```
   A seq_cst fence participates in the single global total order S (chapter 08)
   and provides a FULL barrier including StoreLoad. Classic use: fix the
   Store-Buffer litmus with relaxed atomics + fences:

   // Thread A                        // Thread B
   x.store(1, relaxed);               y.store(1, relaxed);
   atomic_thread_fence(seq_cst);      atomic_thread_fence(seq_cst);
   r1 = y.load(relaxed);              r2 = x.load(relaxed);
   -> r1==0 && r2==0 now IMPOSSIBLE (the seq_cst fences impose the global order).
```

---

## 6. `signal_fence` — compiler-only barrier

```cpp
std::atomic_signal_fence(std::memory_order_acquire);
```

```
   atomic_signal_fence: orders operations between a thread and a SIGNAL HANDLER
   running IN THAT SAME THREAD. It's a COMPILER barrier only (no CPU fence),
   because a signal handler runs on the same core.
   Use for: lock-free communication with async-signal handlers. Rare.
   Do NOT use it for cross-thread ordering — it emits no hardware barrier.
```

---

## 7. Fence pairing rules (summary)

```
   To establish happens-before with fences, you generally need:

   A) release fence (thread 1)  +  a relaxed store that a relaxed load reads
                                    +  acquire fence (thread 2)
        -> fence(release) synchronizes-with fence(acquire)

   B) A release fence + an acquire OPERATION also pair; and a release OPERATION
      + an acquire fence pair. (Fences compose with ordered ops.)

   The atomic op in the middle carries the value; the fences carry the ordering.
```

---

## 8. Practical advice

```
   * Default to ORDERED OPERATIONS (store(release)/load(acquire)). They're
     easier to reason about and match the common patterns.
   * Reach for FENCES when: batching many relaxed ops behind one barrier, or
     implementing a primitive where separating access from ordering helps.
   * Use seq_cst fence when you need a full StoreLoad barrier / global order
     with otherwise-relaxed atomics.
   * atomic_signal_fence is ONLY for same-thread signal handlers.
   * Verify with TSan and, ideally, reasoning + weak-hardware testing.
```

---

## 9. Summary

<!--diagram
title: Fences summary
box[blue] Fence
  text: Standalone barrier; orders the ops around it, not tied to one variable
  text: release fence **BEFORE** a (relaxed) store ~ `store(release)`
  text: acquire fence **AFTER** a (relaxed) load ~ `load(acquire)`
box[teal] Synchronizes-with
  text: A release fence synchronizes-with an acquire fence via a relaxed store/load carrying the value
box[green] Variants
  text: `seq_cst` fence = full barrier + global order (fixes SB litmus)
  text: `atomic_signal_fence` = compiler-only, for same-thread signals
box[green] Guidance
  text: Prefer ordered ops; use fences to batch/upgrade relaxed ops
-->

```
 +------------------------------------------------------------------+
 | Fence = standalone barrier; orders the ops around it, not tied   |
 | to one variable.                                                 |
 |   release fence BEFORE a (relaxed) store ~ store(release)        |
 |   acquire fence AFTER  a (relaxed) load  ~ load(acquire)         |
 | A release fence synchronizes-with an acquire fence via a relaxed |
 | store/load carrying the value.                                   |
 |                                                                  |
 | seq_cst fence = full barrier + global order (fixes SB litmus).   |
 | atomic_signal_fence = compiler-only, for same-thread signals.    |
 | Prefer ordered ops; use fences to batch/upgrade relaxed ops.     |
 +------------------------------------------------------------------+
```

Next: [10-compare-exchange.md](10-compare-exchange.md).
