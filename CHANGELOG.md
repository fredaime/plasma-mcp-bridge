# Changelog

## 0.2.0 (unreleased)

Pre-releases are versioned 0.1.90+.

### Fixed
- A reply containing a string or byte array nested in an array or struct
  (`aas`, `(as)`, `aay`) no longer hangs the server in an infinite loop; such
  values in maps (`a{sas}`, `a{say}`) are no longer replaced by `""`.
- Map keys of type object path or signature (`a{oas}`, `a{gas}`) are kept
  instead of becoming `""`.

### Changed
- A reply element whose D-Bus type cannot be represented is replaced by
  `"<unsupported D-Bus type '<signature>'>"`; the rest of the reply is still
  decoded.

### Added
- Test-suite on a private D-Bus (`ctest`), see README.
