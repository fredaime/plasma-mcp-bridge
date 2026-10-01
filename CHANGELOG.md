# Changelog

## 0.2.0 (2026-10-02)

### Fixed
- A reply containing a string or byte array nested in an array or struct
  (`aas`, `(as)`, `aay`) no longer hangs the server in an infinite loop; such
  values in maps (`a{sas}`, `a{say}`) are no longer replaced by `""`.
- Map keys of type object path or signature (`a{oas}`, `a{gas}`) are kept
  instead of becoming `""`.
- Numbers in replies are exact (`123456789`, `3.14159265`), not rounded to six
  significant digits.
- A method without return value replies `null` (was `<unrepresentable:>` when
  `interface` was given).
- `interface: "org.freedesktop.DBus"` works for calls to the bus daemon.
- Arguments `u`, `y`, `n`, `o`, … are sent with the declared type even when
  `interface` is omitted (method name unique on the object).
- The server no longer blocks during a slow call: `ping` and `tools/list` are
  answered at once, tool calls run concurrently.
- A client closing stdout no longer kills the bridge with `SIGPIPE`.
- Invalid frames get a JSON-RPC error (`-32700`, `-32600`) instead of being
  dropped with a misleading log line.

- Container arguments (`au`, `a{ss}`, `(si)`, `a(si)`, `aas`, `a{sa{sv}}`, …)
  and variants are sent with the declared type instead of `av`/`a{sv}`;
  `Properties.Set` sends the property's declared type.
- A unix fd in a reply reads `"<unix fd: not transferable over MCP>"` (was
  `<unrepresentable:QDBusUnixFileDescriptor>`).

### Changed
- A reply element whose D-Bus type cannot be represented is replaced by
  `"<unsupported D-Bus type '<signature>'>"`; the rest of the reply is still
  decoded.
- Arguments of basic types, `as` and `ay` are converted strictly: out-of-range
  or non-integral integers (e.g. `-1` for `u`, `300` for `y`, `2.7` for `n`) and
  wrong JSON types now fail with `argument <n> (<type>): …` instead of being
  wrapped, rounded or sent as another type. Integers beyond the int64 range
  must be passed as decimal strings; `ay` accepts base64.
- A `uint64` above 2^53 is returned as a decimal string.
- The system bus is refused unless the bridge is started with
  `--allow-system-bus` (all three D-Bus tools).
- `dbus_call` refuses a built-in denylist of destructive methods (logind power
  and session methods, systemd unit start/stop/kill/environment, KWin scripting,
  plasmashell `evaluateScript`, ksmserver `closeSession`, `org.kde.Shutdown`,
  `UpdateActivationEnvironment`) and destinations given by unique name
  (`:N.M`); a rule also applies through the other names of the connection
  that owns the name. See README, "Built-in guard rails". This is not a
  security boundary.
- An unknown `bus` value (e.g. `"sytem"`) is an error instead of silently
  meaning the session bus.
- `dbus_call` rejects service, path, interface or method names with characters
  D-Bus does not allow.
- A `--plugin` that cannot be loaded makes the bridge exit with code 2 (also
  with `--emit-skill`).
- Replies to `tools/call` may arrive out of order (they carry the request id).
- `initialize` negotiates `2024-11-05`, `2025-06-18` or `2025-11-25` (was
  always `2024-11-05`).
- A tool name registered twice is refused (the first registration wins).
- Plugin tools run on a dedicated worker thread, one at a time.
- On stdin EOF the bridge writes the results that arrive within 2 s, then
  exits with code 0 even if a call is still running.

- An integer in a variant, or in an argument whose type is unknown, is sent as
  `i` when it fits in 32 bits (was always `x`).
- An `int64` beyond ±2^53 is returned as a decimal string, like a `uint64`.

### Added
- Test-suite on a private D-Bus (`ctest`), see README.
- `--deny`, `--allow`, `--default-deny`, `--allow-unique-names`; one audit line
  per `dbus_call` on stderr.
- `notifications/cancelled`; `--call-timeout-ms` and a per-call `timeout_ms`
  argument to `dbus_call`; `DBusBridge::callMethod(…, int timeoutMs)`.
- `{"@dbus": "<type>", "value": …}` forces the type of a variant argument.
- MCP 2026-07-28, dual-era: a request carrying
  `_meta["io.modelcontextprotocol/protocolVersion"]` is served statelessly
  (`server/discover`, `tools/list`, `tools/call`, minimal `subscriptions/listen`;
  `-32022` for an unsupported version) beside the `initialize` lifecycle of
  2024-11-05, 2025-06-18 and 2025-11-25.

### Removed
- The D-Bus activation file `org.kde.plasma.mcpbridge.service`: activating the
  bridge started a stdio server with no client. Packagers: drop it from file
  lists.
