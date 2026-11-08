# Todo

Unsupported BLISS features, in order of implementation. Each task ends with
a test in `tests/language/` and an update to `docs/Bliss_Manual.md`.

## 1. Field names in general structure references

`.BLOCK [v, f]` fails with EXPREXP; only `BLOCK [v, 0, 0, 8, 0]` works.

- `lib/frontend/structures.c` (general reference parse, ~line 985): the
  access actuals are taken lexically, so field names are never expanded.
  Expand a field name into its component list, as ordinary structure
  references do.
- Also accept a field-set name with the `FIELD` attribute in that context.
- Test: `BLOCK [.p, nd_kind]`, both fetch and store.

## 2. Overlapping fields in PRESET

`PRESET` with fields that share bits in one unit gives PROVERLAP.

- `lib/llvmgen/llvm_symgen.c` (~lines 438, 451): instead of skipping the
  field, OR its bits into the current cell. Overlap is only an error when
  two fields set the same bits.
- Test: BLOCK with `[0,0,4,0]` and `[0,4,4,0]` preset; check the byte.

## 3. ACTUALCOUNT and ACTUALPARAMETER

Both report UNDCLNAME.

- Design decision first: the SysV x86-64 ABI does not pass an argument
  count. Proposal: a routine that uses ACTUALCOUNT is a variadic LLVM
  function with a hidden count as its first argument; calls from BLISS
  pass the count, and C callers must pass it too. Write the decision into
  the manual.
- Front end: declare them as executable functions (`execfuncs.c`); mark the
  enclosing routine as needing the count.
- Back end: `llvm_execfuncgen.c`; ACTUALPARAMETER(i) uses `va_arg`, or
  indexes the declared parameters when `i` is constant and in range.
- Calls: `expression.c` ~line 1085, also start checking the argument count
  against the declaration (warning).
- Test: a routine called with 0, 1 and 3 arguments.

## 4. WEAK attribute

- Parse `WEAK` in attribute lists (`declarations.c`) for `GLOBAL LITERAL`,
  `EXTERNAL LITERAL`, `GLOBAL` and `EXTERNAL` data and routines.
- Back end: `LLVMSetLinkage(..., LLVMExternalWeakLinkage)` for externals and
  `LLVMWeakAnyLinkage` for globals (`llvm_symgen.c`).
- Test: an `EXTERNAL x : WEAK` that nothing defines has address 0.

## 5. %SWITCHES lexical function

- `lib/frontend/switches.c` already knows the toggles; record their current
  on/off state per scope and return 1/0 from `%SWITCHES (name, ...)`.
- Register it with the other lexical functions (`lexeme.c`/`charfuncs.c`).
- The ignored toggles (OPTIMIZE, SAFE, UNAMES, ZIP) still keep state.
- Test: `%IF %SWITCHES (ZIP) %THEN ...` with SWITCHES ZIP and NOZIP.

## 6. LINKAGE declarations

Currently SYNTAXERR. Implement only what maps onto LLVM calling conventions.

- Parse `LINKAGE name = type (params) : options` with types `CALL` (C
  convention) and `FASTCALL`-like register passing (`fastcc` or
  `x86_regcallcc`); store as a new linkage symbol type in `symbols.c`.
- Accept a linkage name as a routine attribute (`declarations.c` ~line
  1344) and on `EXTERNAL ROUTINE`; carry it to the routine's genspace.
- Back end: `LLVMSetFunctionCallConv` on definitions and calls; register
  parameter locations other than the convention's own are an error.
- General routine calls `linkage-name (address, args)`, e.g.
  `BLISS (.l, 0)` (LRM 12.2, UM 5.2.5); now UNDCLNAME. Predeclare the
  default linkage name and allow user ones (`expression.c` ~line 1392).
- Test: two routines with a non-default linkage calling each other.

## 7. GLOBAL REGISTER and EXTERNAL REGISTER

Variables pinned to a named machine register. Depends on 6.

- Parse in `declarations.c`; the register number maps to an x86 register
  name in `llvm_machines.c`.
- Back end: `llvm.read_register` / `llvm.write_register` intrinsics, or
  reject registers LLVM can't reserve (only rsp/rbp-like are allowed by
  LLVM on x86; check before committing to this task).
- If LLVM cannot support it, report a clear error instead of EXPREXP.

## 8. Condition handling: ENABLE, SIGNAL, SIGNAL_STOP, SETUNWIND

The largest task; VMS-style handlers.

- Runtime: a small C library (`lib/runtime/`) with a per-thread handler
  stack, `SIGNAL`, `SIGNAL_STOP`, unwind via `setjmp`/`longjmp`.
- `ENABLE handler (actual, ...)` (LRM 17.2): only in the outermost block of
  a routine body; actuals are OWN/GLOBAL/FORWARD/LOCAL names (passed by
  address). Push a frame on entry, pop it on every exit path
  (`llvm_gencode.c` routine end, `RETURN`, `LEAVE`).
- `SIGNAL (cond, param, ...)` and `SIGNAL_STOP (...)` are executable
  functions (LRM 17.3); SIGNAL_STOP never continues the signaller.
  `SETUNWIND ()` requests an unwind (LRM 17.4.3.3); the one- and
  two-parameter forms are BLISS-32 only.
- Handler is called with the signal, mechanism and enable vectors
  (LRM 17.4). Returning true (low bit 1) continues, false resignals;
  unwinding is requested with SETUNWIND. The mechanism vector holds a
  return-value cell for SIGNAL and for the unwound establisher.
- Condition-value severity: `STS$K_WARNING`=0, `SUCCESS`=1, `ERROR`=2,
  `INFO`=3, `SEVERE`=4 (LRM 17.6.1); declare these in a library.
- `blissc` must link or document the runtime library; the test runner
  links it.
- UM 9.2 (CALC program) is a complete ENABLE/SIGNAL/SETUNWIND example; its
  handler takes `SIG [1]` as the condition and matches it with
  `STS$MATCH`. `STS$VALUE`/`STS$MATCH` and `SS$_...` codes come from the
  VMS library (STARLET), so provide equivalents in the runtime library.
- Test: signal caught, resignalled, and unwound.

## 9. INTERRUPT linkage type

Depends on 6. In the LRM, INTERRUPT is a linkage-type, not a routine
attribute (BLISS-16 13.2.4.1, BLISS-32 13.3.4.2; BLISS-36 has
PS_INTERRUPT); a routine gets it through `LINKAGE l = INTERRUPT;` and
`ROUTINE r : l`. Such a routine must not be called directly.

- Accept `INTERRUPT` as a linkage-type in task 6; map it to the LLVM
  `x86_intrcc` calling convention. Reject direct calls to it.
- Test: compile-only check of the emitted calling convention (`!! cout`
  or object inspection).

## 10. %ASCID string descriptors

- Emit a static descriptor (length word, type, class, pointer) followed by
  the string, as a PLIT-like constant (`lexeme.c`/`execfuncs.c` for the
  lexical part, `llvm_symgen.c` for data).
- Choose the descriptor layout for 64-bit (length 16 bits, pointer 64).
- Test: fetch length and characters through the descriptor.

## 11. Floating point

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
