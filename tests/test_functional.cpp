// Functional round trip: text, binary, 1 MiB, fragmented echo, server-initiated
// close. Also requires exactly one close callback. Server mode: echo.
#include "WebSocketClient.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static std::mutex mtx;
static std::condition_variable cv;
static bool done = false;
static std::atomic<bool> failed{false};

static void finish(bool fail, const char* why) {
    if (fail) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        failed = true;
    }
    {
        std::lock_guard<std::mutex> lk(mtx);
        done = true;
    }
    cv.notify_one();
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s URL\n", argv[0]); return 2; }

    WebSocketClient client;
    std::atomic<int> step{0};
    std::atomic<int> closes{0};

    const std::string text1 = "hello websocket";
    std::vector<uint8_t> bin1(4096);
    for (size_t i = 0; i < bin1.size(); ++i) bin1[i] = static_cast<uint8_t>((i * 31 + 7) & 0xFF);
    std::string big(1024 * 1024, 'x');
    for (size_t i = 0; i < big.size(); i += 4096) big[i] = static_cast<char>('A' + (i / 4096) % 26);

    client.setConnectionTimeout(5);
    client.setOpenCallback([&] { client.sendMessage(text1); });

    client.setMessageCallback([&](const std::string& msg) {
        switch (step.load()) {
        case 0:
            if (msg != text1) { finish(true, "text echo mismatch"); return; }
            step = 1;
            client.sendBinary(bin1.data(), bin1.size());
            break;
        case 2:
            if (msg != big) { finish(true, "1MB echo mismatch"); return; }
            step = 3;
            client.sendMessage("fragment-me");
            break;
        case 3:
            if (msg != "fragment-me") { finish(true, "fragmented echo mismatch"); return; }
            step = 4;
            client.sendMessage("close-me");
            break;
        default:
            finish(true, "unexpected text message");
        }
    });

    client.setBinaryCallback([&](const void* data, size_t len) {
        if (step.load() != 1) { finish(true, "unexpected binary message"); return; }
        if (len != bin1.size() || std::memcmp(data, bin1.data(), len) != 0) {
            finish(true, "binary echo mismatch");
            return;
        }
        step = 2;
        client.sendMessage(big);
    });

    client.setCloseCallback([&](int code, const std::string&) {
        closes++;
        if (step.load() != 4) { finish(true, "closed before all steps completed"); return; }
        if (code != 1000) { finish(true, "unexpected close code"); return; }
        finish(false, "");
    });

    client.setErrorCallback([&](int code, const std::string& msg) {
        std::fprintf(stderr, "error %d: %s\n", code, msg.c_str());
        finish(true, "error callback fired");
    });

    client.setUrl(argv[1]);
    client.connect();

    {
        std::unique_lock<std::mutex> lk(mtx);
        if (!cv.wait_for(lk, std::chrono::seconds(30), [] { return done; })) {
            std::fprintf(stderr, "FAIL: timeout\n");
            return 1;
        }
    }
    client.disconnect();

    if (closes.load() != 1) {
        std::fprintf(stderr, "FAIL: %d close callbacks (want 1)\n", closes.load());
        return 1;
    }
    if (failed) return 1;
    std::printf("PASS functional\n");
    return 0;
}
