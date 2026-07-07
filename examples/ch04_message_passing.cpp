// ch04 — release/acquire message passing publishes non-atomic data safely.
// build: clang++ -std=c++20 -O2 -pthread ch04_message_passing.cpp -o /tmp/d && /tmp/d
// Try under TSan too (should be CLEAN because it's correctly synchronized):
//   clang++ -std=c++20 -O1 -g -fsanitize=thread -pthread ch04_message_passing.cpp -o /tmp/d && /tmp/d
#include <atomic>
#include <thread>
#include <cassert>
#include <iostream>

int payload = 0;                 // plain int (non-atomic)
std::atomic<bool> ready{false};

void producer() {
    payload = 99;                                    // (1)
    ready.store(true, std::memory_order_release);    // (2) publish
}

void consumer() {
    while (!ready.load(std::memory_order_acquire))   // (3) wait
        std::this_thread::yield();
    assert(payload == 99);                           // (4) never fires
    std::cout << "consumer saw payload = " << payload << '\n';
}

int main() {
    for (int trial = 0; trial < 1000; ++trial) {
        payload = 0;
        ready.store(false);
        std::thread c(consumer);
        std::thread p(producer);
        p.join();
        c.join();
    }
    std::cout << "1000 trials passed (no torn/stale reads)\n";
}
