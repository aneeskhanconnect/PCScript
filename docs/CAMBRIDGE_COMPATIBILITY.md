# Cambridge compatibility

PCScript implements the pseudocode constructs used in Cambridge IGCSE Computer Science 0478, O Level 2210
and AS & A Level 9618 programming questions. **It has not been validated against the official syllabus
documents line by line, and no claim of full specification compliance is made.** The tables below say what is
implemented, what differs, and what is missing.

## Implemented

* Variables, constants, all six basic types, 1D/2D arrays with custom bounds, records, enumerations, pointers.
* Selection (IF, CASE with values, lists, TO ranges, OTHERWISE), FOR/WHILE/REPEAT.
* Procedures, functions, BYVAL/BYREF, recursion, global and local scope.
* Classes: constructors (`NEW`), inheritance, `SUPER`, PUBLIC/PRIVATE, overriding, polymorphism.
* Text files (READ/WRITE/APPEND) and random-access files (SEEK/GETRECORD/PUTRECORD).
* The string, numeric, conversion and date library functions listed in `LANGUAGE_REFERENCE.md`.

## Deliberate differences and choices

| Topic | PCScript behaviour |
|---|---|
| Keyword case | Keywords must be UPPER CASE. Identifiers are case-insensitive. |
| String join | Only `&` joins strings. `+` on strings is an error (E203/R303) with a hint. |
| INTEGER size | 64-bit, not 32-bit. |
| Uninitialised variables | Get a default value instead of raising an error. |
| REAL output | Shortest form up to 15 digits: `8 / 2` prints `4`. |
| `RAND(x)` | Returns a REAL with 0 <= r < x. |
| `DAYINDEX` | Sunday = 1. |
| Loop variable after FOR | Holds the last value plus STEP. |
| Random-access files | Text-based, one record per line, numbered from 1, tab separated. Files are **not** byte-compatible with fixed-width binary records written by other tools, and a record has no fixed size. |
| BYREF | Copy-in/copy-out. Aliasing a variable through two BYREF parameters of the same call behaves differently from true references. |
| Pointers | `^x` works on simple variables only; there is no dynamic allocation. |
| Array assignment | Copies; both arrays must have identical bounds. |
| `ELSE IF` | Write a nested `IF ... ENDIF`; there is no `ELSEIF`. |

## Not implemented

* Date literals, array literals/initialisers, `PROTECTED`, abstract classes, overloaded constructors.
* `OPENFILE ... FOR READWRITE`, binary file formats.
* Static typing of record fields and object members: type errors there are reported at run time (R303).
* Interactive debugger: breakpoints, stepping, variable and call-stack inspection. Only line tracing exists.
* Any behaviour of the 9618 pseudocode guide not listed in `LANGUAGE_REFERENCE.md`.

Where a Cambridge document and PCScript disagree, please report it with the page reference.
