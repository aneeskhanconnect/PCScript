# PCScript Studio guide

PCScript Studio (`pcscript-studio.exe`) is an integrated environment for writing and running PCScript programs:
a code editor with a suggestion drop-down, automatic indentation and a pretty printer, a console where `INPUT`
and `OUTPUT` happen, a Problems list, settings, colour themes, extensions and local user accounts.

Start it from the Start menu (or right-click a `.pcs` file and choose **Edit in PCScript Studio**).

## Signing in

On start you choose a user. **Accounts are local profiles on this computer**, handy on a shared school PC: each
person gets their own settings, open tabs, theme and folder of programs. A password is stored only as a salted
PBKDF2-HMAC-SHA256 hash. There is no online sync and **no password recovery**; anyone with full access to the
PC can still read the files on disk, so the password is a convenience, not strong protection.
*Continue as Guest* skips the password (Guest has its own profile too).

Account menu: switch user / sign out, create account, change password, delete account (the programs in your
folder are never deleted).

## Where things are stored

| What | Where |
|---|---|
| Your programs (default Save folder) | `Documents\PCScript Studio\<your user name>\` |
| Your settings, open tabs, themes, crash-recovery copies | `%APPDATA%\PCScript Studio\users\<your user name>\` |
| Accounts | `%APPDATA%\PCScript Studio\accounts.json` |
| Installed extensions | `%APPDATA%\PCScript Studio\extensions\` |

Programs are plain UTF-8 `.pcs` text files. Files are written safely (to a temporary file, then renamed) so a crash
cannot leave half a program behind. Unsaved text is also copied to a recovery store every few seconds and is
restored if the IDE or the computer stops unexpectedly.

## Writing code

* **Suggestions (autocomplete).** A list opens while you type. It knows keywords, the 24 built-in functions (with
  their signatures), your own variables, constants, procedures, functions, classes, records, enumerations,
  parameters and fields, and is aware of context: types after `:`, class names after `NEW`, `READ/WRITE/APPEND/RANDOM`
  after `OPENFILE ... FOR`, and `obj.` offers that object's fields and methods (including inherited ones;
  `PRIVATE` members only inside their class). The END keyword that closes the block you are in is ranked first.
  Snippets such as `ifelse`, `for`, `case`, `class`, `swap` are in the list too. `Ctrl+Space` opens it on demand;
  `Tab` or `Enter` accepts, `Esc` closes.
* **Auto-indent.** Enter keeps the block structure. `ELSE`, `ENDIF`, `NEXT`, `UNTIL`, `ENDCASE` ... pull themselves
  back to the right level as you type them; `CASE` branches are indented under their `CASE`.
* **Closes blocks for you.** Press Enter after `IF ... THEN`, `FOR ...`, `WHILE ... DO`, `REPEAT`, `CASE OF ...`,
  `PROCEDURE`, `FUNCTION ... RETURNS`, `CLASS` or `TYPE` and the matching `ENDIF`, `NEXT i`, ... is added (only if it is
  not already there).
* **Capitalises keywords.** Typing `endif` becomes `ENDIF`. Words that could be your own variable names (`step`,
  `read`, `write`, `random`, `date`, `new` ...) are never changed, and neither is a word you have declared yourself.
* **Closes brackets and quotes**, wraps a selection in `"..."` or `(...)`, and types over the closing character.
* **Pretty print.** *Edit > Format Document* (`Shift+Alt+F`) re-indents, puts single spaces around operators and after
  commas, upper-cases keywords and tidies blank lines. It works on unfinished code and never changes what a program
  does: the result is compared token by token with the original and refused if anything would differ.
* Matching bracket highlight, indentation guides, line numbers, current-line highlight, comment toggle (`Ctrl+/`),
  duplicate / delete / move line, multi-line indent, find / replace / go to line, unlimited undo/redo.
* **Red squiggles** under errors appear about half a second after you stop typing; the **Problems** tab lists them
  (double-click to jump). Yellow marks are warnings.

## Running programs

`F5` runs the program in the **Output** panel. `OUTPUT` appears there; when the program reaches `INPUT`, the
status bar says so and the cursor moves to the input box under the output: type the value and press Enter
(Up/Down recall earlier answers). `Shift+F5` stops a running program (infinite loops included). Errors are shown
with the offending line and a hint, and are marked in the editor. Relative file names in `OPENFILE` refer to the
folder of the file being run (or your programs folder for an unsaved file).

The IDE runs programs inside itself; it does **not** have a step-by-step debugger (the engine can only trace lines).

## Settings (`Tools > Settings`, `Ctrl+,`)

* **Editor:** font (any monospace font installed), size, line spacing (100-250 % of the font height), tab size,
  spaces or tabs, caret style, line numbers, current-line highlight, indentation guides, show spaces/tabs, bracket
  matching.
* **Typing:** auto-indent, close brackets, close blocks, capitalise keywords, the suggestion drop-down and how many
  characters open it, live error checking.
* **Files:** format on save, trim trailing spaces, final blank line, auto-save every N seconds, reopen my files.
* **Run:** save before running, clear output before each run, stop after N seconds, maximum recursion depth, output font.
* **Theme:** choose a colour theme (previewed live), import a theme file.

`Ctrl` + mouse wheel or `Ctrl+=` / `Ctrl+-` / `Ctrl+0` zooms. Values typed into the settings file by hand are
range-checked, so a typo cannot make the IDE unusable. Settings are stored per user.

## Themes

Built in: *PCScript Dark*, *PCScript Light*, *Classic Blue* (Visual Basic 6 style), *High Contrast*, *Solarized Dark*,
*Midnight*; extensions can add more. To make your own, save a `.json` file in `Tools > Open My Themes Folder`:

```json
{
  "name": "My Theme",
  "base": "dark",
  "colors": { "keyword": "#FF8800", "string": "#00CC66", "editorBg": "#101010" }
}
```

`base` is `dark`, `light` or the name of any other theme; colours you leave out come from it. Colour names:

* **Editor:** `editorBg`, `editorFg`, `gutterBg`, `gutterFg`, `gutterFgActive`, `currentLine`, `selection`, `caret`, `indentGuide`, `bracketMatch`, `error`, `warning`
* **Syntax:** `keyword`, `type`, `builtin`, `string`, `number`, `comment`, `operator`, `constant`
* **Window chrome:** `uiBg`, `uiFg`, `uiBorder`, `panelBg`, `panelFg`, `tabActiveBg`, `tabInactiveBg`, `tabFg`, `accent`, `statusBg`, `statusFg`, `popupBg`, `popupFg`, `popupSelBg`, `popupSelFg`
* **Output panel:** `consoleBg`, `consoleFg`, `consoleInput`, `consoleError`

## Extensions

An extension is a folder with an `extension.json`. Extensions contain **data only** (themes, snippets, program
templates) and cannot run code, so installing one cannot harm the computer. `Tools > Extensions` lists them, turns
them on and off, installs one from a folder and uninstalls it. Three ship with the IDE: *Ocean Themes*, *Cambridge
Algorithm Snippets* (searching, sorting, recursion, string and stack-class snippets, all tested) and *Starter Program
Templates* (File > New From Template).

```json
{
  "id": "my.extension", "name": "My Extension", "version": "1.0.0", "author": "Me", "description": "What it adds",
  "contributes": {
    "themes":    [ { "file": "themes/mine.json" }, { "name": "Inline", "base": "light", "colors": { "keyword": "#0000FF" } } ],
    "snippets":  [ { "prefix": "fact", "description": "Factorial", "body": "FUNCTION Fact(N : INTEGER) RETURNS INTEGER\n    ...\nENDFUNCTION" } ],
    "templates": [ { "name": "My template", "description": "...", "file": "templates/mine.pcs" } ]
  }
}
```

In a snippet `body`, `$0` marks where the caret ends up. Files named in a manifest must stay inside the extension's
folder.

## Keyboard shortcuts

See *Help > Keyboard Shortcuts*. The main ones: `Ctrl+N/O/S` new/open/save, `F5` run, `Shift+F5` stop, `F7` check,
`Ctrl+Space` suggestions, `Ctrl+F/H` find/replace, `Ctrl+G` go to line, `Ctrl+/` comment, `Alt+Up/Down` move line,
`Shift+Alt+F` format.

## Not included

No debugger with breakpoints, no word wrap, no multiple cursors, no split editor, no right-to-left or
double-width-character layout (each character occupies one column), and the title/menu bar colours follow Windows
rather than the theme.
