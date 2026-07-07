# 07 — Relaxed Ordering

`memory_order_relaxed` is the fastest and the most dangerous. It guarantees
**atomicity and per-object coherence** but **no ordering** relative to other
memory. This chapter shows the few cases where it's genuinely correct and the
many where it silently isn't.

---

## 1. What relaxed guarantees (and only this)

```
   GUARANTEES:
     * The operation is atomic (indivisible, no torn values).
     * Per-object coherence: all threads agree on the modification order of
       THIS ONE atomic; a thread never reads it "going backwards" (chapter 04).

   DOES NOT GUARANTEE:
     * Any ordering with respect to OTHER variables (atomic or not).
     * That other threads see this write "soon" (only eventually).
     * Any happens-before / publishing of surrounding data.
```

```
   Relaxed = "a thread-safe number." Nothing about the world AROUND it is ordered.
```

---

## 2. The poster child: a statistics counter

The one universally-safe relaxed use — you only care about the final total, not
about ordering it against other data.

```cpp
std::atomic<long> hits{0};

void on_request() {
    hits.fetch_add(1, std::memory_order_relaxed);   // just counting
}

// later, after all threads joined (join provides the ordering):
long total = hits.load(std::memory_order_relaxed);  // exact count
```

```
   Why safe:
     * Each fetch_add is atomic -> no lost updates (chapter 00).
     * We don't use 'hits' to guard other data, so ordering doesn't matter.
     * We read the total only AFTER join(), which itself creates happens-before.
   Result: exact count, minimal overhead. THIS is relaxed's sweet spot.
```

Runnable: [`examples/ch07_relaxed_counter.cpp`](examples/ch07_relaxed_counter.cpp).

---

## 3. When relaxed is WRONG: publishing data

If the atomic is meant to signal that *other* data is ready, relaxed breaks it.

```cpp
int data = 0;
std::atomic<bool> ready{false};

// Producer
data = 42;
ready.store(true, std::memory_order_relaxed);   // BUG

// Consumer
while (!ready.load(std::memory_order_relaxed)) {}
use(data);                                       // may see 0 / garbage -> UB
```

```
   No release/acquire -> no synchronizes-with -> no happens-before on 'data'.
   The consumer may observe ready==true while 'data' is still 0 (the two
   stores can reorder / not be visible together).
   -> DATA RACE on 'data' -> UNDEFINED BEHAVIOR.
   Fix: release on the store, acquire on the load (chapter 06).
```

---

## 4. The subtle trap: relaxed gives NO ordering across two atomics

Even between two *atomic* variables, relaxed provides no cross-variable ordering.

```cpp
std::atomic<int> x{0}, y{0};

// Thread A                         // Thread B
x.store(1, relaxed);                int b = y.load(relaxed);   // may see 1
y.store(1, relaxed);                int a = x.load(relaxed);   // may see 0!
```

```
   B can observe y==1 but x==0, even though A wrote x BEFORE y.
   Relaxed doesn't order x relative to y. Each variable is independently
   coherent, but there's no combined ordering.
   -> Never use relaxed when the RELATIVE order of two variables matters.
```

---

## 5. Relaxed IS fine for a single variable's own logic

Because of coherence, reasoning about **one** relaxed atomic in isolation is
sound:

```
   Safe relaxed uses (single-variable, value-only):
     * event/hit/error counters aggregated at the end
     * "generation"/sequence numbers used only for equality/monotonic checks
       on that same variable
     * setting a one-shot "shutdown requested" flag WHERE the reader does no
       ordered work depending on other data (rare; usually you DO -> use acquire)
     * an ID allocator: fetch_add to hand out unique IDs
```

```cpp
std::atomic<unsigned> next_id{0};
unsigned allocate_id() { return next_id.fetch_add(1, std::memory_order_relaxed); }
// Each caller gets a unique, never-repeated ID. No other data is ordered by it.
```

---

## 6. Relaxed + a later fence

A common efficient pattern: do many relaxed ops, then one fence to establish
ordering at a boundary (chapter 09):

```cpp
// accumulate with relaxed, then publish once:
buffer_filled.store(n, std::memory_order_relaxed);
// ... more relaxed writes ...
std::atomic_thread_fence(std::memory_order_release);  // one barrier
done.store(true, std::memory_order_relaxed);          // now acts like release
```

We cover standalone fences in chapter 09; the point here is relaxed ops can be
"upgraded" collectively by a fence rather than paying per-op ordering.

---

## 7. Performance reality check

```
   On x86: relaxed vs acquire/release loads/stores often compile to the SAME
   instruction (plain mov). The savings come mostly from the COMPILER being
   free to reorder — and from avoiding the seq_cst StoreLoad fence.

   On ARM/POWER: relaxed really is cheaper (no barrier instructions), so the
   difference is real. But so is the risk.

   -> Don't reach for relaxed on x86 "for speed"; you often gain nothing and
      risk portability bugs that only show up on ARM. Measure first.
```

---

## 8. Decision checklist for using relaxed

```
   Use relaxed ONLY if ALL are true:
     [ ] You care solely about this atomic's own value (a count/flag/id).
     [ ] No other (atomic or non-atomic) data's visibility depends on it.
     [ ] You don't rely on its ordering relative to any other variable.
     [ ] Any final read that needs consistency is after a join/lock/fence.
   If ANY box is unchecked -> use acquire/release (or seq_cst).
```

---

## 9. Worked contrast: correct vs broken

```cpp
// CORRECT relaxed: pure counter
std::atomic<long> counter{0};
void inc() { counter.fetch_add(1, std::memory_order_relaxed); }

// BROKEN relaxed: flag guarding data (needs release/acquire)
int payload; std::atomic<bool> flag{false};
void produce() { payload = 7; flag.store(true, std::memory_order_relaxed); } // BUG
void consume() { while(!flag.load(std::memory_order_relaxed)); use(payload);}// UB
```

Run [`examples/ch07_relaxed_counter.cpp`](examples/ch07_relaxed_counter.cpp)
(correct) and inspect the broken pattern under TSan to see the race.

---

## 10. Summary

<!--diagram
title: Relaxed ordering summary
box[gray] relaxed
  text: Atomicity + per-object coherence, **NOTHING ELSE**
box[green] GOOD
  text: Standalone counters, unique-id allocators, stats gathered after join — value-only, single-variable uses
box[red] BAD
  text: Publishing/consuming other data, ordering across two vars, any "flag means X is ready" scenario -> use acquire/release
box[orange] Portability
  text: On x86 the "speed win" is often zero; the portability **RISK** is real (bugs surface on ARM). Default away from relaxed
-->

```
 +------------------------------------------------------------------+
 | relaxed = atomicity + per-object coherence, NOTHING ELSE.        |
 |                                                                  |
 | GOOD: standalone counters, unique-id allocators, stats gathered  |
 |       after join — value-only, single-variable uses.            |
 | BAD:  publishing/consuming other data, ordering across two vars, |
 |       any "flag means X is ready" scenario -> use acquire/release.|
 |                                                                  |
 | On x86 the "speed win" is often zero; the portability RISK is    |
 | real (bugs surface on ARM). Default away from relaxed.          |
 +------------------------------------------------------------------+
```

Next: [08-seq-cst-and-consume.md](08-seq-cst-and-consume.md).
