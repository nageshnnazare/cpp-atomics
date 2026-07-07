# 00 — Why Atomics? The Problem They Solve

Before syntax, understand the *problem*. Atomics exist because **multiple threads
touching the same memory** is deeply subtle — far more than it looks.

---

## 1. The innocent-looking bug

```cpp
int counter = 0;                 // shared, plain int

void worker() {
    for (int i = 0; i < 100000; ++i)
        ++counter;               // looks atomic. IT IS NOT.
}

// run worker() on 2 threads, join, print counter.
// Expected: 200000.  Actual: some smaller, RANDOM number.
```

Why? `++counter` is **not one operation**. It is three:

```
   ++counter  compiles to roughly:
        1. LOAD  counter -> register     (read)
        2. ADD   register, 1             (modify)
        3. STORE register -> counter     (write)
   This "read-modify-write" (RMW) can be INTERLEAVED between threads.
```

### The interleaving that loses an update

```
   counter starts at 5.

   Thread A                     Thread B
   --------                     --------
   LOAD  counter (5)
                                LOAD  counter (5)     <- both read 5!
   ADD   -> 6
                                ADD   -> 6
   STORE counter = 6
                                STORE counter = 6     <- overwrites A's 6
   ---------------------------------------------------
   Two increments happened, but counter is 6, not 7. ONE UPDATE LOST.
```

```
   Time --->
   A:  R5 .... +1 .... W6
   B:  ...... R5 ......... +1 ...... W6
                                        ^ final value 6 (should be 7)
```

This is a **race condition** (specifically a *lost update*). It is not a
theoretical worry; it happens constantly on real hardware and is why the final
count is random.

---

## 2. It gets worse: it's Undefined Behavior

In C++, two threads accessing the same non-atomic object where at least one
writes, without synchronization, is a **data race**, which is **undefined
behavior (UB)** — not "you get a wrong number," but "the entire program is
meaningless." The compiler may assume it never happens and optimize accordingly.

```
   Data race = UB. Consequences are not limited to "wrong value":
     * torn reads/writes (half-updated values)
     * the compiler caches the variable in a register and never re-reads it
     * "impossible" values, crashes, or code paths that "can't happen"
```

More on this in [01-data-races-and-ub.md](01-data-races-and-ub.md).

---

## 3. Two ways to fix it

### Fix A: a mutex (lock)

```cpp
#include <mutex>
int counter = 0;
std::mutex m;

void worker() {
    for (int i = 0; i < 100000; ++i) {
        std::lock_guard<std::mutex> lock(m);   // only one thread at a time
        ++counter;                              // now safe
    }
}
```

```
   Mutex = "one thread in the critical section at a time."
   A:  [lock] R5 +1 W6 [unlock]
   B:                    [lock] R6 +1 W7 [unlock]
   -> serialized -> no lost updates. Correct: 7.
```

This is the **default, correct, simple** solution. It works for arbitrary
critical sections (not just one variable).

### Fix B: an atomic

```cpp
#include <atomic>
std::atomic<int> counter{0};

void worker() {
    for (int i = 0; i < 100000; ++i)
        ++counter;               // atomic RMW: indivisible, no lock needed
}
```

```
   std::atomic<int>::operator++ performs the read-modify-write as ONE
   INDIVISIBLE hardware operation (e.g. x86 'lock xadd').
   No other thread can observe or interrupt the middle.
   -> Correct: 200000.
```

```
   Atomic RMW is a single, indivisible step:
   A:  [--R+1W--]
   B:            [--R+1W--]     (hardware guarantees no overlap on that word)
```

---

## 4. Mutex vs atomic — the honest comparison

```
                    MUTEX                          ATOMIC
   ----------------------------------------------------------------------
   protects        arbitrary code region          a single variable/word
   ease            very easy, hard to misuse      easy to misuse subtly
   blocking        yes (threads can sleep)         no (lock-free, spins)
   overhead        higher per op (syscall if       lower per op
                   contended), but composes
   deadlock risk   yes                             no (no locks held)
   priority inv.   possible                        avoided
   use for         99% of shared-state needs       counters, flags, lock-free
   memory ordering hidden (lock/unlock do it)      YOU choose (the hard part)
```

The catch that makes atomics hard: **you** become responsible for **memory
ordering** — controlling how operations on *other* variables become visible
across threads. That is the subject of most of this guide.

---

## 5. What "atomic" actually guarantees (and doesn't)

```
   ATOMIC guarantees:
     * Indivisibility: an atomic load/store/RMW happens all-at-once. No torn
       or half-written values on that object.
     * A defined memory ORDER relationship with other operations (you pick it).

   ATOMIC does NOT automatically give you:
     * A correct algorithm. atomic<int> counter is safe to increment, but
       "if (counter == 0) do_once();" across threads is still a race in LOGIC.
     * Protection for MORE than the single atomic object (unless you design the
       ordering to publish other data — that's what acquire/release is for).
```

Example of an atomic that's still logically wrong:

```cpp
std::atomic<int> balance{100};
// Thread A and B both run:
if (balance >= 50)          // (1) atomic load
    balance -= 50;          // (2) atomic RMW  -- but (1) and (2) are SEPARATE!
// Two threads can both pass the check with balance=100, then both subtract,
// leaving -0? No: leaving 0 after two -50, but if balance were 50 both could
// pass and it goes to -50. The CHECK-THEN-ACT is not atomic as a whole.
```

Fix: use a single atomic **compare-exchange** loop (chapter 10) or a mutex.

---

## 6. The mental model to carry forward

<!--diagram
title: Mental model
box[red] Without synchronization
  text: Threads + shared mutable data without synchronization = **data race** = **Undefined Behavior** (not just "wrong number")
box[green] Fix by synchronizing
  text: **mutex**: simple, default, protects regions. USE THIS USUALLY
  text: **atomic**: single-object, lock-free, but YOU manage **ORDERING**
box[blue] Remember
  text: Atomic = indivisible operation + a chosen visibility order
  text: Atomicity != algorithm correctness (check-then-act still races)
-->

```
   +------------------------------------------------------------------+
   | Threads + shared mutable data without synchronization = data race|
   |   = Undefined Behavior (not just "wrong number").                |
   |                                                                  |
   | Fix by SYNCHRONIZING:                                            |
   |   * mutex: simple, default, protects regions. USE THIS USUALLY.  |
   |   * atomic: single-object, lock-free, but YOU manage ORDERING.   |
   |                                                                  |
   | Atomic = indivisible operation + a chosen visibility order.      |
   | Atomicity != algorithm correctness (check-then-act still races). |
   +------------------------------------------------------------------+
```

Next: [01-data-races-and-ub.md](01-data-races-and-ub.md) — precisely what a data
race is, so you can recognize and avoid them.
