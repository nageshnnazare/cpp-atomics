# 06 — Acquire/Release In Depth

Acquire/release is the **workhorse** of correct, efficient atomic code. If you
learn one non-default ordering well, learn this. It's how you publish data
between threads without a mutex and without the cost of full `seq_cst`.

Prereq: [04-memory-model.md](04-memory-model.md), [05-memory-orderings.md](05-memory-orderings.md).

---

## 1. The one-sentence summary

```
   A RELEASE store publishes everything the thread did BEFORE it.
   An ACQUIRE load that reads that store's value sees ALL of it.
   Together they form a one-way "message" from producer to consumer.
```

```
   RELEASE = "here's my finished work, sealed."   (producer)
   ACQUIRE = "I've received it; now I can read."   (consumer)
```

---

## 2. Message passing (the canonical pattern)

```cpp
#include <atomic>
#include <thread>
#include <cassert>

struct Message { int a, b, c; };
Message      msg;                 // plain (non-atomic) data
std::atomic<bool> ready{false};

void producer() {
    msg = {1, 2, 3};                               // (1) write payload
    ready.store(true, std::memory_order_release);  // (2) PUBLISH
}

void consumer() {
    while (!ready.load(std::memory_order_acquire)) // (3) wait for publish
        ;                                          //     (spin; see §7)
    assert(msg.a == 1 && msg.b == 2 && msg.c == 3);// (4) safe to read msg
}
```

```
   (1) ─seq─▶ (2) ══sync-with══▶ (3) ─seq─▶ (4)
              release             acquire
   => (1) happens-before (4). msg is published atomically-consistently.
   'msg' is a PLAIN struct, yet there's NO data race, because the release/
   acquire handshake orders it. THIS is the superpower.
```

Note only **one** atomic (`ready`) is needed to safely transfer an entire
non-atomic payload.

---

## 3. The "one-way barrier" nature (why it's cheaper than seq_cst)

Acquire and release are **half fences**. They only stop motion in one direction,
which is exactly enough for producer/consumer and cheaper than a full fence.

```
   RELEASE store: prior ops can't move DOWN past it (but later ops can move UP).

        A;  B;  C;                 A,B,C are "sealed below"... they stay ABOVE
        store(release);   <──────  ...but an unrelated later op D MAY move up.
        D;

   ACQUIRE load: later ops can't move UP past it (but earlier ops can move down).

        E;                         E MAY move down past the acquire.
        load(acquire);    <──────  ...but F,G,H stay BELOW.
        F;  G;  H;
```

```
   This asymmetry is why acquire/release don't prevent the Store-Buffer litmus
   outcome (chapter 02, both reads 0): a store-release followed by a load can
   still reorder (StoreLoad), because release only guards the "down" direction
   and acquire the "up" direction — neither stops store-then-load swapping.
   Only seq_cst (or a seq_cst fence) does. Keep this limitation in mind.
```

---

## 4. Release/acquire on the SAME variable transfers ownership

A pointer published with release/acquire hands off an entire object graph:

```cpp
std::atomic<Node*> head{nullptr};

// Producer builds a node, then publishes:
Node* n = new Node{...};       // fully construct (writes to *n)
n->next = ...;
head.store(n, std::memory_order_release);   // publish pointer

// Consumer:
Node* p = head.load(std::memory_order_acquire);
if (p) use(p->next);           // sees the fully-constructed node, no race
```

```
   The release "seals" all writes to *n BEFORE the store.
   The acquire loading the pointer guarantees those writes are visible.
   -> classic lock-free publish. (This underlies queues/stacks, chapter 11.)
```

---

## 5. Release/acquire is TRANSITIVE (happens-before chains)

Because happens-before is transitive, ordering chains through intermediaries:

```
   A: data=1; flagA.store(release)
   B: while(!flagA.load(acquire)); ... ; flagB.store(release)
   C: while(!flagB.load(acquire)); read data;   // sees data==1

   A ──sync──▶ B ──sync──▶ C   => A happens-before C => 'data' visible in C.
```

This lets you build pipelines where each stage publishes to the next.

---

## 6. RMW with acq_rel: locks and counters that publish

When an operation must both *observe* others' work and *publish* its own, use an
RMW with `acq_rel` (or `acquire` on the acquire side, `release` on the release
side).

### A mutex-like handoff (conceptual)

```cpp
std::atomic<bool> locked{false};

void lock() {
    bool expected = false;
    while (!locked.compare_exchange_weak(expected, true,
               std::memory_order_acquire,      // success: ACQUIRE the lock
               std::memory_order_relaxed)) {   // failure: no ordering needed
        expected = false;
    }
}
void unlock() {
    locked.store(false, std::memory_order_release); // RELEASE the lock
}
```

```
   lock():   acquire -> sees everything the previous holder did before unlock.
   unlock(): release -> publishes this holder's work to the next locker.
   -> the classic critical-section handshake, hand-built. (Real spinlock:
      chapter 12.)
```

### Reference counting

```cpp
// increment: relaxed is fine (just counting, no data published by the inc)
refcount.fetch_add(1, std::memory_order_relaxed);

// decrement: must be a release so that another thread's destructor (which
// acquires) sees all our uses of the object BEFORE we dropped the ref.
if (refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    // last reference -> we now own it exclusively; delete safely.
    delete ptr;
}
```

```
   This is exactly how std::shared_ptr's control block works: relaxed inc,
   acq_rel (or release + acquire fence) dec, so the final deleter sees all
   prior uses. Getting this wrong = use-after-free.
```

Runnable: [`examples/ch06_refcount.cpp`](examples/ch06_refcount.cpp).

---

## 7. Don't hand-spin if you can avoid it (C++20 wait/notify)

The `while(!ready.load(acquire));` busy-wait burns CPU. C++20 adds efficient
`wait`/`notify` on atomics (chapter 13):

```cpp
// consumer
ready.wait(false, std::memory_order_acquire);  // sleeps until value != false
// producer
ready.store(true, std::memory_order_release);
ready.notify_one();                             // wake a waiter
```

```
   .wait(old): blocks while value == old (efficient OS wait, not a spin).
   .notify_one()/.notify_all(): wake waiters after you store.
   Same acquire/release semantics, but no CPU-burning spin.
```

---

## 8. Common mistakes

```
   * Using relaxed on the flag "to be fast" -> breaks publishing -> UB.
     The whole point of the flag is to order OTHER data; relaxed can't.
   * Acquire on the producer / release on the consumer (backwards). Producer
     STORES (release); consumer LOADS (acquire).
   * Publishing with release but the consumer reads the payload with a DIFFERENT
     atomic that wasn't part of the handshake -> no ordering.
   * Assuming acquire/release stops StoreLoad reordering (it doesn't; that needs
     seq_cst). If two threads each store-then-load different vars and you need
     "at least one sees the other," you need seq_cst.
   * Forgetting the payload write must be BEFORE the release store in program
     order (that's what gets published).
```

---

## 9. When to prefer seq_cst over acquire/release

```
   Use seq_cst when correctness depends on a GLOBAL order across MULTIPLE atomic
   variables — e.g. Dekker/Peterson-style mutual exclusion, or "if I don't see
   your flag, you must see mine." Acquire/release is per-handshake and cannot
   provide a single agreed-upon global order. When unsure -> seq_cst.
```

---

## 10. Summary

<!--diagram
title: Acquire-release summary
box[green] Message passing
  text: **RELEASE** store publishes all prior writes; **ACQUIRE** load that reads it sees them all
  text: Lock-free message passing / ownership handoff with **ONE** atomic guarding arbitrary non-atomic data
box[orange] Limitations
  text: They are **ONE-WAY** (half) barriers -> cheaper than `seq_cst`, but do **NOT** stop StoreLoad reordering (SB litmus) — that needs `seq_cst`
box[teal] Usage
  text: Producer `STORE(release)`; consumer `LOAD(acquire)`. Not backwards
  text: RMW that both observes & publishes -> `acq_rel` (locks, refcounts)
  text: Prefer C++20 wait/notify over hand-spinning
-->

```
 +--------------------------------------------------------------------+
 | RELEASE store publishes all prior writes; ACQUIRE load that reads  |
 | it sees them all. -> lock-free message passing / ownership handoff |
 | with ONE atomic guarding arbitrary non-atomic data.                |
 |                                                                    |
 | They are ONE-WAY (half) barriers -> cheaper than seq_cst, but      | 
 | do NOT stop StoreLoad reordering (SB litmus) — that needs seq_cst. |
 |                                                                    |
 | Producer STORE(release); consumer LOAD(acquire). Not backwards.    |
 | RMW that both observes & publishes -> acq_rel (locks, refcounts).  |
 | Prefer C++20 wait/notify over hand-spinning.                       |
 +--------------------------------------------------------------------+
```

Next: [07-relaxed.md](07-relaxed.md).
