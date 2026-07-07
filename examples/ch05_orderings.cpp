// ch05 — same message-passing skeleton, correct with release/acquire.
// Demonstrates that seq_cst also works (just stronger/costlier).
// build: clang++ -std=c++20 -O2 -pthread ch05_orderings.cpp -o /tmp/d && /tmp/d
#include <atomic>
#include <thread>
#include <iostream>
#include <cassert>

template <std::memory_order StoreOrder, std::memory_order LoadOrder>
void run_trial() {
    int data = 0;
    std::atomic<int> flag{0};

    std::thread consumer([&] {
        while (flag.load(LoadOrder) == 0) std::this_thread::yield();
        assert(data == 42);
    });
    std::thread producer([&] {
        data = 42;
        flag.store(1, StoreOrder);
    });
    producer.join();
    consumer.join();
}

int main() {
    for (int i = 0; i < 500; ++i)
        run_trial<std::memory_order_release, std::memory_order_acquire>();
    std::cout << "release/acquire: OK\n";

    for (int i = 0; i < 500; ++i)
        run_trial<std::memory_order_seq_cst, std::memory_order_seq_cst>();
    std::cout << "seq_cst: OK\n";
    // NOTE: relaxed/relaxed here would be a DATA RACE on 'data' (UB) — omitted.
}
