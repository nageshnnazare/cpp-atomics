// ch10 — lock-free Treiber stack push via CAS loop.
// NOTE: pop() here LEAKS nodes on purpose (safe reclamation is chapter 11's
// topic). This demonstrates the CAS mechanics, not production reclamation.
// build: clang++ -std=c++20 -O2 -pthread ch10_cas_stack.cpp -o /tmp/d && /tmp/d
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>

template <class T>
class LockFreeStack {
    struct Node { T value; Node* next; };
    std::atomic<Node*> head_{nullptr};
public:
    void push(T v) {
        Node* n = new Node{v, head_.load(std::memory_order_relaxed)};
        while (!head_.compare_exchange_weak(n->next, n,
                   std::memory_order_release,
                   std::memory_order_relaxed)) {}
    }
    bool pop(T& out) {
        Node* old = head_.load(std::memory_order_acquire);
        while (old && !head_.compare_exchange_weak(old, old->next,
                   std::memory_order_acquire,
                   std::memory_order_relaxed)) {}
        if (!old) return false;
        out = old->value;
        // Intentionally NOT deleting 'old' — see chapter 11 (reclamation/ABA).
        return true;
    }
};

int main() {
    LockFreeStack<int> stack;
    constexpr int THREADS = 4, PER = 10000;

    std::vector<std::thread> pushers;
    for (int i = 0; i < THREADS; ++i)
        pushers.emplace_back([&] { for (int j = 0; j < PER; ++j) stack.push(j); });
    for (auto& t : pushers) t.join();

    std::atomic<long> popped{0};
    std::vector<std::thread> poppers;
    for (int i = 0; i < THREADS; ++i)
        poppers.emplace_back([&] {
            int v;
            while (stack.pop(v)) popped.fetch_add(1, std::memory_order_relaxed);
        });
    for (auto& t : poppers) t.join();

    std::cout << "pushed " << (long)THREADS * PER
              << ", popped " << popped.load() << " (should match)\n";
}
