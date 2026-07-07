# 05 — The Six Memory Orderings, Precisely

`std::memory_order` has six enumerators. This chapter defines each one, which
operations accept it, and shows the barrier each implies with diagrams. Chapters
06–08 then go deep on the important ones.

---

## 1. The enumerators

```cpp
enum class memory_order {
    relaxed,   // no ordering, only atomicity + coherence
    consume,   // (discouraged; treated as acquire in practice)
    acquire,   // for loads / RMW: "one-way barrier, nothing moves up past it"
    release,   // for stores / RMW: "one-way barrier, nothing moves down past it"
    acq_rel,   // for RMW: acquire + release together
    seq_cst    // sequential consistency (the DEFAULT)
};
```

Aliases you'll see: `std::memory_order_relaxed`, `..._acquire`, etc. (C++20 also
lets you write `std::memory_order::acquire`).

---

## 2. Which order is valid on which operation

Using the wrong order for an operation is a **precondition violation** (often
compile-time-checkable, always logically wrong):

```
   Operation          Sensible orders
   ----------------   --------------------------------------------------
   load               relaxed, consume, acquire,           seq_cst
   store              relaxed,                   release,   seq_cst
   RMW (fetch_add,    relaxed, consume, acquire, release,  seq_cst
     exchange, CAS)     acq_rel
   fence              relaxed, acquire, release, acq_rel,  seq_cst

   Nonsense combos (don't do):
     store with acquire       (a store can't "acquire")
     load  with release       (a load can't "release")
```

```
   Mnemonic:
     loads  ACQUIRE (pull in others' writes)
     stores RELEASE (push out your writes)
     RMW    both -> acq_rel
```

---

## 3. `relaxed` — atomicity only

```
   GUARANTEES: the op is indivisible; per-object modification order/coherence.
   DOES NOT:   order this op relative to any OTHER memory operation.

   Use when: you only care about the atomic's own value, not about publishing
   or observing OTHER data (e.g. a statistics counter). See chapter 07.
```

```
   relaxed load/store: the compiler & CPU may freely reorder surrounding
   NON-dependent memory ops around it.

   x.store(1, relaxed);      // no fence; neighbors can move across this
   int v = y.load(relaxed);  // no fence
```

---

## 4. `acquire` — a "downward" one-way barrier (loads)

```
   On a LOAD (or RMW): no reads or writes that appear AFTER it in program order
   may be reordered to BEFORE it. And it participates in synchronizes-with:
   it "acquires" everything the matching release published.

        ... code ...
   ┌───────────────────────┐
   │  X = a.load(acquire)  │
   └───────────┬───────────┘
               │  ⇩ operations below CANNOT rise above this load
        reads/writes after
```

```
   Think: "acquire = start of a critical section." After you acquire, you see
   everything the releaser did, and your subsequent ops stay after the acquire.
```

---

## 5. `release` — an "upward" one-way barrier (stores)

```
   On a STORE (or RMW): no reads or writes that appear BEFORE it in program
   order may be reordered to AFTER it. It "releases" (publishes) all prior work.

        reads/writes before
               │  ⇧ operations above CANNOT sink below this store
   ┌───────────┴───────────┐
   │  a.store(v, release)  │
   └───────────────────────┘
        ... code ...
```

```
   Think: "release = end of a critical section." Everything you did before the
   release becomes visible to whoever acquires the value you stored.
```

### The pair in one diagram

```
   Thread A (producer)                 Thread B (consumer)
   ───────────────────                 ────────────────────
   write data...          ⇧ can't        │
   ┌─────────────────┐    sink below     │
   │ store(release)  │ ~~~~~~~~~~~~~~~▶ ┌─────────────────┐
   └─────────────────┘   synchronizes   │ load(acquire)   │
                          with          └────────┬────────┘
                                          read data ⇩ can't rise above
   => everything A wrote before release is visible to B after acquire.
```

---

## 6. `acq_rel` — both, for RMW

```
   For a read-modify-write (fetch_add, exchange, compare_exchange): the read
   part is ACQUIRE and the write part is RELEASE.
   Use when an RMW must BOTH observe prior publishes AND publish its own effects
   (e.g. lock-free stack push/pop, releasing a lock).
```

```
   node->next = head.load();                  // (build node)
   head.compare_exchange_weak(expected, node, acq_rel);
        ▲ acquire: see other pushers' work    ▲ release: publish node->next
```

Note: on a *failed* CAS there's no write, so you often specify a weaker failure
order (chapter 10).

---

## 7. `seq_cst` — acquire+release + global total order (default)

```
   Everything acquire/release gives, PLUS: all seq_cst operations across all
   threads share ONE global total order S that everyone agrees on.
   This is the DEFAULT if you omit the order argument.
```

```
   x.store(1);            // == x.store(1, memory_order_seq_cst)
   int v = x.load();      // == x.load(memory_order_seq_cst)

   seq_cst is the only order that prevents the Store-Buffer litmus outcome
   (chapter 02): with both stores+loads seq_cst, r1==0 && r2==0 is IMPOSSIBLE.
```

Cost: on x86 a seq_cst store needs a fence/locked op (StoreLoad barrier); on
weak CPUs (ARM) seq_cst is notably more expensive than acquire/release.

---

## 8. Strength ladder & what each prevents

```
   WEAKER ────────────────────────────────────────────────▶ STRONGER
   relaxed  <  consume  <  acquire/release  <  acq_rel  <  seq_cst

   Reordering prevented (roughly):
   relaxed : none (atomicity/coherence only)
   acquire : LoadLoad + LoadStore  (after the load)
   release : StoreStore + LoadStore (before the store)
   acq_rel : both of the above (for an RMW)
   seq_cst : all of the above + StoreLoad + a single global order
```

```
                    prevents StoreLoad reorder & gives global order?
   relaxed          NO
   acquire/release  NO      (this is why the SB litmus can still bite them)
   seq_cst          YES
```

---

## 9. How to choose (the practical rule)

<!--diagram
title: How to choose memory order
box[green] Practical rule
  text: 1. Start with `seq_cst` (the default). It's correct.
  text: 2. If profiling shows the atomic is hot, and you can **PROVE** it:
  box[teal] Weaker orders (when proven)
    text: pure counter, value only -> `relaxed`
    text: publish/consume data (flag+data) -> `release` / `acquire`
    text: RMW that publishes+observes -> `acq_rel`
  text: 3. Never use `consume` (use `acquire`)
  text: 4. Verify with TSan + reasoning + (ideally) on ARM hardware
-->

```
   +----------------------------------------------------------------+
   | 1. Start with seq_cst (the default). It's correct.             |
   | 2. If profiling shows the atomic is hot, and you can PROVE it: |
   |      * pure counter, value only         -> relaxed             |
   |      * publish/consume data (flag+data) -> release / acquire   |
   |      * RMW that publishes+observes       -> acq_rel            |
   | 3. Never use 'consume' (use acquire).                          |
   | 4. Verify with TSan + reasoning + (ideally) on ARM hardware.   |
   +----------------------------------------------------------------+
```

Decision diagram:

```
   Do you use the atomic's value to guard OTHER (non-atomic) data?
        │
    NO ─┼─▶ only the number/flag itself matters?
        │        └─▶ relaxed may be OK (counters, stats). (chapter 07)
        │
   YES ─┴─▶ producer side (store)  -> release
            consumer side (load)   -> acquire
            single var, RMW both   -> acq_rel
            need global total order across multiple vars -> seq_cst
```

---

## 10. Same code, three orderings (what changes)

```cpp
std::atomic<int> flag{0};
int data = 0;

// PRODUCER
data = 42;
flag.store(1, /* ORDER */);

// CONSUMER
while (flag.load(/* ORDER */) == 0) {}
use(data);
```

```
   ORDER = relaxed  :  BROKEN. No synchronizes-with; 'use(data)' may see 0 or
                       garbage -> data race on 'data' -> UB.
   ORDER = release/acquire : CORRECT. Handshake publishes 'data'. Cheapest
                       correct option for pure message passing.
   ORDER = seq_cst  :  CORRECT (also). Extra global-order guarantee you don't
                       need here -> potentially slower on weak CPUs.
```

Runnable: [`examples/ch05_orderings.cpp`](examples/ch05_orderings.cpp).

---

## 11. Summary

<!--diagram
title: Memory orderings summary
box[gray] relaxed
  text: Atomicity + coherence only. No cross-var ordering
box[teal] acquire
  text: Load barrier; nothing after moves before; pulls in the releaser's writes
box[teal] release
  text: Store barrier; nothing before moves after; publishes
box[blue] acq_rel
  text: Both, for RMW
box[green] seq_cst
  text: acquire+release + single **GLOBAL** total order (**DEFAULT**)
box[red] consume
  text: Avoid; use `acquire`
box[green] Defaults
  text: loads `acquire`, stores `release`, RMW `acq_rel`
  text: Default to `seq_cst`; weaken only with proof + profiling
-->

```
 +------------------------------------------------------------------+
 | relaxed : atomicity + coherence only. No cross-var ordering.     |
 | acquire : load barrier; nothing after moves before; pulls in     |
 |           the releaser's writes.                                 |
 | release : store barrier; nothing before moves after; publishes.  |
 | acq_rel : both, for RMW.                                         |
 | seq_cst : acquire+release + single GLOBAL total order (DEFAULT). |
 | consume : avoid; use acquire.                                    |
 |                                                                  |
 | loads acquire, stores release, RMW acq_rel.                      |
 | Default to seq_cst; weaken only with proof + profiling.          |
 +------------------------------------------------------------------+
```

Next: [06-acquire-release.md](06-acquire-release.md).
