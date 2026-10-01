# Changelog

## 0.2.0 (unreleased)

Pre-releases are versioned 0.1.90+.

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

### Added
- Test-suite on a private D-Bus (`ctest`), see README.
- `--deny`, `--allow`, `--default-deny`, `--allow-unique-names`; one audit line
  per `dbus_call` on stderr.

### Removed
- The D-Bus activation file `org.kde.plasma.mcpbridge.service`: activating the
  bridge started a stdio server with no client. Packagers: drop it from file
  lists.
