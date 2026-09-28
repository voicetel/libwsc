#!/usr/bin/env bash
# Run the libwsc integration tests against the stdlib Python test server.
#
#   tests/run_tests.sh BUILD_DIR
#
# BUILD_DIR must be configured with -DLIBWSC_BUILD_TESTS=ON and built.
set -u

BIN="${1:?usage: $0 BUILD_DIR}/tests"
HERE="$(cd "$(dirname "$0")" && pwd)"
SERVER="$HERE/ws_test_server.py"
PORT=${LIBWSC_TEST_PORT:-19700}
FAILED=0
LOG="$(mktemp -d)"

# run NAME [--v6] [--mode M] -- CLIENT [ARGS...]   (URL is inserted as 1st arg)
run() {
    local name=$1; shift
    local srv_args=()
    while [ "$1" != "--" ]; do srv_args+=("$1"); shift; done
    shift
    local client=$1; shift
    PORT=$((PORT + 1))

    local host="127.0.0.1"
    [[ " ${srv_args[*]-} " == *" --v6 "* ]] && host="[::1]"
    local url="ws://$host:$PORT/test"

    python3 "$SERVER" "$PORT" "${srv_args[@]}" > "$LOG/$name.srv" 2>&1 &
    local srv=$!
    for _ in $(seq 50); do grep -q listening "$LOG/$name.srv" 2>/dev/null && break; sleep 0.1; done

    timeout 120 "$BIN/$client" "$url" "$@" > "$LOG/$name.out" 2>&1
    local rc=$?
    # The server exits once its single connection ends; don't hang if the
    # client never connected.
    for _ in $(seq 50); do kill -0 "$srv" 2>/dev/null || break; sleep 0.1; done
    kill "$srv" 2>/dev/null
    wait "$srv"; local src=$?

    local extra=""
    if [ -n "${EXPECT_ORIGIN:-}" ] && ! grep -qF "origin=$EXPECT_ORIGIN$PORT" "$LOG/$name.srv"; then
        extra="(bad Origin: $(grep HANDSHAKE "$LOG/$name.srv"))"
        rc=1
    fi
    if [ $rc -eq 0 ] && [ $src -eq 0 ]; then
        echo "ok   $name"
    else
        echo "FAIL $name (client rc=$rc, server rc=$src) $extra"
        sed 's/^/     client: /' "$LOG/$name.out"
        sed 's/^/     server: /' "$LOG/$name.srv"
        FAILED=1
    fi
}

run functional_v4                     -- test_functional
EXPECT_ORIGIN="http://[::1]:" \
run functional_v6   --v6              -- test_functional
run stall                             -- test_stall 500
run close_disconnect                  -- test_close disconnect
run close_nopong    --mode nopong     -- test_close nopong
run close_drop      --mode drop       -- test_close drop

rm -rf "$LOG"
[ $FAILED -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAILED
