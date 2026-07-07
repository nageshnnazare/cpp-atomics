# 08 — `seq_cst` (The Safe Default) & `consume` (The Cautionary Tale)

Two orderings at opposite ends of the "should I use this?" spectrum:
`seq_cst` — the strong, correct default you should reach for first; and
`consume` — a clever idea that never worked out and is now discouraged.

---

## Part A: `memory_order_seq_cst`

### 1. What it adds

```
   seq_cst = acquire (on loads) + release (on stores) + acq_rel (on RMW)
             PLUS a single GLOBAL TOTAL ORDER S over ALL seq_cst operations
             that every thread agrees on.
```

```
   With everything seq_cst, you may reason as if there is ONE interleaving of
   all operations, consistent with each thread's program order — the intuitive
   "shared memory" model from chapter 00. This is the easiest to reason about.
```

### 2. It's the default (on purpose)

```cpp
std::atomic<int> x{0};
x.store(1);          // == x.store(1, std::memory_order_seq_cst)
int v = x.load();    // == x.load(std::memory_order_seq_cst)
++x;                 // seq_cst RMW
```

The committee made `seq_cst` the default because it's the **least surprising**
and the hardest to get subtly wrong. Start here; weaken only with proof.

### 3. What it prevents that acquire/release does not

The Store-Buffer litmus (chapters 02, 06): with acquire/release, both threads can
read 0. With seq_cst, they cannot.

```cpp
std::atomic<int> x{0}, y{0};
int r1, r2;

// Thread A                         // Thread B
x.store(1, seq_cst);                y.store(1, seq_cst);
r1 = y.load(seq_cst);               r2 = x.load(seq_cst);
```

```
   Global order S must place the four ops in SOME consistent total order.
   In every such order, at least one load comes after both stores.
   => r1==0 && r2==0 is IMPOSSIBLE with seq_cst.
   (With relaxed/acq_rel it IS possible — this is the key difference.)
```

```
   Any valid S, e.g.:  x.store, y.store, r1=y(=1), r2=x(=1)   -> not both 0
   There's no S where both loads precede both stores while respecting program
   order. seq_cst forbids the "both 0" outcome.
```

### 4. When you actually NEED seq_cst

```
   * Mutual-exclusion algorithms (Dekker, Peterson): "if I don't see your flag,
     you must see mine" requires a global order.
   * Any invariant of the form "at least one of several threads observes the
     other's write" across DIFFERENT variables.
   * When you're not sure. Correct-and-maybe-slower beats fast-and-wrong.
```

### 5. The cost

```
   x86: seq_cst LOADS are cheap (plain mov); seq_cst STORES need an
        xchg / mfence (the StoreLoad barrier). So stores are the expensive part.
   ARM/POWER: seq_cst needs stronger barriers (e.g. dmb ish / sync) on both
        sides -> noticeably pricier than acquire/release.
   -> If a seq_cst atomic is hot AND you can prove acquire/release suffices,
      that's the main win. Otherwise keep seq_cst.
```

### 6. A subtlety: mixing seq_cst with weaker orders

```
   The global order S only governs seq_cst operations. If you mix a relaxed or
   acquire/release op into the mix, it does NOT participate in S, and the strong
   guarantees can be lost. For the "at least one sees the other" reasoning to
   hold, ALL participating operations must be seq_cst. Don't half-do it.
```

---

## Part B: `memory_order_consume` (avoid it)

### 7. The idea (dependency-ordered-before)

`consume` was meant to be a *cheaper acquire* for the common case where you only
need ordering along **data dependencies** (following a pointer you just loaded),
which some CPUs (notably ARM/POWER) provide for free.

```cpp
std::atomic<Node*> head;

Node* p = head.load(std::memory_order_consume);  // intended: cheaper than acquire
int v = p->data;   // 'p->data' DEPENDS on p, so ordering was "free" on HW
```

```
   Intended guarantee: only operations that DEPEND on the consumed value are
   ordered (via "carries-a-dependency"), not ALL later operations (as acquire
   does). Cheaper on weakly-ordered CPUs for pointer-chasing.
```

### 8. Why it failed

```
   * The "dependency chain" rules are extremely hard to specify and preserve
     through optimizers (a compiler can break a dependency, e.g. via 'p - p + q'
     or value speculation).
   * NO mainstream compiler actually implements consume as designed. They all
     silently PROMOTE it to acquire.
   * The standard (since C++17) explicitly discourages it; it's under active
     redesign ([[carries_dependency]] / std::kill_dependency were part of the
     failed machinery).
```

```
   Reality today:
     memory_order_consume  ---(compiler)--->  treated as memory_order_acquire
   You pay acquire cost anyway, plus you've written fragile, confusing code.
```

### 9. What to do instead

```
   * Use memory_order_acquire. It's correct, well-supported, and only marginally
     more expensive than consume would have been on the few CPUs that cared.
   * If you're doing pointer-publishing (the consume use case), acquire is the
     right tool (chapter 06 §4).
```

---

## 10. Choosing between seq_cst and acquire/release (recap)

```
   Need a single GLOBAL order across multiple atomics?      -> seq_cst
   (mutual exclusion, "at least one sees the other")

   Just publishing/consuming data via one flag/pointer?     -> release/acquire
   (producer/consumer, ownership handoff, refcount)

   Only a standalone counter/flag value?                     -> relaxed (ch 07)

   Considering consume?                                       -> use acquire.

   Not sure?                                                  -> seq_cst.
```

---

## 11. Summary

<!--diagram
title: seq_cst and consume summary
box[green] seq_cst
  text: acquire+release + **ONE** global total order S over all seq_cst ops. The **DEFAULT** and the safe choice
  text: Only order that forbids the Store-Buffer "both read 0" outcome. Needed for mutual-exclusion / "at least one sees the other" logic
  text: Cost: pricier stores (x86) / both sides (ARM)
box[red] consume
  text: A well-intentioned cheaper-acquire that no compiler implements (promoted to acquire), hard to use correctly, and discouraged. Just use `acquire`
-->

```
 +------------------------------------------------------------------+
 | seq_cst: acquire+release + ONE global total order S over all     |
 |   seq_cst ops. The DEFAULT and the safe choice. Only order that  |
 |   forbids the Store-Buffer "both read 0" outcome. Needed for     |
 |   mutual-exclusion / "at least one sees the other" logic.        |
 |   Cost: pricier stores (x86) / both sides (ARM).                |
 |                                                                  |
 | consume: a well-intentioned cheaper-acquire that no compiler     |
 |   implements (promoted to acquire), hard to use correctly, and   |
 |   discouraged. Just use acquire.                                |
 +------------------------------------------------------------------+
```

Next: [09-fences.md](09-fences.md).
