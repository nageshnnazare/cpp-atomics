# 03 — `std::atomic<T>` Basics

Now the API. This chapter covers `std::atomic<T>`: how to declare it, the core
operations (load, store, exchange, RMW), and the practical properties
(`is_lock_free`, which types work). We use the **default** memory order
(`seq_cst`) throughout; orderings come in chapter 05.

Header: `#include <atomic>`.

---

## 1. Declaring and initializing

```cpp
#include <atomic>

std::atomic<int>   counter{0};      // brace-init (preferred)
std::atomic<bool>  ready{false};
std::atomic<long>  total = 0;       // also fine (C++17+)

std::atomic<int>   x;               // CAUTION: value is uninitialized (like int)
x = 0;                              // or use x{0} above

std::atomic<int> a{1};
// std::atomic<int> b = a;          // ERROR: atomics are NOT copyable/movable
```

```
   std::atomic<T> is NOT copyable or movable. It represents a memory location.
   To "copy" its value:  int v = a.load();  std::atomic<int> b{v};
```

Since C++20, `std::atomic<T>` is **default-initialized to T{}** (zero for
scalars). Before C++20 a default-constructed atomic held an indeterminate value —
always brace-initialize to be safe/portable.

---

## 2. The core operations

```cpp
std::atomic<int> a{0};

int v = a.load();          // atomic read              -> v
a.store(42);               // atomic write
int old = a.exchange(7);   // atomic swap: set 7, return previous value

// Read-Modify-Write (RMW) — indivisible:
a.fetch_add(5);            // a += 5, returns OLD value
a.fetch_sub(2);            // a -= 2
a.fetch_and(0xF);          // bitwise
a.fetch_or(0x1);
a.fetch_xor(0x2);

// Convenience operators (all atomic RMW):
++a;  a++;  --a;  a--;
a += 3;  a -= 1;  a &= 0xF;  a |= 1;  a ^= 2;

// compare-exchange (the universal primitive) — chapter 10:
int expected = 7;
bool ok = a.compare_exchange_strong(expected, 100);
```

```
   load()      : R          (returns the value)
   store(v)    : W          (returns nothing)
   exchange(v) : R+W        (returns old, writes new)
   fetch_add(n): R+M+W      (returns old, adds n)     <- atomic!
   ++a         : R+M+W      (returns new)
   compare_exchange: conditional R+M+W (the swiss-army knife)
```

**Important:** `fetch_add` etc. return the **old** value; `++a` returns the
**new** value (like normal C++), `a++` returns the old.

### Assignment and conversion shortcuts

```cpp
a = 5;              // == a.store(5)   (returns 5, the stored value, NOT a ref)
int y = a;          // == a.load()     (implicit conversion)
```

```
   Convenient, but note: 'a = a + 1;' is TWO separate atomic ops (a load then a
   store) — NOT atomic as a whole! Use ++a or fetch_add for atomic increment.
```

This is the single most common beginner mistake:

```cpp
a = a + 1;          // BUG: load, +1, store — another thread can interleave
++a;                // CORRECT: one atomic RMW
a.fetch_add(1);     // CORRECT: same thing, explicit
```

---

## 3. Which RMW ops exist for which types

```
   Type of atomic<T>          Available operations
   ------------------------   --------------------------------------------
   integral (int, long, ...)  load/store/exchange/CAS + fetch_add/sub/and/or/xor
   floating (float,double)    load/store/exchange/CAS + fetch_add/sub (C++20)
   pointer (T*)               load/store/exchange/CAS + fetch_add/sub (pointer
                              arithmetic, in units of T)
   bool                       load/store/exchange/CAS (no arithmetic)
   trivially-copyable struct  load/store/exchange/CAS only (no fetch_* — no
                              meaningful arithmetic)
```

```cpp
std::atomic<double> d{1.0};
d.fetch_add(0.5);           // C++20: atomic float add

int arr[10];
std::atomic<int*> p{arr};
p.fetch_add(3);             // advances by 3 ints (like arr+3), atomically
```

---

## 4. `std::atomic<T>` for your own structs

You can make any **trivially copyable** type atomic (no user-defined copy,
trivial layout). Only load/store/exchange/CAS are available — the atomic just
treats it as bytes.

```cpp
struct Point { float x, y; };            // trivially copyable
static_assert(std::is_trivially_copyable_v<Point>);

std::atomic<Point> ap{ {0.0f, 0.0f} };
ap.store({1.0f, 2.0f});                  // atomic 8-byte store
Point p = ap.load();                     // atomic read

// std::atomic<std::string> s;           // ERROR: string is NOT trivially copyable
```

```
   Rule: std::atomic<T> requires T to be TRIVIALLY COPYABLE.
   For non-trivial types (string, vector, shared_ptr's pointee) you need either
   std::atomic<std::shared_ptr<T>> (C++20, chapter 13) or a mutex.
```

If the struct is larger than the platform's atomic word (e.g. 16+ bytes), the
atomic may not be lock-free (§6) — it uses a hidden lock internally.

---

## 5. Common convenience types & flags

```cpp
std::atomic<int>       ai;     // and the aliases:
std::atomic_int        ai2;    // == std::atomic<int>
std::atomic_bool       ab;     // == std::atomic<bool>
std::atomic_size_t     as;     // etc. (atomic_llong, atomic_uint, ...)

// atomic_flag: the ONLY guaranteed-always-lock-free atomic type.
std::atomic_flag f = ATOMIC_FLAG_INIT;   // (pre-C++20 init)
std::atomic_flag f2{};                    // C++20: default false
bool was_set = f2.test_and_set();         // set to true, return PREVIOUS value
f2.clear();                               // set to false
// C++20 adds f2.test() to read without modifying.
```

`atomic_flag` is a minimal test-and-set primitive — the building block for a
spinlock (chapter 12). It has no `load()` before C++20 (only `test_and_set` /
`clear`); C++20 added `test()`, `wait()`, `notify_*`.

---

## 6. `is_lock_free` and `is_always_lock_free`

"Lock-free" means the atomic uses real hardware atomic instructions, not a hidden
mutex. Large/odd types may fall back to a lock.

```cpp
std::atomic<int>  ai;
std::atomic<Big>  ab;   // suppose sizeof(Big) == 64

bool r1 = ai.is_lock_free();                 // runtime check (almost always true)
constexpr bool r2 = std::atomic<int>::is_always_lock_free;  // compile-time!

static_assert(std::atomic<int>::is_always_lock_free,
              "need lock-free int atomics on this platform");
```

```
   is_lock_free()          : member, runtime bool ("is THIS one lock-free now?")
   is_always_lock_free     : static constexpr bool ("is this type ALWAYS lock-free
                             on this platform?") -> use in static_assert.

   Typical results:
     atomic<int>, atomic<void*>  -> lock-free (yes)
     atomic<bigstruct 24B>       -> often NOT lock-free (hidden lock)
     atomic_flag                 -> ALWAYS lock-free (guaranteed by standard)
```

Why care? A non-lock-free atomic is (a) slower and (b) **not safe to use in a
signal handler**, and defeats the purpose in lock-free algorithms.

---

## 7. Alignment & the ABI

For an atomic to be lock-free, it usually must be **naturally aligned** and no
larger than the CPU's atomic width (8 bytes commonly; 16 bytes with special
instructions). `std::atomic` handles alignment for you, but a **`std::atomic_ref`**
(chapter 13) over a misaligned plain object may not be lock-free.

```
   atomic<int64_t> on a 64-bit CPU: 8-byte aligned -> single LOCK-free op.
   A packed/misaligned 8-byte value: may need a lock or split -> not lock-free.
```

---

## 8. Worked example: a correct shared counter

```cpp
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>

std::atomic<long> counter{0};

void work() {
    for (int i = 0; i < 100000; ++i)
        counter.fetch_add(1, std::memory_order_relaxed);  // just counting
}

int main() {
    std::vector<std::thread> ts;
    for (int i = 0; i < 8; ++i) ts.emplace_back(work);
    for (auto& t : ts) t.join();
    std::cout << counter.load() << '\n';   // always 800000
}
```

```
   8 threads x 100000 increments = 800000, guaranteed exact.
   relaxed order is fine here: we only care about the FINAL count, not about
   ordering relative to OTHER data. (Why relaxed is safe here: chapter 07.)
```

Runnable: [`examples/ch03_counter.cpp`](examples/ch03_counter.cpp).

---

## 9. Summary

<!--diagram
title: Atomic basics summary
box[blue] std::atomic<T>
  text: `#include <atomic>`. `std::atomic<T>` = one atomic memory location
  text: Not copyable/movable. Brace-initialize it
box[teal] Operations
  text: Ops: load, store, exchange, fetch_add/sub/and/or/xor, ++/--/+=, compare_exchange (chapter 10)
  text: `fetch_*` / `a++` return the **OLD** value; `++a` returns the **NEW**
  text: `'a = a + 1'` is **TWO** ops (bug); use `++a` / `fetch_add`
box[green] Types & defaults
  text: T must be trivially copyable. `atomic_flag` = always lock-free
  text: Check `is_always_lock_free` (constexpr) with `static_assert`
  text: Default order is `seq_cst` (safe); orderings -> chapter 05
-->

```
 +------------------------------------------------------------------+
 | #include <atomic>. std::atomic<T> = one atomic memory location.  |
 | Not copyable/movable. Brace-initialize it.                       |
 |                                                                  |
 | Ops: load, store, exchange, fetch_add/sub/and/or/xor, ++/--/+=,  |
 |      compare_exchange (chapter 10).                              |
 | fetch_* / a++ return the OLD value; ++a returns the NEW.         |
 | 'a = a + 1' is TWO ops (bug); use ++a / fetch_add.               |
 |                                                                  |
 | T must be trivially copyable. atomic_flag = always lock-free.    |
 | Check is_always_lock_free (constexpr) with static_assert.        |
 | Default order is seq_cst (safe); orderings -> chapter 05.        |
 +------------------------------------------------------------------+
```

Next: [04-memory-model.md](04-memory-model.md).
