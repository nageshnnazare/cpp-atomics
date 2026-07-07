# C++ Atomics & Memory Ordering — The One-Stop Guide (up to C++26)

> A from-scratch, diagram-heavy guide to **atomics**, the **C++ memory model**,
> and **memory ordering**. Written for someone who is a **beginner in atomics**
> but comfortable with intermediate C++. We build intuition first (what hardware
> and compilers actually do), then the precise rules, then real lock-free code.

---

## Who this is for

You know C++ (classes, references, RAII, `std::thread` at a basic level) but
`std::memory_order_acquire` looks like hieroglyphics. By the end you will:

- Understand **why** atomics exist (data races, caches, reordering).
- Read and reason about every `std::memory_order`.
- Use `compare_exchange`, fences, and `atomic_ref` correctly.
- Build a lock-free stack/queue and know the pitfalls (ABA, false sharing).
- Know the C++20/23/26 additions (`wait`/`notify`, `atomic<shared_ptr>`,
  `atomic_ref`, `<stdatomic.h>` interop, and more).
- Know **when NOT to use atomics** (usually: use a mutex — and why that's fine).

---

## How to read this guide

Read in order the first time — the memory model chapters build strictly on each
other. Skipping to "relaxed ordering" without the foundations is how people write
subtly broken code.

| # | File | Topic |
|---|------|-------|
| 00 | [00-why-atomics.md](00-why-atomics.md) | The problem: threads, shared state, why `x++` is dangerous |
| 01 | [01-data-races-and-ub.md](01-data-races-and-ub.md) | Data races, undefined behavior, what "thread-safe" means |
| 02 | [02-hardware-background.md](02-hardware-background.md) | Caches, store buffers, reordering — the *why* behind ordering |
| 03 | [03-atomic-basics.md](03-atomic-basics.md) | `std::atomic<T>`, load/store/RMW, `is_lock_free` |
| 04 | [04-memory-model.md](04-memory-model.md) | The C++ memory model: sequenced-before, happens-before, SC |
| 05 | [05-memory-orderings.md](05-memory-orderings.md) | The 6 memory orders, precisely, with diagrams |
| 06 | [06-acquire-release.md](06-acquire-release.md) | Acquire/release in depth — the everyday tool |
| 07 | [07-relaxed.md](07-relaxed.md) | Relaxed ordering: counters, when it's safe, when it bites |
| 08 | [08-seq-cst-and-consume.md](08-seq-cst-and-consume.md) | `seq_cst` (default) and the cautionary tale of `consume` |
| 09 | [09-fences.md](09-fences.md) | `atomic_thread_fence`, standalone fences vs operation orders |
| 10 | [10-compare-exchange.md](10-compare-exchange.md) | CAS, `weak` vs `strong`, the CAS loop, ABA problem |
| 11 | [11-lock-free-structures.md](11-lock-free-structures.md) | Lock-free stack & SPSC queue, hazards, reclamation |
| 12 | [12-patterns-and-pitfalls.md](12-patterns-and-pitfalls.md) | Patterns (spinlock, flags, double-checked init), false sharing |
| 13 | [13-cpp20-26-features.md](13-cpp20-26-features.md) | `wait`/`notify`, `atomic_ref`, `atomic<shared_ptr>`, C++26 |
| 14 | [14-cheatsheet.md](14-cheatsheet.md) | Quick reference / decision guide |

Runnable examples live in [`examples/`](examples/), with a build script that
uses ThreadSanitizer where available.

---

## The single most important takeaway (read this now)

<!--diagram
title: The single most important takeaway
box[green] Guidance
  text: **99% of the time**: use a `std::mutex` (or higher-level tools)
  text: Atomics are for the **1%**: lock-free counters, flags, and carefully-designed lock-free data structures
  text: If you reach for `memory_order_relaxed` to be "fast", **STOP**
  text: Default to `memory_order_seq_cst` until you can **PROVE** a weaker order is correct
  text: Correct-and-slower beats fast-and-wrong
-->

```
   +---------------------------------------------------------------+
   | 99% of the time: use a std::mutex (or higher-level tools).    |
   | Atomics are for the 1%: lock-free counters, flags, and        |
   | carefully-designed lock-free data structures.                 |
   |                                                               |
   | If you reach for memory_order_relaxed to be "fast", STOP.     |
   | Default to memory_order_seq_cst until you can PROVE a weaker  |
   | order is correct. Correct-and-slower beats fast-and-wrong.    |
   +---------------------------------------------------------------+
```

This guide teaches the weak orderings thoroughly *so that you can understand
existing code and the rare cases you truly need them* — not so you sprinkle them
everywhere.

---

## Building the examples

```bash
# a single example
clang++ -std=c++20 -O2 -pthread examples/ch03_counter.cpp -o /tmp/demo && /tmp/demo

# with ThreadSanitizer (highly recommended for concurrency code)
clang++ -std=c++20 -O1 -g -fsanitize=thread -pthread \
        examples/ch01_race.cpp -o /tmp/race && /tmp/race

# build everything your compiler supports
./examples/build_all.sh
```

> ThreadSanitizer (`-fsanitize=thread`) is your best friend: it detects data
> races at runtime. Several examples are designed to be run under TSan.

---

## The 30-second map

```
                         SHARED MUTABLE STATE
                                 |
                +----------------+----------------+
                |                                 |
           NO SYNC                            SYNCHRONIZED
        (data race = UB)                          |
                              +-------------------+-------------------+
                              |                                       |
                          MUTEX / locks                          ATOMICS
                        (simple, default)                    (lock-free-ish)
                                                                   |
                                              +--------------------+-----------+
                                              |                                |
                                        WHAT operation                   WHAT ORDER
                                        load/store/RMW/CAS         relaxed / acquire /
                                                                   release / acq_rel /
                                                                   seq_cst / (consume)
```

Start with [00-why-atomics.md](00-why-atomics.md).
