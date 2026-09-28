// Regression test for stranded send-queue messages (v1.1.5 lost wakeup):
// send a small burst from a non-event thread, go idle, and require every echo
// to arrive without any further send to wake the loop. Server mode: echo.
#include "WebSocketClient.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s URL [bursts]\n", argv[0]); return 2; }
    const int bursts = argc > 2 ? std::atoi(argv[2]) : 300;

    std::atomic<int> received{0};
    std::atomic<bool> open{false};

    WebSocketClient client;
    client.setConnectionTimeout(5);
    client.setOpenCallback([&] { open = true; });
    client.setMessageCallback([&](const std::string&) { received++; });
    client.setErrorCallback([](int c, const std::string& m) {
        std::fprintf(stderr, "error %d: %s\n", c, m.c_str());
    });
    client.setUrl(argv[1]);
    client.connect();

    for (int i = 0; i < 500 && !open; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!open) { std::fprintf(stderr, "FAIL: no open\n"); return 1; }

    // The race needs a send to land while the event thread is mid-flush, i.e.
    // a few microseconds after the previous send woke it; busy-wait random
    // gaps in that range (sleep_for is far too coarse).
    auto spin = [](unsigned us) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(us);
        while (std::chrono::steady_clock::now() < until) {}
    };

    std::mt19937 rng(42);
    int sent = 0;
    for (int b = 0; b < bursts; ++b) {
        const int k = 2 + int(rng() % 7);
        for (int j = 0; j < k; ++j) {
            client.sendMessage("m" + std::to_string(sent));
            ++sent;
            spin(rng() % 40);
        }
        // Stay idle: nothing else will wake the event loop. The bug left
        // messages queued indefinitely; 2s is generous for a loaded CI runner.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (received.load() < sent && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        if (received.load() < sent) {
            std::fprintf(stderr, "FAIL: stranded message at burst %d: sent=%d received=%d\n",
                         b, sent, received.load());
            return 1;
        }
    }
    client.disconnect();
    std::printf("PASS stall (bursts=%d sent=%d)\n", bursts, sent);
    return 0;
}
