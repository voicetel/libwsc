#!/usr/bin/env python3
"""Minimal RFC 6455 test server (stdlib only). Serves one connection, then exits.

usage: ws_test_server.py PORT [--v6] [--mode echo|nopong|drop]

  echo    echo data frames; "fragment-me" is echoed as 3 fragments and
          "close-me" makes the server send CLOSE 1000 (default)
  nopong  like echo, but never answer pings (half-open peer)
  drop    echo the first data message, then drop TCP without a CLOSE

Rejects unmasked client frames, prints the handshake Host/Origin, and exits 2
if client frame masks repeat.
"""
import argparse, base64, hashlib, socket, struct, sys

GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
masks = []


def recv_exact(conn, n):
    buf = b""
    while len(buf) < n:
        c = conn.recv(n - len(buf))
        if not c:
            raise ConnectionError("eof")
        buf += c
    return buf


def handshake(conn):
    req = b""
    while b"\r\n\r\n" not in req:
        c = conn.recv(4096)
        if not c:
            raise ConnectionError("eof during handshake")
        req += c
    headers = {}
    for line in req.split(b"\r\n")[1:]:
        if b":" in line:
            k, v = line.split(b":", 1)
            headers[k.strip().lower()] = v.strip()
    print("HANDSHAKE host=%s origin=%s" % (
        headers.get(b"host", b"").decode(), headers.get(b"origin", b"").decode()), flush=True)
    accept = base64.b64encode(hashlib.sha1(headers[b"sec-websocket-key"] + GUID).digest()).decode()
    conn.sendall((
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n" % accept).encode())


def recv_frame(conn):
    b1, b2 = recv_exact(conn, 2)
    fin, opcode = b1 & 0x80, b1 & 0x0F
    masked, ln = b2 & 0x80, b2 & 0x7F
    if ln == 126:
        ln = struct.unpack(">H", recv_exact(conn, 2))[0]
    elif ln == 127:
        ln = struct.unpack(">Q", recv_exact(conn, 8))[0]
    if not masked:
        raise ConnectionError("client frame not masked (RFC violation)")
    mask = recv_exact(conn, 4)
    masks.append(mask)
    payload = recv_exact(conn, ln) if ln else b""
    payload = bytes(b ^ mask[i & 3] for i, b in enumerate(payload))
    return fin, opcode, payload


def send_frame(conn, opcode, payload, fin=True):
    b1 = (0x80 if fin else 0) | opcode
    ln = len(payload)
    if ln <= 125:
        hdr = struct.pack("BB", b1, ln)
    elif ln <= 65535:
        hdr = struct.pack(">BBH", b1, 126, ln)
    else:
        hdr = struct.pack(">BBQ", b1, 127, ln)
    conn.sendall(hdr + payload)


def handle(conn, mode):
    handshake(conn)
    fragments = b""
    while True:
        fin, opcode, payload = recv_frame(conn)
        if opcode == 0x8:
            send_frame(conn, 0x8, payload)
            return
        if opcode == 0x9:
            if mode != "nopong":
                send_frame(conn, 0xA, payload)
            continue
        if opcode == 0xA:
            continue
        fragments += payload
        if not fin:
            continue
        msg, fragments = fragments, b""
        if msg == b"fragment-me":
            third = len(msg) // 3 or 1
            send_frame(conn, opcode, msg[:third], fin=False)
            send_frame(conn, 0x0, msg[third:2 * third], fin=False)
            send_frame(conn, 0x0, msg[2 * third:], fin=True)
        elif msg == b"close-me":
            send_frame(conn, 0x8, struct.pack(">H", 1000))
        else:
            send_frame(conn, opcode, msg)
            if mode == "drop":
                conn.shutdown(socket.SHUT_RDWR)
                raise ConnectionError("dropped by test server")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", type=int)
    ap.add_argument("--v6", action="store_true")
    ap.add_argument("--mode", default="echo", choices=["echo", "nopong", "drop"])
    args = ap.parse_args()

    srv = socket.socket(socket.AF_INET6 if args.v6 else socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("::1" if args.v6 else "127.0.0.1", args.port))
    srv.listen(1)
    srv.settimeout(60)
    print("listening on %d" % args.port, flush=True)
    conn, _ = srv.accept()
    conn.settimeout(60)
    try:
        handle(conn, args.mode)
    except (ConnectionError, OSError) as e:
        print("connection ended: %s" % e, flush=True)
    finally:
        conn.close()
    uniq = len(set(masks))
    print("frames received: %d, unique masks: %d" % (len(masks), uniq), flush=True)
    if len(masks) >= 4 and uniq < len(masks) * 0.9:
        print("MASK-REUSE-DETECTED", flush=True)
        sys.exit(2)


if __name__ == "__main__":
    main()
