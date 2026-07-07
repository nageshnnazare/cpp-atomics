# 11 — Lock-Free Data Structures

Putting it together: real lock-free structures, their memory ordering, and the
hard parts (memory reclamation, ABA, progress guarantees). This chapter is where
atomics get genuinely difficult — the goal is to give you correct, minimal
examples and, crucially, an honest sense of the pitfalls.

> **Reality check:** writing correct, production lock-free structures is expert
> work. Prefer battle-tested libraries (e.g. `boost::lockfree`, `folly`, `moodycamel`).
> Learn the mechanics here so you can read them and know when a mutex is better.

---

## 1. Progress guarantees (the taxonomy)

```
   BLOCKING        : a stalled thread can block others indefinitely (mutex).
   OBSTRUCTION-FREE : a thread makes progress if it runs alone (weakest non-block).
   LOCK-FREE       : SOME thread always makes progress system-wide (no livelock),
                     but individual threads may starve. (CAS loops give this.)
   WAIT-FREE       : EVERY thread makes progress in bounded steps. (Hardest.)
```

```
   Guarantee strength:  blocking < obstruction-free < lock-free < wait-free
   "Lock-free" does NOT mean "no loops" or "always fast." It means the system
   as a whole can't be stuck because one thread was paused (no held locks).
```

---

## 2. SPSC ring buffer (single-producer, single-consumer)

The **easiest** and most useful lock-free queue. One thread pushes, one pops. No
CAS needed — just acquire/release on two indices.

```cpp
#include <atomic>
#include <vector>
#include <optional>

template <class T>
class SpscQueue {
    std::vector<T> buf_;
    const std::size_t cap_;
    std::atomic<std::size_t> head_{0};   // read index  (consumer writes)
    std::atomic<std::size_t> tail_{0};   // write index (producer writes)
public:
    explicit SpscQueue(std::size_t cap) : buf_(cap + 1), cap_(cap + 1) {}

    bool push(const T& item) {                       // producer only
        const auto tail = tail_.load(std::memory_order_relaxed);
        const auto next = (tail + 1) % cap_;
        if (next == head_.load(std::memory_order_acquire))
            return false;                            // full
        buf_[tail] = item;                           // write payload
        tail_.store(next, std::memory_order_release);// publish
        return true;
    }

    std::optional<T> pop() {                          // consumer only
        const auto head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire))
            return std::nullopt;                     // empty
        T item = buf_[head];                          // read payload
        head_.store((head + 1) % cap_, std::memory_order_release);
        return item;
    }
};
```

```
   Ring buffer (cap+1 slots; one slot kept empty to distinguish full/empty):

        head_                tail_
          v                    v
        [ _ ][ A ][ B ][ C ][ _ ][ _ ]
              ^^^^^^^^^^^^^ occupied ^
   producer advances tail_ (release) after writing buf_[tail].
   consumer sees new tail_ (acquire) -> the payload write is visible. No race.
   empty:  head_ == tail_        full: (tail_+1)%cap == head_
```

```
   Why no CAS: only ONE producer touches tail_, only ONE consumer touches head_.
   Each index has a single writer -> plain load/store with acquire/release
   suffices. This is why SPSC is the go-to lock-free queue: simple AND fast.
```

Runnable: [`examples/ch11_spsc_queue.cpp`](examples/ch11_spsc_queue.cpp).

---

## 3. Lock-free stack (Treiber stack) with CAS

Multiple producers/consumers. Uses the CAS loop from chapter 10. Correct for
**push**; **pop** has the ABA/reclamation problem.

```cpp
template <class T>
class LockFreeStack {
    struct Node { T value; Node* next; };
    std::atomic<Node*> head_{nullptr};
public:
    void push(T v) {
        Node* n = new Node{std::move(v), head_.load(std::memory_order_relaxed)};
        while (!head_.compare_exchange_weak(n->next, n,
                   std::memory_order_release,
                   std::memory_order_relaxed)) {}
    }

    bool pop(T& out) {
        Node* old = head_.load(std::memory_order_acquire);
        while (old &&
               !head_.compare_exchange_weak(old, old->next,
                   std::memory_order_acquire,
                   std::memory_order_relaxed)) {}
        if (!old) return false;
        out = std::move(old->value);
        // delete old;   // <-- DANGER: another thread may still be reading 'old'
                          //     (ABA / use-after-free). See reclamation below.
        return true;
    }
};
```

```
   push:  build node, CAS head: old_head -> new_node (release publishes node).
   pop:   read head, CAS head -> head->next (acquire to see node contents).

   THE HARD PART: when is it safe to `delete old`? Another thread that loaded
   'old' just before you popped may dereference old->next AFTER you free it.
   Naive delete -> use-after-free + ABA (chapter 10 §6).
```

---

## 4. Memory reclamation — the real difficulty

"How do I free a node when I can't be sure no one is reading it?" Three standard
answers:

```
   1. HAZARD POINTERS
      Each thread publishes (in a shared array) the pointers it's currently
      dereferencing ("hazards"). Before freeing a node, a reclaimer scans all
      hazard pointers; if none match, it's safe to delete; otherwise defer.

        thread's hazard slot:  [ ptr it is reading ]
        reclaimer: "is 'old' in ANY hazard slot? if yes -> retire list, retry
                    later; if no -> delete."

   2. EPOCH-BASED RECLAMATION (EBR) / RCU
      Global epoch counter; threads announce entry/exit of critical regions.
      Retired nodes are freed only after all threads have passed the epoch in
      which the node was unlinked (no reader could still hold it).

   3. REFERENCE COUNTING (atomic<shared_ptr>)
      Let a lock-free-ish atomic shared_ptr manage lifetime (chapter 13).
      Simpler to write; usually slower; may not be lock-free.
```

```
   Rule of thumb: if you find yourself needing hazard pointers / EBR, strongly
   consider whether a mutex-protected structure or an existing library is the
   better engineering choice. Reclamation is where lock-free bugs hide.
```

C++26 is adding **standard hazard pointers (`std::hazard_pointer`)** and **RCU
(`std::rcu_domain`)** to the library — see chapter 13 — precisely because rolling
your own is so error-prone.

---

## 5. MPMC queues (multi-producer, multi-consumer)

The general case is genuinely hard. The well-known correct design is the
**Michael-Scott queue** (linked list, CAS on head and tail with helping) and the
**bounded array queue with per-slot sequence numbers** (Vyukov). Both are subtle.

```
   Michael-Scott (sketch):
     enqueue: CAS tail->next = new node, then CAS tail = new node ("swing tail").
              Other threads "help" swing a lagging tail.
     dequeue: read head, help-advance tail if needed, CAS head = head->next.
     + a dummy head node to simplify empty/one-element cases.
     + reclamation scheme for freed nodes (hazard pointers / EBR).
```

```
   Recommendation: DO NOT hand-write an MPMC queue for production. Use:
     * moodycamel::ConcurrentQueue (fast, well-tested)
     * boost::lockfree::queue
     * folly::MPMCQueue
   Understand the design; don't reinvent the reclamation.
```

---

## 6. False sharing (a performance killer, chapter 12 too)

Even correct lock-free code can be slow if independent atomics share a cache
line. In the SPSC queue, put `head_` and `tail_` on **separate cache lines**:

```cpp
#include <new>   // std::hardware_destructive_interference_size

struct alignas(std::hardware_destructive_interference_size) PaddedIndex {
    std::atomic<std::size_t> v{0};
};
PaddedIndex head_, tail_;   // producer & consumer no longer fight over one line
```

```
   Without padding:                    With padding:
   line: [ head_ | tail_ | ... ]       line1: [ head_ | pad... ]
         producer & consumer            line2: [ tail_ | pad... ]
         invalidate each other          -> independent -> much faster
```

---

## 7. Choosing: lock-free vs mutex (be honest)

```
   Prefer a MUTEX (or std::queue + lock) unless:
     * profiling shows the lock is a real bottleneck, AND
     * the critical section is tiny, AND
     * you (or a library) can implement reclamation correctly.

   Lock-free wins for: very high contention on tiny ops, real-time/signal-safe
   contexts (no blocking), avoiding priority inversion.
   Lock-free costs: extreme difficulty, ABA/reclamation, hard-to-reproduce bugs,
     and often NOT faster under low/moderate contention.
```

<!--diagram
title: Lock-free vs mutex
box[green] Right question
  text: "Is it lock-free?" is the **wrong** first question
  text: "Is it **CORRECT** and fast **ENOUGH**?" is the right one
  text: A well-used mutex beats a broken lock-free structure always
-->

```
   +--------------------------------------------------------------+
   | "Is it lock-free?" is the wrong first question.               |
   | "Is it CORRECT and fast ENOUGH?" is the right one.            |
   | A well-used mutex beats a broken lock-free structure always.  |
   +--------------------------------------------------------------+
```

---

## 8. Summary

<!--diagram
title: Lock-free structures summary
box[blue] Progress ladder
  text: blocking < obstruction-free < lock-free < wait-free
box[green] SPSC ring buffer
  text: Single writer per index -> just acquire/release, no CAS. Easiest & fast. Pad head/tail to avoid false sharing
box[teal] Treiber stack
  text: CAS loop; push is easy, pop needs safe **RECLAMATION**
box[orange] Reclamation
  text: Hazard pointers / EBR-RCU / atomic `shared_ptr`. C++26 standardizes hazard pointers & RCU
box[purple] MPMC
  text: Michael-Scott/Vyukov: use a library, don't hand-roll
box[green] Default
  text: Default to a mutex; go lock-free only with proof + care
-->

```
 +------------------------------------------------------------------+
 | Progress: blocking < obstruction-free < lock-free < wait-free.   |
 | SPSC ring buffer: single writer per index -> just acquire/release,|
 |   no CAS. Easiest & fast. Pad head/tail to avoid false sharing.  |
 | Treiber stack: CAS loop; push is easy, pop needs safe RECLAMATION.|
 | Reclamation (the hard part): hazard pointers / EBR-RCU / atomic  |
 |   shared_ptr. C++26 standardizes hazard pointers & RCU.         |
 | MPMC (Michael-Scott/Vyukov): use a library, don't hand-roll.    |
 | Default to a mutex; go lock-free only with proof + care.        |
 +------------------------------------------------------------------+
```

Next: [12-patterns-and-pitfalls.md](12-patterns-and-pitfalls.md).
