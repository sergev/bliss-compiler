# README for the BLISS-M Compiler

This is the source package for the BLISS-M compiler, a portable
cross-compiler for the BLISS programming language.

## What is BLISS?
The BLISS language was developed at Carnegie-Mellon University in the 1970's for systems programming.
It was adopted and used extensively by Digital Equipment Corporation for development of systems
software across their 16-bit, 32-bit, 36-bit, and 64-bit systems.  It is a typeless, block-structured,
language with an extensive lexical processing (macro) facility.  See the
[Wikipedia article on BLISS](http://en.wikipedia.org/wiki/BLISS) for more information about the language.


## Current Status

Work in progress.   The front-end is mostly complete, with a fully
functioning parser and macro facility.  Back-end support is currently
limited to LLVM and x86 CPUs (32- or 64-bit), with a limited amount
of machine-specific support.

## Prerequisites

* Recent-vintage C compiler
* Recent version LLVM
* CMake 3.13 or later, or a recent version of autotools

Recent development and testing has been on Ubuntu 20.04 with
gcc 9 and LLVM 9.0.1 (prebuilt llvm-9-dev package) and LLVM
10.0.1 (built from sources).

## Building the compiler

First, clone [the repository](https://github.com/sergev/bliss-compiler.git).

1. Create a directory for the build.
2. Run `cmake <path-to-sources>` to generate the build files.
   CMake will attempt to locate a usable copy of LLVM automatically,
   but you can specify -DLLVM_CONFIG=<path-to-llvm-config> to force
   it to use a particular installation of LLVM.
3. Run `make` to build the compiler.

Running the compiler
--------------------

The build will produce a program called **blissc** in your build
directory.  Run `./blissc --help` for a description of the arguments
and options.


License
-------
All sources are released under the BSD 2-clause license.  See the
[LICENSE.TXT](LICENSE.TXT)
file for the license text.
