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

### Added
- Test-suite on a private D-Bus (`ctest`), see README.
