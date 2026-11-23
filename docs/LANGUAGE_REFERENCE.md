# PCScript language reference

Everything here is implemented and covered by `tests/`. Anything not listed is not supported
(see `CAMBRIDGE_COMPATIBILITY.md`).

## Source basics

* Files use the extension `.pcs`; UTF-8, LF or CRLF line endings.
* One statement per line. `// comment` runs to the end of the line.
* **Keywords are UPPER CASE** (`IF`, `ENDIF`, `INTEGER` ...). **Identifiers are case-insensitive**
  (`Count`, `COUNT` and `count` are the same variable). The built-in type names `INTEGER`, `REAL`, `STRING`,
  `CHAR`, `BOOLEAN`, `DATE` are also accepted in lower case.
* Assignment is `<-` or the arrow glyph `←`. Comparison operators: `=  <>  <  >  <=  >=` (also `≤ ≥ ≠`).
* Strings use double quotes `"text"`, a CHAR uses single quotes `'a'` and must contain exactly one character.

## Types and declarations

```
DECLARE Age : INTEGER
DECLARE Name, City : STRING
DECLARE Marks : ARRAY[1:10] OF INTEGER
DECLARE Grid : ARRAY[0:2, 0:2] OF REAL
CONSTANT Pi <- 3.14159
```

| Type | Notes |
|---|---|
| INTEGER | 64-bit signed. Overflow is detected (R317). |
| REAL | Double precision. INTEGER values widen to REAL on assignment. |
| STRING / CHAR | A CHAR may be assigned to a STRING; the reverse is an error. |
| BOOLEAN | `TRUE`, `FALSE` |
| DATE | Create with `SETDATE`/`TODAY`; prints as `dd/mm/yyyy`; comparable. |

Variables start with a default value (0, 0.0, empty string, space, FALSE, 01/01/1970, first enum value,
NULL for objects and pointers). Re-executing the same `DECLARE` (for example inside a loop) re-initialises it.

User-defined types:

```
TYPE Student                       // record
    DECLARE Name : STRING
    DECLARE Marks : ARRAY[1:3] OF INTEGER
ENDTYPE
TYPE Season = (Spring, Summer, Autumn, Winter)   // enumeration
TYPE IntPointer = ^INTEGER                        // pointer
```

Records are copied by value on assignment and when passed BYVAL. Enumeration values compare with
`= <> < > <= >=` by position. Pointers: `p <- ^x` stores the address of the simple variable `x`; `p^` reads
or writes the variable it points to; dereferencing an unset pointer is error R305.

## Expressions

Precedence, highest first: `^` (right-associative), unary `-`, `* / DIV MOD`, `+ -`, `&` (string join),
comparisons, `NOT`, `AND`, `OR`. So `-2 ^ 2` is `-4`.

* `/` always produces a REAL; `DIV` and `MOD` need INTEGERs and truncate towards zero.
* `+` does **not** join strings; use `&` (`"a" & NUM_TO_STR(3)`).
* `AND` and `OR` short-circuit.

## Statements

```
IF cond THEN ... [ELSE ...] ENDIF
CASE OF x
    1 : ...
    2, 3 : ...
    4 TO 9 : ...
    OTHERWISE : ...
ENDCASE
FOR i <- a TO b [STEP s] ... NEXT [i]
WHILE cond DO ... ENDWHILE
REPEAT ... UNTIL cond
INPUT target            OUTPUT expr, expr, ...
```

`OUTPUT` prints its items without separators and ends the line. REAL values are printed with up to 15
significant digits and no trailing zeros (`OUTPUT 8 / 2` prints `4`). `INPUT` reads one line and converts it to the
target's type (INTEGER, REAL, BOOLEAN as `TRUE`/`FALSE` in any case, STRING, CHAR).

The FOR loop variable must be a declared INTEGER; start, end and step are evaluated once.

## Procedures and functions

```
PROCEDURE Swap(BYREF A : INTEGER, BYREF B : INTEGER)
    ...
ENDPROCEDURE
FUNCTION Area(R : REAL) RETURNS REAL
    RETURN 3.14159 * R ^ 2
ENDFUNCTION
CALL Swap(X, Y)        Area(2.0)
```

* Parameters default to BYVAL. BYREF arguments must be variables, array elements or fields; BYREF is
  implemented as copy-in / copy-out.
* Array parameters may omit bounds: `Data : ARRAY OF INTEGER`.
* Routines can be defined before or after their first use. Procedures and functions may use global variables.
  Locals shadow globals. Recursion is supported up to a configurable depth.
* `CALL name` may omit empty brackets; `CALL` itself may be omitted for procedures.

## Classes

```
CLASS Dog INHERITS Animal
    PRIVATE Tricks : INTEGER
    PUBLIC PROCEDURE NEW(GivenName : STRING)
        SUPER.NEW(GivenName, 4)
        Tricks <- 0
    ENDPROCEDURE
    PUBLIC FUNCTION GetTricks() RETURNS INTEGER
        RETURN Tricks
    ENDFUNCTION
ENDCLASS
DECLARE Rex : Dog
Rex <- NEW Dog("Rex")
CALL Rex.Learn()
```

Constructors are `PROCEDURE NEW`. Methods are looked up from the object's real class (overriding and
polymorphism work). `SUPER.NEW(...)` and `SUPER.Method(...)` call the parent class. PRIVATE members can only be
used inside methods of the class that declares them (error R308 otherwise). Objects are references.

## Files

```
OPENFILE "data.txt" FOR READ | WRITE | APPEND | RANDOM
READFILE "data.txt", Variable        WRITEFILE "data.txt", expression
CLOSEFILE "data.txt"                 EOF("data.txt")
SEEK "scores.dat", 3   GETRECORD "scores.dat", Rec   PUTRECORD "scores.dat", Rec
```

READ files are read a line at a time into STRING, CHAR, INTEGER, REAL or BOOLEAN variables. RANDOM files
store one record per line (records are numbered from 1; `PUTRECORD`/`GETRECORD` use the current position and
advance it). Record fields in RANDOM files must be simple types. Files are located relative to the folder of the
`.pcs` file. Open files are flushed when the program ends.

## Built-in functions

| Function | Result |
|---|---|
| `LENGTH(s)` | INTEGER |
| `LEFT(s, n)`, `RIGHT(s, n)` | STRING |
| `MID(s, start, length)`, `SUBSTRING(s, start, length)` | STRING (positions start at 1) |
| `UCASE(x)`, `LCASE(x)`, `TO_UPPER(s)`, `TO_LOWER(s)` | same kind as the argument |
| `CHR(n)`, `ASC(c)` | CHAR / INTEGER (0-255) |
| `INT(x)` | INTEGER, truncates towards zero |
| `RAND(x)` | REAL, 0 <= result < x |
| `ROUND(x, places)` | REAL |
| `NUM_TO_STR(n)`, `STR_TO_NUM(s)`, `IS_NUM(s)` | STRING / INTEGER or REAL / BOOLEAN |
| `SETDATE(day, month, year)`, `TODAY()` | DATE |
| `DAY(d)`, `MONTH(d)`, `YEAR(d)` | INTEGER |
| `DAYINDEX(d)` | INTEGER, Sunday = 1 ... Saturday = 7 |
| `EOF(file)` | BOOLEAN |

## Diagnostics

Every message has a code, file, line, column, a source excerpt and, where possible, a suggestion.

| Code | Meaning |
|---|---|
| E101 | Lexical error (unterminated string, bad character, bad CHAR literal) |
| E102 / E103 | Syntax error / block never closed |
| E201 | Undeclared variable |
| E202 | Name declared twice |
| E203 | Type mismatch found by static analysis |
| E204 | Unknown routine, or procedure/function used the wrong way |
| E205 | Wrong number of arguments |
| E206 | Assignment to a constant |
| E207 | RETURN or SUPER used where it is not allowed |
| E208 | Unknown type or class |
| E209 | Condition is not BOOLEAN |
| W501 | Variable declared but never used (warning) |
| R301 | Division by zero |
| R302 | Array index out of bounds |
| R303 | Type error at run time |
| R304 | Invalid conversion or INPUT value |
| R305 | Uninitialised object/pointer, or function without RETURN |
| R306 | File error |
| R307 | Call depth exceeded (runaway recursion) |
| R308 | PRIVATE member accessed from outside its class |
| R309 | INPUT with no data left |
| R310 | Execution timeout |
| R311-R315 | Unknown name/type/member/method, duplicate definition, wrong argument count |
| R316 | Invalid argument to a built-in function |
| R317 | INTEGER overflow |
| R318 | Execution stopped by the user (IDE Stop button) |

Programs with E-class errors are not run. Warnings never stop a program.

## Tracing

`pcscript --trace file.pcs` prints `[trace] line N` to standard error before each statement. This is the
execution-tracing foundation for a future debugger; breakpoints, stepping and variable inspection are **not**
implemented yet.
