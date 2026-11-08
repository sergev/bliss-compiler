# Todo

Unsupported BLISS features, in order of implementation. Each task ends with
a test in `tests/language/` and an update to `docs/Bliss_Manual.md`.

## T9. INTERRUPT linkage type

In the LRM, INTERRUPT is a linkage-type, not a routine
attribute (BLISS-16 13.2.4.1, BLISS-32 13.3.4.2; BLISS-36 has
PS_INTERRUPT); a routine gets it through `LINKAGE l = INTERRUPT;` and
`ROUTINE r : l`. Such a routine must not be called directly.

- Accept `INTERRUPT` as a linkage-type (`declare_linkage`); map it to the LLVM
  `x86_intrcc` calling convention. Reject direct calls to it.
- Test: compile-only check of the emitted calling convention (`!! cout`
  or object inspection).

## T10. %ASCID string descriptors

- Emit a static descriptor (length word, type, class, pointer) followed by
  the string, as a PLIT-like constant (`lexeme.c`/`execfuncs.c` for the
  lexical part, `llvm_symgen.c` for data).
- Choose the descriptor layout for 64-bit (length 16 bits, pointer 64).
- Test: fetch length and characters through the descriptor.

## T11. Floating point

Largest front-end change; lowest priority.

- Literals (LRM 4.2): `%E'mantissa[E exp]'` single, `%D'...[D exp]'`
  double, `%G'...[G exp]'` extended-exponent double (BLISS-32/36),
  `%H'...[Q exp]'` extended precision (BLISS-32). Add to `lexeme.c` as a
  new lexeme type. Map F to IEEE float, D and G to double, H to fp128 or
  reject it.
- BLISS has no float operators. Arithmetic is done with machine-specific
  builtins (LRM Appendix D, BLISS-32 list): ADDF/D/G/H, SUBx, MULx, DIVx,
  CMPx; conversions CVTFD, CVTDF, CVTFG, CVTGF, CVTFH, CVTHF, CVTxI/CVTIx
  (F, D, G), CVTxL/CVTLx (F, D, G, H), and rounded CVTRFL, CVTRDL,
  CVTRGL, CVTRHL, CVTRDH, CVTRGH. Declared with `BUILTIN`.
- Signatures (BLISS-32 User Manual AA-H322E-TE, chapter 4): all operands
  are addresses, even short source values (`%REF (.x)` or `UPLIT (5)`).
  Operand order follows VAX instructions:
  - `ADDx (src1a, src2a, dsta)`, `MULx` likewise: NOVALUE.
  - `SUBx (subtrahend, minuend, dsta)`: dst = minuend - subtrahend.
  - `DIVx (divisor, dividend, quot)`: quot = dividend / divisor.
  - `CMPx (src1a, src2a)`: -1, 0 or 1.
  - `CVTxy (srca, dsta)`: returns 1, or 0 on overflow, when the target
    can overflow (to integer/long: CVTFI, CVTFL, CVTRFL, ...; and the
    narrowing CVTDF); NOVALUE otherwise (CVTLF, CVTIF, CVTFD, CVTGF, ...).
- Add them to `llvm_builtins_x86.c`: load through the addresses, compute,
  store through the destination. Values are bit patterns in data segments.
- Back end: `LLVMBuildFAdd` etc. with bitcasts to/from `float`/`double`.
- Test: arithmetic, comparisons and conversions.
