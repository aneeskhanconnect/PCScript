# PCScript

PCScript is an interpreter for Cambridge-style pseudocode (`.pcs` files) written in C++20.
It is aimed at students working through Cambridge IGCSE Computer Science 0478, O Level 2210 and
AS & A Level 9618 programming tasks.

**Status of this repository (v2.0.0, work in progress)**

| Component | State |
|---|---|
| `pcscript` language engine (lexer, parser, semantic analysis, interpreter, standard library) | Implemented. Built and tested on Linux (GCC 13, CMake, sanitizers). |
| Test suite (`pcscript_tests`) | Implemented. All tests pass on Linux. |
| `studio/`: portable core of the IDE (editor model, pretty printer, autocomplete, settings, themes, accounts, extensions, program runner) | Implemented. **38 test groups / ~82,000 checks pass on Linux**, also under AddressSanitizer + UBSan. |
| Windows build via GitHub Actions (`.github/workflows/build.yml`) | Written, **not yet run**. |
| `pcscript-studio` (`editor/`, the Windows GUI on top of `studio/`) | **Written but never compiled or run.** Only type-checked against a hand-written stub of the Win32 API. GitHub Actions will compile it and run `pcscript-studio --selftest`. See `docs/IDE_GUIDE.md`. |
| Windows installer (`installer/PCScript.iss`, Inno Setup 6) | Written. Installs the PCScript language (command line), examples, docs, optional PATH and `.pcs` association. **Not yet built or run**: GitHub Actions builds `PCScript-Setup.exe` and silent-tests it. Updated to also install `pcscript-studio.exe`, the extensions and shortcuts (still unbuilt). |

See `BUILD_REPORT.md` for exactly what was verified and what was not.

## Build

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Binaries are written to `build/bin/` on every platform. Requires CMake 3.20+ and a C++20 compiler
(MSVC 2022, GCC 11+, Clang 14+).

## Run

`pcscript.exe` is a command-line program: double-clicking it with no file only shows the usage text. Open Command Prompt
in its folder and run `pcscript.exe examples\01_variables.pcs`, or drag a `.pcs` file onto the exe.

```
pcscript examples/01_variables.pcs
pcscript --check program.pcs          # analyse only
pcscript --machine --check program.pcs  # file:line:col: error CODE: message (for editors)
pcscript --trace program.pcs          # print each executed line to stderr
```

Exit codes: `0` success, `1` compile error, `2` runtime error, `3` usage/file error.

## Layout

```
src/common     diagnostics (error codes, formatting)
src/lexer      tokenizer
src/ast        syntax tree
src/parser     recursive-descent parser
src/semantic   declarations, scopes, calls, static type checks, warnings
src/runtime    values and the tree-walking interpreter
src/stdlib     built-in functions
src/pcscript.cpp  command-line front end
assets/        logo and icons (logo_original.png is the unmodified source image)
examples/      eleven runnable programs
tests/         lexer/parser/runtime/language tests
docs/          language reference, compatibility notes, installation
```

Documentation: `docs/LANGUAGE_REFERENCE.md`, `docs/CAMBRIDGE_COMPATIBILITY.md`, `docs/INSTALLATION.md`.
