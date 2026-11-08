# Todo

Unsupported BLISS features, in order of implementation. Each task ends with
a test in `tests/language/` and an update to `docs/Bliss_Manual.md`.

## T14. H-floating

- `%H'...[Q exp]'` as 16 bytes (only in PLIT/INITIAL, more than a
  fullword); H = LLVM `fp128` (soft-float in libgcc).
- `ADDH` ... `CMPH`, `CVTFH`, `CVTHF`, `CVTGH`, `CVTHG`, `CVTHL`, `CVTLH`,
  `CVTRHL`.
- Test: arithmetic and conversions through H.
