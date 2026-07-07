# 04 — The C++ Memory Model

This is the conceptual heart of the guide. The "memory model" is the set of rules
that define **when a write by one thread is guaranteed to be visible to a read by
another**. Every memory order is just a knob on this model. Take your time here.

---

## 1. The vocabulary you must internalize

Four relations, from local to global:

```
   1. SEQUENCED-BEFORE   : ordering WITHIN a single thread (program order-ish).
   2. SYNCHRONIZES-WITH  : a release operation pairs with an acquire operation
                           across threads (the cross-thread "handshake").
   3. HAPPENS-BEFORE     : the transitive combination -> the master relation.
                           If A happens-before B, A's effects are visible to B.
   4. MODIFICATION ORDER : for each atomic object, a single total order of all
                           writes to it (all threads agree on this per-object).
```

```
      sequenced-before (in-thread)
              +  synchronizes-with (cross-thread handshake)
              =  HAPPENS-BEFORE  (what's visible to whom)
```

If there is **no** happens-before between a write and a read of the same
non-atomic location, and at least one is a write -> **data race -> UB**
(chapter 01). So happens-before is literally what keeps you out of UB.

---

## 2. Sequenced-before (single thread)

Within one thread, statements are (mostly) ordered by program order. This is the
ordering you already intuit.

```cpp
int a = 1;      // (1)
int b = 2;      // (2)   (1) is sequenced-before (2)
int c = a + b;  // (3)   (2) is sequenced-before (3)
```

```
   (1) --seq-before--> (2) --seq-before--> (3)
   The compiler may reorder as long as the OBSERVABLE single-thread result is
   the same ("as-if" rule). Other threads can see the reordered version, though!
```

---

## 3. Synchronizes-with (the cross-thread handshake)

The bridge between threads. The canonical case: a **release store** synchronizes
with an **acquire load** that reads the stored value.

```
   Thread A                              Thread B
   -----------------------------         --------------------------------
   data = 42;                 (a)
   flag.store(1, release);    (b)  ~~~~> while(flag.load(acquire)!=1){} (c)
                                          read data;                    (d)

   (b) SYNCHRONIZES-WITH (c)   [because (c) reads the value (b) stored, and
                                (b) is release, (c) is acquire]
```

```
   The handshake requires ALL of:
     * (b) is a store with release (or stronger) ordering
     * (c) is a load  with acquire (or stronger) ordering
     * (c) actually READS the value written by (b) (or later in mod order via
       a release sequence)
```

---

## 4. Happens-before = the payoff

Combine sequenced-before + synchronizes-with transitively:

```
   (a) seq-before (b)   [thread A: write data, then release]
   (b) sync-with  (c)   [the handshake]
   (c) seq-before (d)   [thread B: acquire, then read data]
   ------------------------------------------------------
   => (a) HAPPENS-BEFORE (d)
   => the write 'data = 42' is guaranteed visible to 'read data'
   => NO data race on 'data', even though 'data' is a PLAIN int.
```

```
   A:  data=42 ──seq──▶ store-release(flag)
                              │ synchronizes-with
                              ▼
   B:                  load-acquire(flag) ──seq──▶ read data  (sees 42) ✓
```

This is the mechanism that lets one thread **publish** a whole data structure to
another by releasing a single flag/pointer. The acquire thread, upon seeing the
flag, is guaranteed to see everything the releaser did *before* the release.
This is *the* everyday use of atomics (chapter 06).

---

## 5. The "release sequence" refinement (why RMWs chain)

The acquiring thread doesn't have to read the *exact* value the releaser wrote —
it may read a later value produced by a chain of **read-modify-write** operations
on the same atomic. This chain is the *release sequence*, and it preserves the
handshake. This is what makes reference counting and queues work.

```
   A: store-release(x, 1)
   B: fetch_add(x, ...)     (RMW — part of the release sequence headed by A)
   C: load-acquire(x)       reads C's value -> STILL synchronizes-with A's store
   -> A's prior writes are visible to C.
```

You rarely reason about this directly, but it's why acquire/release composes
through atomic counters.

---

## 6. Modification order & coherence

For **each individual atomic object**, all threads agree on a single total order
of writes to it — its *modification order*. Four coherence guarantees follow
(they hold for **any** memory order, even relaxed):

```
   Given a single atomic object M:
     write-write coherence : all threads see writes to M in the same order.
     read-read   coherence : a thread that reads a newer value of M never later
                             reads an older value of M.
     read-write / write-read coherence: similar consistency for the reader.

   -> A SINGLE atomic variable is always "coherent" and never goes backwards
      for a given thread. Relaxed is enough for THAT ONE variable's own value.
```

The catch: coherence is **per-object**. It says nothing about ordering between
*different* variables. That cross-variable ordering is exactly what
acquire/release/seq_cst provide.

```
   Coherence: relaxed gives you a sane view of ONE atomic's own history.
   Ordering (acquire/release/seq_cst): relationships ACROSS variables.
```

---

## 7. Sequential consistency (the strongest, the default)

`memory_order_seq_cst` (the default) adds, on top of acquire/release, a **single
global total order** `S` over *all* seq_cst operations that every thread agrees
on. This restores the intuitive "one shared memory, consistent order" illusion —
at a cost.

```
   With seq_cst everywhere, you may reason AS IF:
     * all threads' operations are interleaved into ONE global sequence,
     * consistent with each thread's program order.

   This is the model in chapter 00's mental picture. It forbids the Store-Buffer
   litmus outcome (both reads = 0) from chapter 02 -> at least one read sees 1.
```

```
   seq_cst = acquire (on loads) + release (on stores) + a global total order S.
   It's the SAFE DEFAULT. Weaker orders drop the global order S (and some
   barriers) for speed — only do that when you can prove correctness.
```

---

## 8. Putting the relations in one picture

```
                       THE C++ MEMORY MODEL
  ┌──────────────────────────────────────────────────────────────┐
  │ within a thread:        sequenced-before  (program order)      │
  │                                                                │
  │ across threads:         release ──synchronizes-with──▶ acquire │
  │                                                                │
  │ combine transitively:   HAPPENS-BEFORE                         │
  │     (A hb B  =>  A's writes visible to B, no race)             │
  │                                                                │
  │ per atomic object:      MODIFICATION ORDER (coherence)         │
  │                                                                │
  │ seq_cst adds:           single GLOBAL total order S            │
  └──────────────────────────────────────────────────────────────┘
  Data race  =  conflicting access with NO happens-before  =  UB.
```

---

## 9. A worked message-passing proof

```cpp
std::atomic<bool> ready{false};
int payload = 0;                 // plain int (non-atomic)

// Producer
payload = 99;                              // (1)
ready.store(true, std::memory_order_release);   // (2)

// Consumer
while (!ready.load(std::memory_order_acquire)) {}  // (3)
assert(payload == 99);                      // (4) NEVER fires
```

```
   (1) seq-before (2)              [producer program order]
   (2) synchronizes-with (3)       [release store read by acquire load]
   (3) seq-before (4)              [consumer program order]
   => (1) happens-before (4)
   => payload's write is visible at (4); the assert holds; no data race.

   If (2)/(3) used RELAXED instead, (2) would NOT synchronize-with (3):
   -> no happens-before on payload -> DATA RACE -> the assert could fire / UB.
```

Runnable: [`examples/ch04_message_passing.cpp`](examples/ch04_message_passing.cpp).

---

## 10. Summary

<!--diagram
title: Memory model summary
box[blue] Ordering relations
  text: **sequenced-before**: in-thread order
  text: **synchronizes-with**: release store <-> acquire load (handshake)
  text: **happens-before**: transitive combo -> defines visibility; no happens-before + conflicting access = data race = UB
box[teal] modification order
  text: Per-atomic total order (coherence); relaxed suffices for a single variable's own value
box[green] seq_cst
  text: + one **GLOBAL** total order over seq_cst ops — the safe default; matches the naive model
box[green] Key idea
  text: A release/acquire pair **PUBLISHES** ordinary data across threads safely (the everyday use of atomics)
-->

```
 +------------------------------------------------------------------+
 | sequenced-before  : in-thread order.                             |
 | synchronizes-with : release store  <->  acquire load (handshake).|
 | happens-before    : transitive combo -> defines visibility.      |
 |    no happens-before + conflicting access = data race = UB.      |
 | modification order: per-atomic total order (coherence); relaxed  |
 |    suffices for a single variable's own value.                  |
 | seq_cst           : + one GLOBAL total order over seq_cst ops.   |
 |                     the safe default; matches the naive model.   |
 |                                                                  |
 | KEY IDEA: a release/acquire pair PUBLISHES ordinary data across  |
 | threads safely (the everyday use of atomics).                   |
 +------------------------------------------------------------------+
```

Next: [05-memory-orderings.md](05-memory-orderings.md).
