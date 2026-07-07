# 01 — Data Races, Undefined Behavior & "Thread-Safe"

To use atomics correctly you must precisely understand what they *prevent*: the
**data race**. This chapter defines it rigorously and shows why it's UB, not just
"a wrong value."

---

## 1. The precise definition (C++ standard)

```
   A DATA RACE occurs when:
     * two or more threads access the SAME memory location,
     * at least ONE access is a WRITE (a modification),
     * the accesses are NOT ordered by a "happens-before" relationship
       (i.e. not synchronized), and
     * at least one access is NON-ATOMIC.

   If a program has a data race, its behavior is UNDEFINED.
```

```
   Concurrent accesses matrix (same location, no synchronization):

                 Thread B: READ        Thread B: WRITE
   Thread A:     +-------------------+-------------------+
   READ          |    OK (safe)      |   DATA RACE (UB)  |
                 +-------------------+-------------------+
   WRITE         |   DATA RACE (UB)  |   DATA RACE (UB)  |
                 +-------------------+-------------------+

   Two reads are fine. Any write with a concurrent access = race (unless atomic
   or synchronized).
```

Key: **read/read is always safe**. Immutable shared data needs no
synchronization. The trouble is *mutation*.

---

## 2. Why UB, not just "wrong value"?

Because the compiler and hardware are allowed to assume **no data races exist**,
and they optimize on that assumption. When you break it, "spooky" things happen.

### Compiler example: hoisting a load out of a loop

```cpp
bool stop = false;               // plain bool, no atomic

void wait_loop() {
    while (!stop) { /* spin */ }  // another thread sets stop = true
}
```

```
   The compiler sees: 'stop' is never modified INSIDE this function, and there's
   no synchronization, so it may transform:

     while (!stop) {}         -->     if (!stop) { for(;;) {} }   // INFINITE!

   It loaded 'stop' ONCE into a register. Your other thread's write is invisible.
   The loop never exits. This is a real, common bug with non-atomic flags.
```

Fix: `std::atomic<bool> stop{false};` — the compiler must re-read it and other
threads' writes become visible.

### Hardware example: torn values

```
   A 64-bit value written non-atomically on some platforms can be split into
   two 32-bit stores. A reader may observe HALF the old value and HALF the new:

     old = 0x0000_0000_FFFF_FFFF
     new = 0x1111_1111_0000_0000
     torn read possible: 0x0000_0000_0000_0000 or 0x1111_1111_FFFF_FFFF
   -> a value that was NEVER written. Atomics forbid this.
```

---

## 3. Race condition vs data race (not the same thing!)

```
   DATA RACE      = a specific technical UB (unsynchronized conflicting access).
   RACE CONDITION = a broader term: the result depends on timing/interleaving.

   You can have:
     * a RACE CONDITION without a DATA RACE (e.g. two atomic ops interleaved in
       a logically-wrong order — the check-then-act bug from chapter 00).
     * a DATA RACE that is a race condition too (the usual case).
```

Atomics eliminate **data races** on the atomic object. They do **not** magically
fix **race conditions** in your algorithm — you still must design correct logic.

```
   std::atomic<int> x;
   if (x.load() == 0)     // no data race here...
       x.store(1);        // ...but another thread may have stored between the
                          // load and the store: RACE CONDITION remains.
   -> use x.compare_exchange (chapter 10) to make check-and-set atomic.
```

---

## 4. What creates "happens-before" (synchronization)

A data race requires the *absence* of happens-before ordering. These establish
it (details in [04-memory-model.md](04-memory-model.md)):

```
   Establishes happens-before between threads:
     * mutex unlock (thread A) ---> mutex lock (thread B) on the same mutex
     * atomic store-release (A) ---> atomic load-acquire (B) reading that value
     * thread creation: parent's prior work ---> the new thread's start
     * thread join: the joined thread's work ---> the joiner after join()
     * std::atomic operations with appropriate ordering
     * condition_variable wait/notify (built on a mutex)
```

```
   Thread A                         Thread B
   data = 42;                       (waits...)
   flag.store(true, release);  --happens-before-->  while(!flag.load(acquire)){}
                                                     read data;   // sees 42, safe
   The release/acquire pair creates the ordering that makes 'data = 42' visible
   and race-free — even though 'data' is a PLAIN int!
```

This is the crucial insight: **atomics don't just protect themselves; a properly
ordered atomic can safely "publish" ordinary (non-atomic) data** to another
thread. That's the whole point of acquire/release (chapter 06).

---

## 5. Detecting data races: ThreadSanitizer

You cannot reliably find races by testing — they're timing-dependent. Use
**ThreadSanitizer (TSan)**, a runtime detector:

```bash
clang++ -std=c++20 -O1 -g -fsanitize=thread -pthread race.cpp -o race
./race         # prints a detailed report if a data race occurs
```

```
   TSan output (abbreviated):
     WARNING: ThreadSanitizer: data race
       Write of size 4 by thread T2 ... in worker()
       Previous read of size 4 by thread T1 ... in worker()
       Location: global 'counter'
   -> pinpoints the exact conflicting accesses and stacks.
```

Run [`examples/ch01_race.cpp`](examples/ch01_race.cpp) under TSan to see it fire,
then the atomic version to see it clean.

---

## 6. "Thread-safe" is not one thing

Levels of guarantee you'll see documented:

```
   * const/read-only shared     : safe for concurrent reads (no writers).
   * "thread-safe" (internally  : you can call methods from multiple threads;
      synchronized)                the object locks internally (e.g. a concurrent
                                   queue). Often slower.
   * "thread-compatible"        : distinct objects usable from distinct threads;
      (most std types, e.g.        the SAME object needs external synchronization.
      std::vector)                 Concurrent reads OK; any write needs a lock.
   * "not thread-safe"          : all sharing needs external synchronization.
```

Most standard library types (`std::vector`, `std::string`, `std::map`) are
**thread-compatible**: concurrent reads of the *same* object are fine; concurrent
write (or read+write) is a data race. `std::shared_ptr`'s control block refcount
is atomic, but the pointee is not automatically protected.

---

## 7. Summary

<!--diagram
title: Data races summary
box[red] Data race
  text: **DATA RACE** = unsynchronized access to the same location where >=1 is a write and >=1 is non-atomic ==> **UNDEFINED BEHAVIOR**
  text: (read/read is always safe.)
box[orange] What UB means
  text: "Wrong value" **AND** compiler hoisting, torn reads, infinite loops, "impossible" states
box[blue] Race condition vs data race
  text: Race condition (timing-dependent logic) != data race; atomics fix data races, not necessarily your algorithm's logic
box[green] Happens-before
  text: Happens-before (mutex, release/acquire, join, thread start) removes races **AND** lets atomics publish plain data
box[teal] Detection
  text: Detect with ThreadSanitizer: `-fsanitize=thread`
-->

```
 +------------------------------------------------------------------+
 | DATA RACE = unsynchronized access to the same location where     |
 |   >=1 is a write and >=1 is non-atomic  ==>  UNDEFINED BEHAVIOR. |
 |   (read/read is always safe.)                                    |
 |                                                                  |
 | UB means "wrong value" AND compiler hoisting, torn reads,        |
 |   infinite loops, "impossible" states.                           |
 |                                                                  |
 | Race condition (timing-dependent logic) != data race; atomics    |
 |   fix data races, not necessarily your algorithm's logic.        |
 |                                                                  |
 | Happens-before (mutex, release/acquire, join, thread start)      |
 |   removes races AND lets atomics publish plain data.             |
 |                                                                  |
 | Detect with ThreadSanitizer: -fsanitize=thread.                  |
 +------------------------------------------------------------------+
```

Next: [02-hardware-background.md](02-hardware-background.md) — the caches and
reordering that make ordering necessary.
