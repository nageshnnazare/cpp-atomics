# 02 — Hardware Background: Caches, Store Buffers & Reordering

Memory ordering feels arbitrary until you see *why* it exists. This chapter is
the "physics" behind atomics: modern CPUs and compilers **reorder** memory
operations for speed, and each core sees memory through **caches** and **store
buffers**. Ordering rules are how you tame that.

> You don't need to be a hardware engineer. But this intuition makes every
> `memory_order` obvious instead of magic.

---

## 1. The naive (wrong) mental model

```
   What beginners imagine:

     Thread A ---writes---> [  ONE SHARED MEMORY  ] <---reads--- Thread B
                            (everyone sees writes instantly, in order)

   This is called SEQUENTIAL CONSISTENCY. It is NOT how real hardware works
   by default. It's an ILLUSION we sometimes pay for.
```

---

## 2. The real hardware model (simplified)

```
   +---------+   +---------+          +---------+
   | Core 0  |   | Core 1  |   ...    | Core N  |
   | regs    |   | regs    |          | regs    |
   | store   |   | store   |          | store   |   <- STORE BUFFER (write queue)
   | buffer  |   | buffer  |          | buffer  |
   +----+----+   +----+----+          +----+----+
        |L1|          |L1|                 |L1|      <- per-core cache
        +--+          +--+                 +--+
        |L2|          |L2|                 |L2|
        +--+----------+--+-----------------+--+
                    | L3 (shared) |
                    +------+------+
                           |
                    +------+------+
                    |  Main RAM   |
                    +-------------+
```

Two features cause all the trouble:

```
   1. STORE BUFFER: when a core writes, it puts the store in a local buffer and
      moves on WITHOUT waiting for it to reach cache/RAM. Other cores can't see
      it yet. The core's OWN later reads see it (store forwarding), but others
      lag.

   2. CACHES + coherence: each core caches copies. Cache coherence protocols
      (MESI) eventually make everyone agree, but "eventually" and "in what
      order" is the question.
```

---

## 3. Reordering: four kinds

Operations can appear to happen out of program order. There are two *sources*
(compiler and CPU) and four *directions*:

```
   StoreStore : two stores reorder    (W W)
   LoadLoad   : two loads reorder     (R R)
   LoadStore  : a load and a later store swap
   StoreLoad  : a store and a later load swap  <- the hard one (store buffer)
```

```
   COMPILER reordering: the optimizer moves instructions (e.g. hoists loads,
     sinks stores) as long as SINGLE-THREADED behavior is unchanged. It has no
     idea about other threads unless you use atomics / fences.

   CPU reordering: even the compiled instruction stream is executed out of
     order and stores are buffered. Each ISA has its own rules (memory model).
```

---

## 4. The classic "Store Buffer" litmus test

Two threads, two flags. Can *both* reads see 0?

```cpp
int x = 0, y = 0;      // (imagine atomics with relaxed order)
int r1, r2;

// Thread A            // Thread B
x = 1;                 y = 1;
r1 = y;                r2 = x;
```

```
   Intuition (sequential consistency): at least one of r1, r2 must be 1.
   REALITY on x86/ARM/etc.: r1 == 0 AND r2 == 0 is POSSIBLE.

   Why? Store buffers:
     A writes x=1 -> sits in A's store buffer (B can't see it yet)
     B writes y=1 -> sits in B's store buffer (A can't see it yet)
     A reads y    -> still 0 (B's store not visible)
     B reads x    -> still 0 (A's store not visible)
   Both read 0. The stores were "delayed" past the loads (StoreLoad reorder).
```

```
   Time --->
   A:  [store x=1 -> buffer] .......... [load y = 0]
   B:  ........ [store y=1 -> buffer] ...... [load x = 0]
       (neither store has drained to the other core when the loads happen)
```

This is *the* example that shows why you sometimes need the strongest ordering
(`seq_cst`) or explicit fences — it's the one thing acquire/release does **not**
prevent (chapter 06).

---

## 5. Different CPUs, different default strictness

```
   x86 / x86-64  : "strong" model (TSO — Total Store Order).
        Only StoreLoad reordering happens. Loads/stores are otherwise ordered.
        -> acquire/release are often "free" (no extra instructions); seq_cst
           needs one fence/locked op for the StoreLoad case.

   ARM / ARM64 / POWER / RISC-V : "weak" models.
        Almost everything can reorder. You NEED explicit barriers for ordering.
        -> the memory_order you choose actually emits different instructions;
           bugs that "never reproduce on my x86 laptop" appear on ARM.
```

```
   Lesson: your code must be correct per the C++ MEMORY MODEL, not per what
   your x86 machine happens to allow. "Works on my machine" is meaningless for
   ordering bugs — test on ARM and with TSan.
```

---

## 6. What atomics + memory_order actually do

An atomic operation with a memory order is a **contract** that:

```
   1. Makes the operation itself indivisible (no torn values).
   2. Constrains reordering around it (a compiler + CPU barrier of some strength).
   3. Establishes cross-thread visibility relationships (happens-before).
```

```
   memory_order  = "how strong a fence does this atomic op carry?"

   relaxed   : atomic value only; NO ordering of other memory. (weakest)
   acquire   : this load; no reads/writes AFTER it move BEFORE it. (a "one-way
               barrier" downward)
   release   : this store; no reads/writes BEFORE it move AFTER it. (one-way up)
   acq_rel   : both (for RMW).
   seq_cst   : acquire+release AND a single global total order. (strongest)
```

Full precise treatment in [05-memory-orderings.md](05-memory-orderings.md); the
diagram to hold onto:

```
   ACQUIRE (on a load):                RELEASE (on a store):
       ... anything ...                    ... anything ...
   +--------------------+              [ these stay ABOVE ]  <-- can't sink below
   |  load-acquire  X   |                    |
   +--------------------+              +--------------------+
   [ these stay BELOW ] <-- can't      |  store-release Y   |
   |  rise above the load              +--------------------+
   v                                       ... anything ...
   (reads/writes after can't move up)  (reads/writes before can't move down)
```

---

## 7. Cache line = the unit of sharing (false sharing preview)

Coherence works on **cache lines** (typically 64 bytes), not individual bytes.
Two unrelated atomics on the same line "fight" even though they're logically
independent — **false sharing** (chapter 12).

```
   One 64-byte cache line:
   [ counterA | counterB | ...padding... ]
        ^Core0 writes    ^Core1 writes
   Even though A and B are different variables, every write invalidates the
   other core's cached copy of the WHOLE line -> ping-pong -> slow.
   Fix: pad/align each to its own line (alignas(64) / hardware_destructive_
   interference_size).
```

---

## 8. Summary

<!--diagram
title: Hardware background summary
box[blue] Real hardware
  text: Per-core caches + store buffers. Writes are not instantly/uniformly visible; operations get **REORDERED** by both the compiler and the CPU
box[orange] Four reorderings
  text: StoreStore, LoadLoad, LoadStore, StoreLoad
  text: StoreLoad (store buffer) is the sneaky one -> the SB litmus test can see both reads = 0
box[teal] CPU models
  text: x86 = strong (TSO); ARM/POWER = weak. Code to the C++ model, not to your CPU. Test on ARM + TSan
box[purple] memory_order & coherence
  text: `memory_order` = the **STRENGTH** of the barrier an atomic op carries
  text: Coherence unit = cache line (64B) -> beware false sharing
-->

```
 +------------------------------------------------------------------+
 | Real HW: per-core caches + store buffers. Writes are not         |
 | instantly/uniformly visible; operations get REORDERED by both    |
 | the compiler and the CPU.                                        |
 |                                                                  |
 | 4 reorderings: StoreStore, LoadLoad, LoadStore, StoreLoad.       |
 | StoreLoad (store buffer) is the sneaky one -> the SB litmus test |
 | can see both reads = 0.                                          |
 |                                                                  |
 | x86 = strong (TSO); ARM/POWER = weak. Code to the C++ model, not |
 | to your CPU. Test on ARM + TSan.                                 |
 |                                                                  |
 | memory_order = the STRENGTH of the barrier an atomic op carries. |
 | Coherence unit = cache line (64B) -> beware false sharing.       |
 +------------------------------------------------------------------+
```

Next: [03-atomic-basics.md](03-atomic-basics.md).
