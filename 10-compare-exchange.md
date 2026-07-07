# 10 — Compare-Exchange (CAS): The Universal Primitive

`compare_exchange` (CAS = Compare-And-Swap) is the most powerful atomic
operation. Every lock-free algorithm is built on it. It's how you do
"read-check-then-conditionally-write" **atomically**, closing the check-then-act
race from chapter 00.

---

## 1. What CAS does

```
   compare_exchange(expected, desired):
     atomically {
       if (*this == expected) { *this = desired; return true;  }
       else                   { expected = *this; return false; }
     }
   All of that happens as ONE indivisible step.
```

```
   Note the side effect on FAILURE: 'expected' is UPDATED to the current value.
   This is what makes the CAS retry loop work (you get the fresh value for free).
```

```cpp
std::atomic<int> a{10};
int expected = 10;
bool ok = a.compare_exchange_strong(expected, 20);
// if a was 10: a becomes 20, ok==true,  expected still 10
// if a was 15: a unchanged,   ok==false, expected updated to 15
```

---

## 2. `weak` vs `strong`

```cpp
a.compare_exchange_weak(expected, desired);
a.compare_exchange_strong(expected, desired);
```

```
   STRONG: fails ONLY if *this != expected. No false failures.
   WEAK:   may fail SPURIOUSLY even when *this == expected (returns false
           without swapping), on some architectures (LL/SC based: ARM/POWER).

   Why weak exists: on LL/SC CPUs, weak maps to a single load-linked/store-
   conditional pair (cheaper); strong needs a loop to hide spurious failures.
```

```
   Rule:
     * Inside a RETRY LOOP -> use WEAK (you're looping anyway; spurious failure
       just loops once more; it's cheaper per iteration).
     * For a SINGLE one-shot attempt (no loop) -> use STRONG (you don't want a
       spurious false).
```

---

## 3. The CAS loop (the fundamental idiom)

Most lock-free updates are "read current, compute new, try to install; retry if
someone beat me":

```cpp
std::atomic<int> value{0};

void atomic_multiply(int factor) {
    int current = value.load(std::memory_order_relaxed);
    int desired;
    do {
        desired = current * factor;                 // compute from snapshot
    } while (!value.compare_exchange_weak(current, desired,
                 std::memory_order_release,          // success order
                 std::memory_order_relaxed));        // failure order
    // On failure, 'current' was refreshed -> recompute and retry.
}
```

```
   +---------------------------------------------------------+
   | current = load()                                        |
   |   ┌──────────────────────────────────────────────┐     |
   |   │ desired = f(current)                          │     |
   |   │ CAS(current, desired) ?                        │     |
   |   │    success -> done                            │     |
   |   │    failure -> current now = latest; loop ─────┘     |
   +---------------------------------------------------------+
   Progress guarantee: at least one thread always succeeds each round
   (lock-free), because a failure means someone else made progress.
```

CAS can implement **any** RMW (even ones with no dedicated `fetch_*`), which is
why it's called universal.

---

## 4. The two memory orders of CAS

`compare_exchange` takes **two** orders: one for success, one for failure.

```cpp
bool compare_exchange_weak(T& expected, T desired,
                           memory_order success,
                           memory_order failure);
```

```
   success order: applied when the swap happens (it's an RMW -> acquire/release/
                  acq_rel/seq_cst all valid).
   failure order: applied when it doesn't swap -> it's just a LOAD, so only
                  relaxed/acquire/seq_cst are valid (NOT release/acq_rel).

   Common choices:
     success = acq_rel (or release), failure = relaxed   (typical CAS loop)
     success = acquire, failure = acquire                (when you read on fail)
```

```
   Constraint: 'failure' must be no stronger than 'success', and cannot be
   release/acq_rel (a failed CAS performs no store, so it can't release).
```

Single-argument forms default both to `seq_cst`:

```cpp
a.compare_exchange_strong(expected, desired);   // both seq_cst
```

---

## 5. Worked example: lock-free stack push

```cpp
struct Node { int value; Node* next; };
std::atomic<Node*> head{nullptr};

void push(int v) {
    Node* n = new Node{v, nullptr};
    n->next = head.load(std::memory_order_relaxed);
    while (!head.compare_exchange_weak(n->next, n,
               std::memory_order_release,     // publish n (and n->next writes)
               std::memory_order_relaxed)) {  // on fail, n->next refreshed
        // n->next was updated to the current head by the failed CAS; retry.
    }
}
```

```
   Thread A: n->next = head(=X);  CAS(head: X->n)
   If B pushed between load and CAS, head != X:
      CAS fails, sets n->next = head(new value), loop retries.
   Eventually CAS succeeds -> head points to n, n->next chains correctly.

        head ──▶ X ──▶ ...          becomes         head ──▶ n ──▶ X ──▶ ...
```

Release order publishes the fully-built node; a popping thread uses acquire.
Full stack (with the ABA caveat below) in chapter 11.

---

## 6. The ABA problem (CAS's classic trap)

CAS checks *value equality*, not "did it change." If a value goes A -> B -> A
between your load and your CAS, the CAS **succeeds** as if nothing changed — but
the world did change. This corrupts pointer-based structures.

```
   Lock-free stack pop, thread 1:
     old_head = head (points to node A, A->next = B)
     ... PREEMPTED ...
   Thread 2: pop A, pop B, push A again (reusing A's address)
             now head = A, but A->next is stale/freed!
   Thread 1 resumes:
     CAS(head: A -> A->next(=B))   // A is back, so CAS SUCCEEDS
     -> head now points to B, which was FREED. Corruption / use-after-free.
```

```
   Time:   head=A(->B)   [T1 read A]   [T2: pop A, pop B, free B, push A]
           head=A(->?)   [T1 CAS A->B] <-- B is gone! ABA struck.
```

### Fixes for ABA

```
   1. Tagged pointers / version counter: CAS a {pointer, counter} pair; bump the
      counter on every change so A-with-tag-1 != A-with-tag-2.
      (needs double-width CAS: compare_exchange on a 128-bit struct, or packed
       pointer+tag in 64 bits.)
   2. Hazard pointers: readers publish which nodes they're using; reclamation
      waits until no hazard points to a node (chapter 11).
   3. RCU / epoch-based reclamation: defer freeing until no reader can hold a
      reference.
   4. Don't reuse memory quickly (e.g. use a GC or a freelist with tags).
```

```cpp
// Sketch of a tagged pointer to defeat ABA:
struct TaggedPtr { Node* ptr; std::uintptr_t tag; };   // 16 bytes
std::atomic<TaggedPtr> head;   // may or may not be lock-free (check!)
// each successful update does {ptr=new, tag=old.tag+1}
```

ABA is *the* reason "I wrote a lock-free stack in an afternoon" code is usually
broken. Respect it.

---

## 7. Building `fetch_max` (something with no built-in)

CAS lets you implement atomic operations the standard doesn't provide:

```cpp
int atomic_fetch_max(std::atomic<int>& a, int v) {
    int cur = a.load(std::memory_order_relaxed);
    while (v > cur &&
           !a.compare_exchange_weak(cur, v,
                std::memory_order_relaxed)) {
        // cur refreshed on failure; loop condition re-checks v > cur
    }
    return cur;
}
```

```
   Keeps trying to raise 'a' to v, but only if v is larger. If another thread
   raised it past v meanwhile, 'v > cur' becomes false and we stop.
```

Runnable: [`examples/ch10_cas_stack.cpp`](examples/ch10_cas_stack.cpp),
[`examples/ch10_fetch_max.cpp`](examples/ch10_fetch_max.cpp).

---

## 8. Summary

<!--diagram
title: Compare-exchange summary
box[blue] CAS
  text: Atomically "if value==expected, set desired; else load into expected." Returns success bool. The universal RMW primitive
box[teal] weak vs strong
  text: **weak**: may fail spuriously -> use in **LOOPS** (cheaper)
  text: **strong**: no spurious fail -> use for **SINGLE** attempts
box[green] CAS loop
  text: Two orders: success (RMW) + failure (load; <= success, no release)
  text: load -> compute -> CAS(weak); retry on fail (expected auto-refreshed). Lock-free progress guaranteed
box[red] ABA problem
  text: Value returns to A after A->B->A; CAS wrongly succeeds. Fix with tagged pointers / hazard pointers / RCU
-->

```
 +------------------------------------------------------------------+
 | CAS: atomically "if value==expected, set desired; else load into  |
 | expected." Returns success bool. The universal RMW primitive.    |
 |                                                                  |
 | weak: may fail spuriously -> use in LOOPS (cheaper).            |
 | strong: no spurious fail -> use for SINGLE attempts.            |
 | Two orders: success (RMW) + failure (load; <= success, no release).|
 | CAS loop: load -> compute -> CAS(weak); retry on fail (expected  |
 |   auto-refreshed). Lock-free progress guaranteed.               |
 |                                                                  |
 | ABA PROBLEM: value returns to A after A->B->A; CAS wrongly       |
 |   succeeds. Fix with tagged pointers / hazard pointers / RCU.    |
 +------------------------------------------------------------------+
```

Next: [11-lock-free-structures.md](11-lock-free-structures.md).
