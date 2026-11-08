# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

BLISS-M: a cross-compiler for the BLISS systems-programming language (C, with one C++ file), using LLVM (17+) as the back end. Only x86 (32/64-bit) targets are supported. Produces the `blissc` executable.

## Build and test

```
make            # configure into build/ (RelWithDebInfo) on first run, then build
make debug; make   # reconfigure as Debug (-Werror is on in Debug)
make clean      # rm -rf build
```

Pass `-DLLVM_CONFIG=<path-to-llvm-config>` to cmake to pick a specific LLVM. Requires LLVM 17+ (tested with 21): the back end uses opaque pointers (`LLVMBuildLoad2`/`GEP2`/`Call2` with explicit element/function types; GEPs on unit pointers use `gctx->unittype`) and the new pass manager (`LLVMRunPasses` in `gencode_module_end`).

The CMake `check` target is part of `ALL`, so every `make` also compiles `tests/testharness.req` into `testharness.lib` and runs the full test suite (needs Python 3). `make test` runs `ctest`, but no CTest tests are registered; rely on the `check` output.

Run a single test (from `build/tests`, where `testharness.lib` lives):

```
cd build/tests
python3 ../../tests/runtests.py --blissc=../blissc ../../tests/lexical/literals.bli
```

`tests/Makefile` is a legacy in-tree runner; it leaves ignored binaries and `.o` files in `tests/`.

### Test format

Each test is a `.bli` module that does `LIBRARY 'testharness.lib'`, calls `test_setup(numcases=N)`, and defines `GLOBAL ROUTINE runtest(caseno)`. Expected results are comments starting with `!!`:
- `!! <n> <text>`: runtime output for case n (printed as `TEST 00n: <text>` by `test_output`)
- `!! cout <text>`: a line expected on compiler stdout
- `!! cerr <prefix>`: expected compiler diagnostic (lines starting with `%`), matched in order

The runner compiles the test with `blissc -o x.o -I .`, links it with `tests/testharness.c` using the C compiler, runs it, and compares output.

## Architecture

Static libraries, linked into `blissc` by [CMakeLists.txt](CMakeLists.txt):

- `lib/support`: file I/O, logging, status codes / message formatting (`statcodes.c`), string helpers.
- `lib/frontend`: the bulk of the compiler. Layered pipeline: `scanner` (characters) → `lexer` (lexeme streams, with macro and lexical-function expansion via `macros.c`, `charfuncs.c`, `lexeme.c`) → `parser` (lexeme-level parsing, keyword tables) → `declarations` / `expression` / `expr_control` / `execfuncs` / `structures` (semantic processing into expression trees). `nametable` and `symbols` manage scoped names; `libgen` writes precompiled `.lib` files (`blissc --library`); `listings` produces listings; `switches` handles module switches.
- `lib/llvmgen`: back end. Implements the interface in [include/blissc/gencode.h](include/blissc/gencode.h) and [include/blissc/machinedef.h](include/blissc/machinedef.h). `llvm_expgen.c` dispatches expression-tree nodes to `llvm_opexpgen` / `llvm_ctrlexpgen` / `llvm_execfuncgen`; `llvm_symgen` emits data and routine symbols; `llvm_machines.c` defines target parameters; `llvm_builtins_x86.c` provides x86 builtins; `llvm_helper.cpp` wraps LLVM C++ APIs not exposed through the LLVM C API.
- `lib/driver` + `driver/blissc.c`: command-line parsing and orchestration (`machine_init`, frontend setup, output selection).
- `lib/ir`: an unused BLISS IR experiment; not built.

Frontend ↔ back end coupling: `expression.c` calls `gencode_init`/`gencode_postinit`/`gencode_finish`; `declarations.c` calls `gencode_module_begin/end` and `gencode_routine_begin/end`, so code generation happens per routine as declarations are parsed. Machine properties (unit/value/address bit widths, char sizes) come from `machinedef_t` and are queried by the frontend through the inline accessors in `machinedef.h`.

Public headers are in `include/blissc/` (with `support/` and `ir/` subdirs); `config.h` is generated into `build/include/blissc/` from `cmake/config.h.in`.

When updating for new LLVM versions, the API-sensitive code is in `lib/llvmgen` (especially `llvm_helper.cpp`) and `cmake/Modules/FindLLVM.cmake`.
