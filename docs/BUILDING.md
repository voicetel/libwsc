## Install Prerequisites

* **Ubuntu/Debian**

  ```bash
  sudo apt-get update && sudo apt-get install \
    build-essential libevent-dev zlib1g-dev libssl-dev cmake
  ```

* **CentOS/RHEL/Fedora**

  ```bash
  sudo dnf install \
    @development-tools libevent-devel zlib-devel openssl-devel cmake
  ```

---

## Installation & CMake Integration

Shared builds are available via `-DBUILD_SHARED_LIBS=ON`, but static is the default.

- Supported flags
  - -DUSE_TLS=ON, **OFF** by default (TLS support)
  - -DLIBWSC_USE_DEBUG=ON, **OFF** by default (verbose debugging, logs to stdout|stderr or syslog)
  - -DBUILD_SHARED_LIBS=ON, **OFF** by default
  - -DLIBWSC_STRIP=OFF, **ON** by default (strip symbols from Release builds)
  - -DLIBWSC_BUILD_TESTS=ON, **OFF** by default (integration tests, see below)

The easiest way is to clone the repository and use it in your cmake project via `add_sudirectory()`. You can also build a shared library:

```bash
git clone git@github.com:amigniter/libwsc.git
cd libwsc
mkdir build && cd build

# Shared library:
cmake .. \
    -DCMAKE_BUILD_TYPE=Release  \
    -DUSE_TLS=ON                \
    -DLIBWSC_USE_DEBUG=ON       \
    -DBUILD_SHARED_LIBS=ON
make
sudo make install
```

### Integration

**In-tree**

```cmake
# Top-level CMakeLists.txt
set(USE_TLS ON CACHE BOOL "" FORCE)
add_subdirectory(path/to/libwsc)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE libwsc)
```

**Installed package**

```cmake
# After running install above:
find_package(libwsc REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE libwsc::libwsc)
```

---
---

## Running the tests

The integration tests drive the client against a small stdlib-only Python
RFC 6455 server (`tests/ws_test_server.py`); they need `python3` and a
loopback interface with IPv4 and IPv6.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DUSE_TLS=ON -DLIBWSC_BUILD_TESTS=ON
cmake --build build
tests/run_tests.sh build
```

| Test | Covers |
| --- | --- |
| `functional_v4`, `functional_v6` | text, binary, 1 MiB, fragmented echo, server-initiated close, frame masking/mask uniqueness; IPv6 literal URL and bracketed `Origin` |
| `stall` | messages sent from another thread are never stranded in the send queue |
| `close_disconnect` | client-initiated close: one close callback (1000), no error |
| `close_nopong` | peer ignores pings: `PING_TIMEOUT` error, then one close callback (1006) |
| `close_drop` | peer drops TCP without CLOSE: `IO` error, then one close callback (1006) |

For sanitizer runs, add e.g. `-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined
-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -DLIBWSC_STRIP=OFF`.
