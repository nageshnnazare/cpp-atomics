// ch13 — C++20 atomic wait/notify (block instead of spin).
// build: clang++ -std=c++20 -O2 -pthread ch13_wait_notify.cpp -o /tmp/d && /tmp/d
#include <atomic>
#include <thread>
#include <iostream>
#include <chrono>

std::atomic<int> stage{0};
int payload = 0;

int main() {
    std::thread consumer([] {
        stage.wait(0, std::memory_order_acquire);   // sleeps until stage != 0
        std::cout << "consumer woke; payload = " << payload << '\n';  // 42
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    payload = 42;
    stage.store(1, std::memory_order_release);
    stage.notify_one();                              // wake the waiter

    consumer.join();
    std::cout << "done\n";
}
