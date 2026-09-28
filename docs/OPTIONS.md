## Additional Options

After setting up your URL and callbacks, you can tweak these behaviors before calling `connect()`:

- **Compression**  
  per-message deflate is enabled by default and offered in websocket handshake. To disable it use:

  ```cpp
  client.enableCompression(false);
  ```

- **Ping interval**  
  Disabled by default. When set (seconds), a ping is sent every interval and
  the peer must answer with a pong: after 2 consecutive unanswered pings
  (about 3 intervals of silence) the connection is treated as half-open and
  torn down — the error callback fires with `ErrorCode::PING_TIMEOUT`,
  followed by the close callback with code 1006. Only enable it against
  servers that answer pings (RFC 6455 requires them to).

  ```cpp
  client.setPingInterval(3);
  ```

- **Connection timeout**  
  Default is 1s, covering DNS resolution, TCP connect, the TLS handshake and
  the WebSocket upgrade together. Set it explicitly for production,
  especially for `wss://` or remote endpoints:

  ```cpp
  client.setConnectionTimeout(3);
  ```

- **Custom HTTP Headers**  
  Add or override any handshake headers:

  ```cpp
  WebSocketHeaders hdrs;
  hdrs.set("X-My-Header", "Value");
  hdrs.set("User-Agent", "libwsc/1.0.0");
  client.setHeaders(hdrs);
  ```

- **TLS Settings**  
  Provide certificates, cipher suites, and peer-verification options via `WebSocketTLSOptions`.
  Other options remain at their defaults.

  ```cpp
  WebSocketTLSOptions tls;
  tls.certFile                = "/path/to/client.crt";
  tls.keyFile                 = "/path/to/client.key";
  tls.caFile                  = "NONE";               // disable peer verification
  tls.ciphers                 = WebSocketTLSOptions::getDefaultCiphers();
  tls.disableHostnameValidation = true;
  client.setTLSOptions(tls);
  ```

  - Setting `tls.caFile = "NONE"` alone is enough to disable peer verification, and that all other fields will fall back to their defaults.

## Callback semantics

All callbacks run on the client's internal event thread.

- **Open** fires once, when the WebSocket upgrade succeeds.
- **Close** fires **exactly once for every connection that opened**:
  - with the peer's code (or 1000) when the close handshake completes,
    whichever side started it;
  - with **1006** (abnormal closure) when the connection ends any other way —
    ping timeout, the peer dropping TCP without a CLOSE frame, or a transport
    or TLS error. In these cases the error callback fires first, describing
    the cause.
- **Error** reports failures. A connection that never opened (bad URL,
  connect/TLS/handshake failure or timeout) gets only the error callback — no
  close callback.

Applications that clean up per-connection state should do so in the close
callback (for opened connections) and treat the error callback as
informational, so teardown doesn't run twice.

`disconnect()` blocks until the event thread has finished, and the close
callback may run during that call. Don't hold a lock in the thread calling
`disconnect()` that the close callback also takes.