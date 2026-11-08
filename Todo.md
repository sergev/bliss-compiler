# Todo

Unsupported BLISS features, in order of implementation. Each task ends with
a test in `tests/language/` and an update to `docs/Bliss_Manual.md`.

## T12. Floating-point arithmetic and comparison builtins

- `BUILTIN` names (UM 4) `ADDx`, `SUBx`, `MULx`, `DIVx`, `CMPx` for
  x = F, D, G; all operands are addresses (VAX order: `SUBx (sub, min,
  dst)`, `DIVx (divisor, dividend, quot)`); `CMPx` returns -1, 0, 1.
- Generator in `llvm_execfuncgen.c`: load, compute, store.
- Test: each operation on F and D values.

## T13. Floating-point conversions

- `CVTxy (srca, dsta)` for F, D, G and longword integers (L, I):
  CVTFD, CVTDF, CVTFG, CVTGF, CVTxL, CVTxI, CVTLx, CVTIx, rounded CVTRxL.
- Conversions that can overflow (to integer, narrowing) return 1, or 0
  on overflow; others NOVALUE. Truncate with `llvm.fptosi.sat`, round
  half away from zero with `llvm.round`.
- Test: conversions both ways, rounding, overflow status.

## T14. H-floating

- `%H'...[Q exp]'` as 16 bytes (only in PLIT/INITIAL, more than a
  fullword); H = LLVM `fp128` (soft-float in libgcc).
- `ADDH` ... `CMPH`, `CVTFH`, `CVTHF`, `CVTGH`, `CVTHG`, `CVTHL`, `CVTLH`,
  `CVTRHL`.
- Test: arithmetic and conversions through H.
