// ch06 — a shared_ptr-like refcount: relaxed increment, acq_rel decrement.
// build: clang++ -std=c++20 -O2 -pthread ch06_refcount.cpp -o /tmp/d && /tmp/d
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>

struct Object {
    int data = 12345;
    std::atomic<int> refcount{1};
};

void acquire_ref(Object* o) {
    o->refcount.fetch_add(1, std::memory_order_relaxed);   // counting only
}

void release_ref(Object*& o) {
    // acq_rel so the final deleter sees all prior uses of *o.
    if (o->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        // we dropped the last reference -> exclusive owner now.
        delete o;
        o = nullptr;
    }
}

int main() {
    Object* obj = new Object();      // refcount starts at 1

    constexpr int N = 8;
    std::vector<std::thread> ts;
    for (int i = 0; i < N; ++i) {
        acquire_ref(obj);            // +1 for each thread (main-thread safe here)
        ts.emplace_back([obj]() mutable {
            Object* local = obj;     // pretend each thread owns a ref
            volatile int x = local->data; (void)x;
            release_ref(local);      // each releases its ref
        });
    }
    for (auto& t : ts) t.join();

    release_ref(obj);                // release the original ref -> deletes
    std::cout << (obj == nullptr ? "object freed exactly once\n"
                                 : "LEAK/BUG\n");
}
