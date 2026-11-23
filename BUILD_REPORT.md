# Build report - PCScript 2.0.0 (engine + PCScript Studio)

## Summary

| Part | State |
|---|---|
| Language engine (`src/`) | Built and tested on Linux. 113 tests pass. One small change in this milestone: a cancel flag (`Options::cancel`, error R318) so the IDE's Stop button can end a running program. |
| PCScript Studio **core** (`studio/`, portable C++, no GUI code) | Built and tested on Linux, including under AddressSanitizer + UBSan + leak detection. |
| PCScript Studio **GUI** (`editor/`, Win32) | **Written, never compiled, never run.** Only type-checked against a hand-written Win32 stub. |
| `CMakeLists.txt` files added/changed, installer changes, CI changes | **Not run** (no CMake, no Windows, no Inno Setup in the authoring environment). |

No Windows binary of anything has been built. `pcscript-studio.exe` does not exist yet; the first time it is compiled
will be by GitHub Actions.

## Verified locally (Linux, g++ 13, `-std=c++20 -Wall -Wextra`, no warnings)

| Item | Result |
|---|---|
| Engine tests (`pcscript_tests`) | 113 passed, 0 failed (unchanged by the cancel hook) |
| Studio tests (`studio_tests`) | **38 groups, ~81,900 checks, 0 failures** |
| Same, built with AddressSanitizer + UBSan, leak detection on | 0 failures, 0 sanitizer reports, 0 leaks. (Two deliberately deep recursion tests are excluded/limited under ASan only, because ASan inflates stack frames; they pass in the normal build.) |
| Pretty printer on the engine's whole test corpus (97 programs) | Every result is idempotent and the program behaves identically before and after (same output / same error code). |
| Pretty printer on 600 randomly corrupted programs | No crash; its token-equality safety net was never triggered. |
| Random editing (16,000 operations: typing, Enter, Backspace, Tab, comment, move/duplicate/delete line, paste, cut, format ...) | Caret/selection always valid; undoing everything restores the original text exactly. |
| Bundled extension content | All 11 algorithm snippets and all 4 templates are executed and produce the expected output (e.g. bubble sort, binary search, `Stack` class, file template). |
| Passwords | SHA-256, HMAC-SHA-256 and PBKDF2 match published test vectors; passwords never appear in `accounts.json`. Create + verify with the production iteration count took ~0.3 s (-O1 build). |
| GUI sources vs Win32 stub (`tools/gui-typecheck/typecheck.sh`) | 0 errors across all 9 GUI files |

What the studio tests cover: JSON; settings validation/clamping; themes; syntax highlighting (and agreement of the editor's
scanner with the engine's lexer on every corpus program); block-structure/indentation rules; pretty printer; symbol index;
autocomplete contexts (types, `NEW`, file modes, `obj.` members with inheritance and PRIVATE, closing-keyword ranking,
built-in signatures vs the engine's table); the editing model (auto-indent, auto-close blocks, brackets/quotes, keyword
capitalisation, tabs, comments, line operations, undo/redo, find/replace, snippets, UTF-8); files (atomic save); accounts
(create/login/persist/change password/remove/corrupt file); profile, session and crash recovery; extensions (loading,
enable/disable, install/uninstall, path-traversal refusal); the threaded runner (INPUT/OUTPUT streaming, errors, Stop,
timeout, reuse, deep recursion).

### Defects found by this testing and fixed
* Snippets `bubblesort` / `insertionsort` would have shipped broken: in PCScript `BYREF` applies to every following
  parameter until `BYVAL`, so `Size : INTEGER` after `BYREF Data` rejected a literal argument. Found by running the snippets. The
  symbol index now follows the same rule.
* Moving lines down with a selection ending at column 0 of the last line left the caret past the end of the text
  (found by the random-editing test); the selection is now clamped at the end of every command.
* A variable named like a keyword (`DECLARE input : STRING`) was not recognised as a user name, so typing `input` later
  was auto-capitalised to `INPUT`.
* Auto-closing treated an outer `ENDWHILE` as closing an inner `IF`; it now looks for a closer of the same kind only.
* `widget.cpp` used `win::rgb` without including its header and mixed `int`/`LONG` in `std::max` (both would have been
  MSVC compile errors) - found by the stub type-check.

## Not verified

* **Nothing GUI-related has run.** Layout, painting (GDI), caret, scrolling, the drop-down, dialogs, the tree/list/rich-edit
  controls, DPI scaling, dark-mode scroll bars, clipboard, drag and drop - all unseen. Expect first-run visual and
  behavioural bugs. The type-check proves only that the C++ is internally consistent; every call into the real Win32 API
  is from memory of its signatures.
* MSVC may still report errors or `/W4` warnings in the GUI (and possibly in the portable core, which was compiled with GCC only).
* `studio/CMakeLists.txt`, `editor/CMakeLists.txt`, `studio.rc` / `studio.manifest` (common controls v6, per-monitor DPI,
  UTF-8), the changed `installer/PCScript.iss` and `.github/workflows/build.yml` were written without being run.
* CI will run `pcscript-studio --selftest` (also registered with CTest): it creates the real windows hidden, types into the
  editor, opens the drop-down, runs programs including INPUT and Stop, saves, formats, switches themes, and writes
  `build\selftest.log`. This is the first real exercise of the GUI and its failures are the to-do list.
* Local accounts are a convenience for shared PCs, not strong security (see `docs/IDE_GUIDE.md`).
* The earlier engine notes still apply: no interactive debugger, no performance benchmarking, syllabus corner cases untested
  against the official documents.

## Remaining work
1. Get the Windows CI green: fix MSVC compile errors, then the self-test failures, then look at it by eye.
2. Visual polish and the things listed under "Not included" in `docs/IDE_GUIDE.md` (word wrap, debugger, ...).
3. Windows verification of the installer changes.
