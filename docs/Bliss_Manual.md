# BLISS in a Nutshell

A short guide to the BLISS programming language for programmers who know C,
Python or JavaScript. For full details see the *BLISS Language Reference
Manual* (DEC, 1987), [bliss-language-reference-manual.pdf](bliss-language-reference-manual.pdf).
References like **LRM 6.3** point to its sections.

Notes marked **blissc** describe this compiler (BLISS-M, x86-64 back end).

## Contents

1. [What BLISS is](#1-what-bliss-is)
2. [A first program](#2-a-first-program)
3. [Lexical rules](#3-lexical-rules)
4. [Values, names and the dot](#4-values-names-and-the-dot)
5. [Literals](#5-literals)
6. [Operators](#6-operators)
7. [Blocks](#7-blocks)
8. [Control expressions](#8-control-expressions)
9. [Data declarations](#9-data-declarations)
10. [Attributes](#10-attributes)
11. [Literals and BIND](#11-literals-and-bind)
12. [PLITs: constant tables](#12-plits-constant-tables)
13. [Data structures](#13-data-structures)
14. [Routines](#14-routines)
15. [Lexical functions and conditional compilation](#15-lexical-functions-and-conditional-compilation)
16. [Macros](#16-macros)
17. [REQUIRE and LIBRARY](#17-require-and-library)
18. [Character handling (CH$)](#18-character-handling-ch)
19. [Modules](#19-modules)
20. [Other declarations](#20-other-declarations)
21. [Linkages and condition handling](#21-linkages-and-condition-handling)
22. [Pitfalls for C programmers](#22-pitfalls-for-c-programmers)
23. [Keyword index](#23-keyword-index)

---

## 1. What BLISS is

BLISS is a systems-programming language created at Carnegie Mellon in 1970
and used by DEC to write much of VMS. It sits at about the level of C, with
some unusual ideas:

- **Typeless.** Every value is a machine word (a *fullword*). The operator
  decides how to treat it (signed, unsigned, address).
- **Everything is an expression.** Blocks, `IF`, `CASE` and loops all yield
  values. There are no statements.
- **A name means an address.** `x` is the address of variable `x`; `.x` is
  its contents. The dot is explicit, always.
- **No `goto`.** Instead there are `LEAVE`, `EXITLOOP` and `RETURN`.
- **A powerful macro language** that works on tokens, with lexical functions
  and conditional compilation.
- **User-defined data structures**: you define how an index maps to an
  address.

Dialects: BLISS-16 (PDP-11), BLISS-32 (VAX), BLISS-36 (PDP-10). blissc
implements BLISS-M, a BLISS-32-like dialect for modern targets. (LRM 1)

**blissc:** on x86-64 a fullword is 64 bits: `%BPVAL` = 64,
`%BPUNIT` = 8, `%UPVAL` = 8 (bytes per word).

## 2. A first program

```bliss
MODULE hello (MAIN = start) =   ! the program begins by calling start
BEGIN

EXTERNAL ROUTINE
    %NAME('printf');            ! C function; %NAME keeps lower case

ROUTINE fact (n) =
    IF .n LEQ 1 THEN 1 ELSE .n * fact (.n - 1);

ROUTINE start =
BEGIN
    %NAME('printf') (UPLIT (%ASCIZ %STRING ('10! = %ld', %CHAR (10))),
                     fact (10));
    0                           ! value of start = exit status
END;

END
ELUDOM
```

Build and run with blissc:

```
blissc -o hello.o hello.bli
cc -o hello hello.o
./hello                         # prints: 10! = 3628800
```

Points to notice:

- A module is `MODULE name = BEGIN ... END ELUDOM` (`ELUDOM` is `MODULE`
  backwards). `MAIN = start` names the routine where the program starts.
  **blissc** turns it into a C `main` that calls `start` and returns its
  value as the exit status.
- `.n` fetches the value of parameter `n`.
- The routine body is one expression; its value is the result.
- `!` starts a comment.

## 3. Lexical rules

(LRM 2, 4.10)

- **Case does not matter.** `Count`, `COUNT` and `count` are the same name.
  **blissc** turns names to upper case in object files; use `%NAME('x')`
  for a case-sensitive external name.
- **Names**: letters, digits, `$`, `_`; up to 31 characters; must not start
  with a digit. Names with `$` are by convention reserved for DEC/system use.
- **Keywords** (`BEGIN`, `IF`, `WORD`, `BIND`, ...) are reserved and can't be
  names. See [Keyword index](#23-keyword-index).
- **Comments**:
  ```bliss
  x = 1;          ! from "!" to end of line
  %( embedded
     comment, may span lines )%
  ```
- Spaces and line breaks separate tokens and are otherwise ignored.

## 4. Values, names and the dot

(LRM 3, 4.5, 5.1)

A **data segment** is a piece of storage: a word, a byte, a vector. A
**name** declared for a data segment stands for its **address**:

```bliss
OWN a, b;

a = 5;          ! store 5 at address a
b = .a + 1;     ! fetch from a, add 1, store at b  -> b contains 6
b = a + 1;      ! b gets the ADDRESS of a plus 1 (almost never what you want)
```

The fetch operator `.` can be applied to any expression that yields an
address:

```bliss
OWN p, x;
p = x;          ! p holds the address of x (like p = &x in C)
.p = 42;        ! store through p          (like *p = 42)
x = ..p + 1;    ! fetch p, then fetch what it points to
```

The left side of `=` is an address expression; it is **not** fetched.

### Words and units

- `%BPUNIT`: bits per addressable unit (8 on byte machines).
- `%BPVAL`: bits per fullword value.
- `%UPVAL`: units per value (`%BPVAL / %BPUNIT`).
- `%BPADDR`: bits per address.

Address arithmetic counts **units** (bytes), not elements: the word after
`x` is at `x + %UPVAL`.

### Truth

Relational operators yield `1` (true) or `0` (false). Tests (`IF`,
`WHILE`, ...) look **only at the low bit**: odd is true, even is false. So
`IF 2 THEN ...` is false! Use `IF .x NEQ 0` rather than `IF .x`.
(LRM 6.1.3)

## 5. Literals

(LRM 4.2, 4.3)

### Numbers

| Form | Meaning | Example |
|---|---|---|
| `123` | decimal | `123` |
| `%B'...'` | binary | `%B'1010'` = 10 |
| `%O'...'` | octal | `%O'777'` = 511 |
| `%X'...'` | hexadecimal | `%X'FF'` = 255 |
| `%DECIMAL'...'` | decimal (explicit) | `%DECIMAL'-12'` |
| `%C'c'` | character code | `%C'A'` = 65 |
| `%E'...'`, `%D'...'`, `%G'...'`, `%H'...'` | floating point (VAX formats) | `%E'1.5'` |

`%B`, `%O`, `%X`, `%DECIMAL` may have a sign inside the quotes:
`%B'-1111'` = -15.

**blissc:** floating-point literals are not supported.

### Strings

A string is in single quotes; a quote inside is doubled:

```bliss
'Hello'
'It''s'         ! It's
```

String forms, mostly used inside PLITs:

| Form | Stored as |
|---|---|
| `'abc'` or `%ASCII 'abc'` | the characters |
| `%ASCIZ 'abc'` | characters + a 0 byte (C string) |
| `%ASCIC 'abc'` | a count byte, then characters |
| `%ASCID 'abc'` | VMS string descriptor (not in blissc) |
| `%RAD50_11`, `%SIXBIT` | old packed encodings (not in blissc) |

A short string used as a value is a number: `'AB'` as an expression is the
word holding those bytes.

## 6. Operators

(LRM 5.1)

From highest to lowest priority:

| Operators | Meaning | Associates |
|---|---|---|
| `.e` | fetch | right |
| `+e`, `-e` | sign | right |
| `e1 ^ e2` | **shift** e1 left by e2 bits (negative = right, arithmetic) | left |
| `*`, `/`, `MOD` | multiply, divide (truncates), remainder | left |
| `+`, `-` | add, subtract | left |
| `EQL NEQ LSS LEQ GTR GEQ` | signed comparison | left |
| `EQLU NEQU LSSU LEQU GTRU GEQU` | unsigned comparison | left |
| `EQLA NEQA LSSA LEQA GTRA GEQA` | address comparison | left |
| `NOT e` | bitwise complement | right |
| `AND` | bitwise and | left |
| `OR` | bitwise or | left |
| `EQV`, `XOR` | bitwise equivalence, exclusive or | left |
| `e1 = e2` | assignment; value is e2 | right |

Examples:

```bliss
x = 1 ^ 4;              ! 16  (^ is shift, not power!)
x = -32 ^ -2;           ! -8  (arithmetic right shift)
x = 17 / 5;             ! 3
x = -17 MOD 5;          ! -2  (sign follows the dividend)
x = -1 LSS 0;           ! 1   (signed)
x = -1 LSSU 0;          ! 0   (as unsigned, -1 is huge)
x = NOT 0;              ! all ones = -1
a = b = 0;              ! assignment is an expression: both get 0
```

`AND`, `OR` and `NOT` are **bitwise**. In a test they work as logical
operators because true is 1 and false is 0. In tests the compiler may skip
evaluating an operand it does not need, but it is not obliged to, and the
order is not fixed. Don't rely on short-circuit side effects.
(LRM 6.1.3)

An operand of an operator must not be a control expression; wrap it in
parentheses: `x = (IF .a THEN 1 ELSE 2) + 1;`.

### Built-in functions

(LRM 5.2)

| Function | Value |
|---|---|
| `ABS (e)` | absolute value |
| `SIGN (e)` | -1, 0 or 1 |
| `MAX (e1, e2, ...)`, `MIN (...)` | signed max/min of any number of args |
| `MAXU`, `MINU` | unsigned |
| `MAXA`, `MINA` | address |
| `%REF (e)` | address of a temporary holding e |

`%REF` passes a value to a routine that expects an address:

```bliss
ROUTINE deref (p) = ..p;
x = deref (%REF (.a + 1));      ! = .a + 1
```

## 7. Blocks

(LRM 4.6, 8.1)

A block is a sequence of declarations, then expressions, between
`BEGIN`/`END` or `(`/`)`. The two forms are identical.

```bliss
y = BEGIN
        LOCAL t;
        t = .x * 2;
        .t + 1          ! no ";" -> this is the value of the block
    END;
```

- Expressions are separated (terminated) by `;`.
- If the last expression is **not** followed by `;`, it is the block's value.
- If it is followed by `;`, the block has no value. Using it as a value is an
  error.
- Names declared in a block are visible only inside it, and hide outer names
  with the same spelling.

So `(.a + .b) * 2` is really a block containing one expression.

## 8. Control expressions

(LRM 6)

### IF

```bliss
IF .x LSS 0 THEN x = 0;
max = (IF .a GTR .b THEN .a ELSE .b);
```

`ELSE` binds to the closest `IF`. An `IF` without `ELSE` has no value.

### CASE: jump table on an integer

```bliss
CASE .op FROM 1 TO 5 OF
SET
    [1]:        r = .a + .b;
    [2, 3]:     r = .a - .b;        ! several labels
    [4 TO 5]:   r = 0;              ! a range
    [INRANGE]:  r = -1;             ! any value in 1..5 not listed
    [OUTRANGE]: r = -2;             ! value outside 1..5
TES;
```

The selector must lie in `FROM`..`TO` unless `[OUTRANGE]` is given;
otherwise the result is undefined. Labels must be compile-time constants.

### SELECT: match against a list of tests

```bliss
SELECT .ch OF
SET
    [%C'0' TO %C'9']: kind = digit;
    [%C' ', 9]:       kind = space;
    [ALWAYS]:         count = .count + 1;      ! every time
    [OTHERWISE]:      kind = other;            ! only if nothing matched yet
TES;
```

- `SELECT` tries **every** label in order and runs **all** that match.
- `SELECTONE` stops at the first match (like C `switch` with `break`).
- `SELECTU`, `SELECTONEU`: unsigned comparison; `SELECTA`, `SELECTONEA`:
  address comparison.
- Labels may be any expressions, not just constants.
- Value: the value of the last action executed, or -1 if none ran.

### Counted loops

```bliss
INCR i FROM 1 TO 10 DO total = .total + .i;
DECR i FROM 10 TO 1 BY 2 DO show (.i);     ! 10, 8, 6, 4, 2
INCR i FROM 0 DO                           ! no TO: runs until EXITLOOP
    IF ..p EQL 0 THEN EXITLOOP ELSE p = .p + %UPVAL;
```

- The loop variable (`i`) is declared by the loop; it is local to the loop
  and you read it with `.i`.
- Defaults: `FROM 0`, `BY 1`; for `INCR` no `TO` means no end test; for
  `DECR` the default `FROM` is the largest value.
- `INCRU`/`DECRU` compare unsigned, `INCRA`/`DECRA` as addresses.
- Bounds are evaluated once, before the loop starts.
- Value: -1 if the loop runs to completion.

### Tested loops

```bliss
WHILE .n GTR 0 DO n = .n - 1;      ! test first
UNTIL .done DO step ();
DO n = .n - 1 WHILE .n GTR 0;      ! body first
DO c = getc () UNTIL .c EQL %C'.';
```

Value: -1.

### Leaving early

| Expression | Effect |
|---|---|
| `EXITLOOP` / `EXITLOOP e` | leave the innermost loop; e becomes the loop's value |
| `LEAVE label` / `LEAVE label WITH e` | leave the labeled block or loop |
| `RETURN` / `RETURN e` | leave the routine |

Labels are declared with `LABEL` and attached with `name:`:

```bliss
BEGIN
    LABEL search;
    found = search:
        BEGIN
            INCR i FROM 0 TO .n - 1 DO
                IF .tab[.i] EQL .key THEN LEAVE search WITH .i;
            -1                              ! not found
        END;
END;
```

`LEAVE` can exit several nested loops at once, which is what `goto` is
usually used for in C.

## 9. Data declarations

(LRM 10)

| Declaration | Where storage lives | Lifetime | Visible |
|---|---|---|---|
| `OWN` | static memory | whole program | this block/module |
| `GLOBAL` | static memory | whole program | other modules too |
| `EXTERNAL` | defined in another module | — | here |
| `LOCAL` | stack (or register) | the block | this block |
| `STACKLOCAL` | stack, never a register | the block | this block |
| `REGISTER` | a register, if possible | the block | this block |

```bliss
OWN
    counter,                            ! one word, starts at 0
    table : VECTOR [100];

GLOBAL
    errors : INITIAL (0);

EXTERNAL
    stdin;                              ! declared GLOBAL elsewhere

ROUTINE next =
BEGIN
    OWN calls;                          ! like C "static" inside a function
    LOCAL tmp;
    calls = .calls + 1;
    .calls
END;
```

`OWN` and `GLOBAL` data start at zero unless initialized. `LOCAL` data
start undefined unless you give `INITIAL`/`PRESET`.

`FORWARD` declares an `OWN` or `GLOBAL` name before its definition later
in the same module:

```bliss
FORWARD tbl : VECTOR [10];
```

### MAP: change how a name is viewed

`MAP` gives new attributes to an existing name without allocating storage.
Typical use: treat a word holding an address as a pointer to a structure.

```bliss
ROUTINE sum (v, n) =
BEGIN
    MAP v : REF VECTOR;
    LOCAL s;
    s = 0;
    INCR i FROM 0 TO .n - 1 DO s = .s + .v[.i];
    .s
END;
```

## 10. Attributes

(LRM 9)

Attributes follow a name after `:`.

| Attribute | Meaning |
|---|---|
| `BYTE`, `WORD`, `LONG` | allocation unit: 8, 16, 32 bits (**blissc** also has `QUAD`, 64) |
| `SIGNED`, `UNSIGNED` | sign-extend on fetch, or zero-extend (default) |
| `VECTOR [...]`, `BLOCK [...]`, ... | structure; see [§13](#13-data-structures) |
| `REF struct` | this word holds the address of a structure |
| `FIELD (names)` | field sets usable with a BLOCK |
| `INITIAL (values)` | initial contents, in order |
| `PRESET ([index] = value, ...)` | initialize selected elements |
| `ALIGN (n)` | align the address to 2^n units |
| `VOLATILE` | always fetch/store through memory |
| `PSECT (name)` | put in a named program section |
| `NOVALUE` | routine returns no value |
| `LINKAGE`, `ADDRESSING_MODE`, `WEAK`, `RANGE` | target-specific; see LRM 9 |

```bliss
OWN
    flags : BYTE,
    temp : WORD SIGNED,
    hex : VECTOR [16, BYTE] INITIAL (BYTE ('0123456789ABCDEF')),
    primes : VECTOR [5] INITIAL (2, 3, 5, 7, 11),
    zeros : VECTOR [8] INITIAL (REP 8 OF (0)),
    sparse : VECTOR [10] PRESET ([0] = 1, [9] = 100),
    buf : VECTOR [64, BYTE] ALIGN (3),
    port : VOLATILE;
```

A `SIGNED BYTE` holding 255 reads back as -1; an `UNSIGNED BYTE` (the
default) reads 255.

## 11. Literals and BIND

(LRM 14)

### LITERAL: named constants

```bliss
LITERAL
    buf_size = 512,
    half = buf_size / 2,
    esc = 27;

GLOBAL LITERAL version = 3;            ! visible to other modules
EXTERNAL LITERAL max_users;            ! value comes from the linker
```

A literal has no address; use it without a dot.

### BIND: another name for an address

```bliss
OWN table : VECTOR [10];
BIND
    first = table[0],                   ! alias for an element
    msg = UPLIT BYTE (%ASCIZ 'Ready') : VECTOR [, BYTE];

first = 7;                              ! sets table[0]
```

`BIND` does not copy anything; it names an address. Inside a routine the
address may be computed at run time, when the block is entered:

```bliss
BEGIN
    BIND elem = tab[.i];                ! evaluated once, here
    elem = .elem + 1;
END
```

`BIND ROUTINE` names a routine address:

```bliss
BIND ROUTINE handler = .handler_table[.code];
handler (.arg);
```

`GLOBAL BIND` makes the name visible to other modules.

## 12. PLITs: constant tables

(LRM 4.4)

A PLIT ("pure literal") is a read-only table in memory. Its value is its
address.

```bliss
BIND
    days = PLIT (31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31) : VECTOR,
    names = UPLIT (UPLIT (%ASCIZ 'red'), UPLIT (%ASCIZ 'green')) : VECTOR,
    bytes = UPLIT BYTE (1, 2, 3, 250) : VECTOR [, BYTE];

x = .days[1];                           ! 28
n = .days[-1];                          ! 12: PLIT stores its count before element 0
```

- `PLIT` stores the length (in words) in the word **before** the data.
- `UPLIT` ("uncounted PLIT") has no count word.
- `BYTE`, `WORD`, `LONG` after `PLIT`/`UPLIT` set the element size.
- Items can be numbers, strings, addresses of other data, nested PLITs, or
  `REP n OF (items)`.
- Strings are packed byte by byte, padded to a whole word.

## 13. Data structures

(LRM 11)

### Predefined structures

```bliss
OWN
    v : VECTOR [10],                    ! 10 words, v[0]..v[9]
    vb : VECTOR [80, BYTE],             ! 80 bytes
    vs : VECTOR [4, WORD, SIGNED],      ! signed 16-bit elements
    bits : BITVECTOR [100],             ! 100 one-bit flags
    blk : BLOCK [3],                    ! a 3-word record
    bv : BLOCKVECTOR [10, 3];           ! 10 records of 3 words

v[3] = 42;
x = .v[3];
bits[17] = 1;
IF .bits[17] THEN ...;
```

Indexes start at 0. There is no bounds checking.

### Fields

A **field** is a bit range inside a structure: `[offset, position, size, sign]`
(offset in words for BLOCK, then bit position, bit size, 0 = unsigned / 1 =
signed).

```bliss
FIELD
    node_fields = SET
        nd_next = [0, 0, %BPVAL, 0],    ! word 0
        nd_kind = [1, 0, 8, 0],         ! word 1, low byte
        nd_flag = [1, 8, 1, 0],         ! word 1, bit 8
        nd_val  = [2, 0, %BPVAL, 1]     ! word 2, signed
    TES;

OWN n1 : BLOCK [3] FIELD (node_fields);

n1[nd_kind] = 5;
n1[nd_flag] = 1;
IF .n1[nd_flag] THEN ...;
```

A field name is just a macro for its four values; `n1[1, 0, 8, 0]` is the
same as `n1[nd_kind]`.

### Pointers to structures: REF

```bliss
ROUTINE list_sum (head : REF BLOCK FIELD (node_fields)) =
BEGIN
    LOCAL s, p : REF BLOCK FIELD (node_fields);
    s = 0;
    p = .head;
    WHILE .p NEQ 0 DO
    BEGIN
        s = .s + .p[nd_val];
        p = .p[nd_next];
    END;
    .s
END;
```

With `REF`, `p[x]` means "element x of the structure whose address is in p".
Without `REF`, you would write `.p[...]` on a name that *is* the
structure.

### Field references: bits of any word

`e<position, size>` or `e<position, size, sign>` picks bits out of the
word at address e:

```bliss
OWN w;
w = %X'ABCD';
x = .w<8, 4>;           ! %X'B'
w<0, 4> = 0;            ! clear low 4 bits -> %X'ABC0'
x = .w<0, 8, 1>;        ! low byte, sign-extended
```

Position and size may be run-time expressions.

### User-defined structures

You define the allocation size and the address formula:

```bliss
STRUCTURE
    ring [i; n = 4] =                       ! access formal i; allocation formal n (default 4)
        [n * %UPVAL]                        ! size in units
        (ring + (i MOD n) * %UPVAL),        ! address of element i
    matrix [r, c; nr, nc] =
        [nr * nc * %UPVAL]
        (matrix + ((r - 1) * nc + (c - 1)) * %UPVAL);  ! 1-based

OWN
    r8 : ring [8],
    m : matrix [3, 4];

r8[11] = 5;             ! same as r8[3]
m[2, 4] = .m[1, 1];
```

Inside the definition the structure name stands for the base address.
`VECTOR`, `BLOCK`, etc. are themselves defined this way (LRM 11.10).

### General structure reference

Apply a structure to any address without declaring a name:

```bliss
x = .VECTOR [.p, 2];            ! element 2 of the vector at .p
BLOCK [.q, 1, 0, 8, 0] = 1;     ! a field of the block at .q
```

### Size functions

```bliss
%ALLOCATION (v)                 ! units allocated for v
%SIZE (VECTOR [10, BYTE])       ! units needed for that structure: 10
```

## 14. Routines

(LRM 12)

```bliss
ROUTINE add (a, b) = .a + .b;           ! value = result

ROUTINE swap (pa, pb) : NOVALUE =       ! no result
BEGIN
    LOCAL t;
    t = ..pa;
    .pa = ..pb;
    .pb = .t;
END;

swap (x, y);                            ! pass addresses of x and y
z = add (.x, 1);                        ! pass values
```

- Parameters are passed **by value** (a word each). To let a routine change
  a variable, pass its address and assign through it, as `swap` does.
- A parameter is a local variable: read it with `.a`, assign with `a = ...`.
- Parameters may have attributes: `ROUTINE f (v : REF VECTOR, n) = ...`.
- `NOVALUE` routines return nothing; using their value is an error.
- Recursion is allowed.

| Declaration | Meaning |
|---|---|
| `ROUTINE f ...` | private to the module |
| `GLOBAL ROUTINE f ...` | callable from other modules |
| `FORWARD ROUTINE f;` | f is defined later in this module |
| `EXTERNAL ROUTINE f;` | f is defined in another module (or in C) |

### Calling through an address

The name of a routine is its address, so routines are first-class values:

```bliss
ROUTINE apply (f, x) = (.f) (.x);       ! call the routine whose address is in f

ROUTINE double (x) = .x * 2;

y = apply (double, 21);                 ! 42
```

### Variable argument lists

`ACTUALCOUNT ()` returns the number of arguments passed and
`ACTUALPARAMETER (i)` the i-th one. **blissc:** not supported.

## 15. Lexical functions and conditional compilation

(LRM 15)

Names starting with `%` are processed by the compiler as it reads tokens,
before parsing. They work on token sequences and constants.

### Strings and names

| Function | Result |
|---|---|
| `%STRING ('AB', 12, 'C')` | `'AB12C'` |
| `%EXACTSTRING (n, %C'x', s...)` | string padded/truncated to n chars |
| `%CHAR (65, 66)` | `'AB'` |
| `%CHARCOUNT ('abc')` | 3 |
| `%EXPLODE ('ABC')` | `'A', 'B', 'C'` |
| `%NAME ('ab', 'c')` | the name `abc` (case kept) |
| `%QUOTENAME (...)` | like `%NAME`, but not expanded if it is a macro |
| `%REMOVE ((a + 1))` | `a + 1` (strips outer brackets) |

### Tests

| Function | Result |
|---|---|
| `%NULL (x)` | 1 if x is empty |
| `%IDENTICAL (s1, s2)` | 1 if two token sequences are the same |
| `%ISSTRING (x)` | 1 if every argument is a string |
| `%CTCE (e)` | 1 if e is a compile-time constant |
| `%LTCE (e)` | 1 if e is a link-time constant |
| `%DECLARED (name)` | 1 if name is declared |
| `%BLISS (BLISS32)` | 1 if compiling with that dialect (**blissc**: `%BLISS (BLISSM)`) |
| `%SWITCHES (...)` | 1 if the given switches are on |
| `%VARIANT` | value of the `--variant` command-line option |

### Numbers

| Function | Result |
|---|---|
| `%NUMBER (k)` | value of a literal / compile-time name, as a number |
| `%NBITS (7, 2)` | 4: bits needed to hold the values, signed |
| `%NBITSU (7, 2)` | 3: same, unsigned |
| `%ALLOCATION (x)`, `%SIZE (s)` | see [§13](#13-data-structures) |
| `%FIELDEXPAND (f)` | the four values of field f |

### Messages

`%ERROR ('text')`, `%WARN`, `%INFORM`, `%MESSAGE`, `%PRINT`,
`%ERRORMACRO`; `%TITLE` and `%SBTTL` set listing headers.

### Conditional compilation

```bliss
%IF %BLISS (BLISS32) %THEN
    LITERAL word_bits = 32;
%ELSE
    LITERAL word_bits = %BPVAL;
%FI
```

The test is a compile-time constant; as with `IF`, the low bit decides.
The skipped part is not compiled at all. `%IF` can appear anywhere, even in
the middle of an expression.

### Compile-time variables

```bliss
COMPILETIME n = 0;
%ASSIGN (n, n + 1)              ! n is now 1
LITERAL count = n;
```

## 16. Macros

(LRM 16)

Macros replace tokens with tokens. A macro body ends with `%`.

### Simple and parameterized macros

```bliss
MACRO
    word_size = %BPVAL %,
    square (x) = ((x) * (x)) %,
    inc (var) = var = .var + 1 %;

y = square (.a + 1);
inc (count);
```

Missing actual parameters are empty. Extra ones are an error unless the
macro is conditional or iterative.

### Conditional macros: `[]`

A macro with `[]` after the parameter list is **conditional**: it expands to
nothing if called with fewer actuals than formals. `%REMAINING` holds the
extra actuals. Only conditional macros may call themselves recursively.

```bliss
MACRO
    sum_all (a) [] = (a %IF NOT %NULL (%REMAINING) %THEN + sum_all (%REMAINING) %FI) %;

x = sum_all (1, 2, 3, 4);       ! (1 + (2 + (3 + (4))))
```

### Iterative macros: `[formals]`

The body is repeated once for each group of actuals; repetitions are
separated by the punctuation around the call (a comma inside a list, `;`
in a block, and so on).

```bliss
MACRO
    zero [v] = v = 0 %;

zero (a, b, c);                 ! a = 0; b = 0; c = 0
```

### Keyword macros

Arguments are given by name, in any order, and can have defaults:

```bliss
KEYWORDMACRO
    make_buf (name, size = 80, fill = 0) =
        OWN name : VECTOR [size, BYTE] INITIAL (REP size OF BYTE (fill)) %;

make_buf (name = line);
make_buf (size = 16, name = id);
```

### Macro helpers

| Function | Meaning |
|---|---|
| `%LENGTH` | number of actuals in this call |
| `%REMAINING` | actuals not matched to formals |
| `%COUNT` | recursion depth or iteration number (from 0) |
| `%EXITMACRO` | stop expanding this macro |
| `%EXITITERATION` | skip to the next iteration |
| `%QUOTE x` | don't expand x now |
| `%UNQUOTE x`, `%EXPAND x` | force binding / expansion |

**blissc:** runaway recursion stops with error `MACDEPTH` after 1000 levels.

## 17. REQUIRE and LIBRARY

(LRM 16.5, 16.6)

```bliss
REQUIRE 'defs.req';             ! include the source text (like #include)
LIBRARY 'defs';                 ! load a precompiled library (.lib)
```

A library is made by compiling a file of declarations with
`blissc --library defs.req`. Libraries load faster than `REQUIRE`.
`-I dir` adds a search directory. `%REQUIRE ('file')` includes a file
from inside an expression or macro.

## 18. Character handling (CH$)

(LRM 20)

Strings are sequences of characters in memory, accessed via **character
pointers**. On byte machines a character pointer is just an address.

```bliss
OWN buf : VECTOR [CH$ALLOCATION (80)];     ! room for 80 characters
LOCAL p;

p = CH$PTR (buf);                          ! pointer to first char
CH$WCHAR_A (%C'H', p);                     ! write, then advance p
CH$WCHAR_A (%C'i', p);
n = CH$DIFF (.p, CH$PTR (buf));            ! 2
```

| Group | Functions |
|---|---|
| Allocation | `CH$ALLOCATION (n)` words for n chars; `CH$SIZE (ptr)` bits per char |
| Pointers | `CH$PTR (addr [, i])`, `CH$PLUS (ptr, i)`, `CH$DIFF (p1, p2)` |
| Read | `CH$RCHAR (ptr)`, `CH$RCHAR_A (addr_of_ptr)` read then advance, `CH$A_RCHAR` advance then read |
| Write | `CH$WCHAR (ch, ptr)`, `CH$WCHAR_A (ch, addr_of_ptr)`, `CH$A_WCHAR` |
| Copy | `CH$MOVE (n, src, dst)`, `CH$FILL (ch, n, dst)`, `CH$COPY (n1, s1, n2, s2, ..., fill, n, dst)` |
| Compare | `CH$EQL`, `CH$NEQ`, `CH$LSS`, `CH$LEQ`, `CH$GTR`, `CH$GEQ (n1, p1, n2, p2 [, fill])`; `CH$COMPARE` gives -1/0/1 |
| Search | `CH$FIND_CH (n, ptr, ch)`, `CH$FIND_NOT_CH`, `CH$FIND_SUB (n, ptr, n2, sub)`; `CH$FAIL (ptr)` is 1 if not found |
| Translate | `CH$TRANSTABLE (...)` builds a table; `CH$TRANSLATE (tab, n1, src, fill, n2, dst)` |

The `_A` forms take the **address** of a pointer variable (`p`, not `.p`)
because they update it.

`CH$MOVE`, `CH$FILL` and `CH$COPY` return a pointer just past what they
wrote, so calls can be chained:

```bliss
p = CH$PTR (buf);
p = CH$MOVE (5, CH$PTR (UPLIT ('Hello')), .p);
p = CH$FILL (%C'!', 3, .p);              ! buf = 'Hello!!!'
```

Shorter strings in a comparison are padded with the fill character (default
0):

```bliss
CH$EQL (2, CH$PTR (UPLIT ('ab')), 4, CH$PTR (UPLIT ('ab  ')), %C' ')   ! 1
```

## 19. Modules

(LRM 19)

A module is the unit of compilation:

```bliss
MODULE mymod (IDENT = 'V1.0', MAIN = start) =
BEGIN
    ! declarations only: no executable code at module level
END
ELUDOM
```

Common module switches:

| Switch | Meaning |
|---|---|
| `IDENT = 'string'` | version string in the object file |
| `MAIN = routine` | program entry point; once per program |
| `LANGUAGE (BLISS32, ...)` | dialects the module is meant for |
| `ADDRESSING_MODE`, `ENVIRONMENT`, `LINKAGE` | target settings |
| `LIST (...)`, `OPTIMIZE`, `OPTLEVEL = n` | compiler output and optimization |

**blissc:** `MAIN = r` generates a C `main` that calls `r` (passing 0 for
any parameters) and returns its value as the exit status; 0 if `r` is
`NOVALUE`. `MAIN` is a reserved word in blissc. `LANGUAGE`,
`ADDRESSING_MODE` and `ENVIRONMENT` are accepted and ignored.

A program is several modules linked together; they share data and routines
through `GLOBAL` and `EXTERNAL` declarations.

## 20. Other declarations

(LRM 18)

```bliss
PSECT OWN = mydata;             ! put later OWN data in program section "mydata"
SWITCHES NOOPTIMIZE;            ! change switches for the rest of the block
BUILTIN SP;                     ! use a machine-specific builtin
LABEL loop1;                    ! declare a label for LEAVE
UNDECLARE tmp;                  ! forget a name (e.g. a macro from a library)
```

## 21. Linkages and condition handling

(LRM 13, 17)

These were tied to DEC operating systems.

- **LINKAGE** declarations describe calling conventions: which registers
  hold which parameters, which registers are preserved, and so on.
- **ENABLE** names a handler routine for a block. `SIGNAL (code, ...)`
  raises a condition; the handler may continue, resignal, or unwind the
  stack (`SETUNWIND`). `SIGNAL_STOP` raises a fatal condition. This is
  like exceptions, built on the VMS condition-handling mechanism.

**blissc:** user `LINKAGE` declarations, `ENABLE`, `SIGNAL` and
`ACTUALCOUNT` are not implemented. Routines use the platform C calling
convention, so BLISS and C code can call each other directly.

## 22. Pitfalls for C programmers

| You write | You meant | Why |
|---|---|---|
| `x = y + 1` | `x = .y + 1` | `y` is an address; fetch with `.` |
| `IF .flags THEN` | `IF .flags NEQ 0 THEN` | tests look only at the low bit |
| `x = 2 ^ 3` expecting 8 | `x = 2 * 2 * 2` | `^` is a shift: `2 ^ 3` = 16 |
| `p + 1` to get the next word | `p + %UPVAL` | addresses count bytes |
| `x = IF ... THEN 1 ELSE 2 + 3` | `x = (IF ... THEN 1 ELSE 2) + 3` | control expressions as operands need parentheses |
| `BEGIN ...; .x; END` as a value | `BEGIN ...; .x END` | a trailing `;` discards the value |
| `-1 LSS 5` on unsigned data | `LSSU` | plain relations are signed |
| `CH$WCHAR_A (c, .p)` | `CH$WCHAR_A (c, p)` | `_A` functions need the pointer's address |
| using `word`, `bind`, `label` as names | another name | they are keywords |

## 23. Keyword index

| Keyword | Section |
|---|---|
| `ABS`, `MAX`, `MIN`, `SIGN`, `%REF` | [§6](#6-operators) |
| `ALIGN`, `BYTE`, `WORD`, `LONG`, `SIGNED`, `UNSIGNED`, `VOLATILE`, `INITIAL`, `PRESET` | [§10](#10-attributes) |
| `BEGIN`, `END` | [§7](#7-blocks) |
| `BIND`, `LITERAL` | [§11](#11-literals-and-bind) |
| `BITVECTOR`, `BLOCK`, `BLOCKVECTOR`, `VECTOR`, `STRUCTURE`, `FIELD`, `REF`, `MAP` | [§13](#13-data-structures), [§9](#9-data-declarations) |
| `CASE`, `SELECT`, `SELECTONE`, `INRANGE`, `OUTRANGE`, `OTHERWISE`, `ALWAYS` | [§8](#8-control-expressions) |
| `CH$...` | [§18](#18-character-handling-ch) |
| `COMPILETIME`, `%IF`, `%ASSIGN`, other `%` functions | [§15](#15-lexical-functions-and-conditional-compilation) |
| `ENABLE`, `SIGNAL`, `LINKAGE` | [§21](#21-linkages-and-condition-handling) |
| `EXITLOOP`, `LEAVE`, `RETURN`, `LABEL` | [§8](#8-control-expressions) |
| `EXTERNAL`, `FORWARD`, `GLOBAL`, `LOCAL`, `OWN`, `REGISTER`, `STACKLOCAL` | [§9](#9-data-declarations) |
| `IF`, `INCR`, `DECR`, `WHILE`, `UNTIL`, `DO` | [§8](#8-control-expressions) |
| `MACRO`, `KEYWORDMACRO`, `%REMAINING`, `%LENGTH`, `%COUNT` | [§16](#16-macros) |
| `MODULE`, `ELUDOM` | [§19](#19-modules) |
| `PLIT`, `UPLIT`, `REP` | [§12](#12-plits-constant-tables) |
| `PSECT`, `SWITCHES`, `BUILTIN`, `UNDECLARE` | [§20](#20-other-declarations) |
| `REQUIRE`, `LIBRARY` | [§17](#17-require-and-library) |
| `ROUTINE`, `NOVALUE` | [§14](#14-routines) |

The complete list of reserved and predeclared identifiers is in LRM
Appendix A.
