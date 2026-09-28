// Close-callback semantics: every upgraded connection ends with exactly one
// close callback, after any error callback describing the cause.
//
//   test_close URL disconnect   client-initiated close   -> 1000, no error
//   test_close URL nopong       peer ignores pings       -> PING_TIMEOUT, then 1006
//   test_close URL drop         peer drops TCP, no CLOSE -> IO error, then 1006
#include "WebSocketClient.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

using ErrorCode = WebSocketClient::ErrorCode;

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s URL disconnect|nopong|drop\n", argv[0]); return 2; }
    const std::string mode = argv[2];

    std::atomic<bool> open{false};
    std::atomic<int> closes{0};
    std::atomic<int> close_code{0};
    std::atomic<int> errors{0};
    std::atomic<int> first_error{0};
    std::atomic<bool> error_before_close{false};

    WebSocketClient client;
    client.setConnectionTimeout(5);
    if (mode == "nopong") client.setPingInterval(1);

    client.setOpenCallback([&] {
        open = true;
        if (mode == "drop") client.sendMessage("trigger-drop");
    });
    client.setCloseCallback([&](int code, const std::string&) {
        if (closes++ == 0) {
            close_code = code;
            error_before_close = errors.load() > 0;
        }
    });
    client.setErrorCallback([&](int code, const std::string& msg) {
        std::printf("error %d: %s\n", code, msg.c_str());
        if (errors++ == 0) first_error = code;
    });
    client.setUrl(argv[1]);
    client.connect();

    for (int i = 0; i < 500 && !open; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!open) { std::fprintf(stderr, "FAIL: no open\n"); return 1; }

    if (mode == "disconnect") client.disconnect();

    // Ping timeout needs ~3 intervals; allow headroom, then linger to catch a
    // second (duplicate) close callback.
    for (int i = 0; i < 1000 && closes.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    client.disconnect();

    bool ok = true;
    auto fail = [&](const char* why) { std::fprintf(stderr, "FAIL: %s\n", why); ok = false; };

    if (closes.load() != 1) fail("expected exactly one close callback");
    if (mode == "disconnect") {
        if (close_code.load() != 1000) fail("expected close code 1000");
        if (errors.load() != 0) fail("unexpected error callback");
    } else {
        if (close_code.load() != 1006) fail("expected close code 1006");
        if (!error_before_close.load()) fail("expected error callback before close");
        const int want = mode == "nopong" ? int(ErrorCode::PING_TIMEOUT) : int(ErrorCode::IO);
        if (first_error.load() != want) fail("unexpected error code");
    }
    std::printf("%s close[%s] closes=%d code=%d errors=%d\n", ok ? "PASS" : "FAIL",
                mode.c_str(), closes.load(), close_code.load(), errors.load());
    return ok ? 0 : 1;
}
