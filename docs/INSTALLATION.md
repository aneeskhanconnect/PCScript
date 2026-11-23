# Building and running PCScript

## Windows installer

GitHub Actions builds `PCScript-Setup.exe` (artifact **PCScript-Setup**) from `installer/PCScript.iss`. Run it and follow the
wizard: licence, install folder (per-user by default; choose "all users" when asked for admin rights), and two optional tasks:

* **Add PCScript to the PATH** - lets you type `pcscript file.pcs` in any Command Prompt.
* **Associate .pcs files** - double-click runs the program in a console that waits for Enter; right-click offers
  *Run in PCScript terminal (keep open)* and *Edit in Notepad*.

It also installs the examples to `Documents\PCScript\Examples`, the documentation, and a Start Menu folder with a
"PCScript Command Prompt" shortcut. Uninstall from *Settings > Apps*; it removes the PATH entry and file association.
The installer currently installs the language only; the IDE will be added when it exists.

Build the installer yourself with Inno Setup 6: `ISCC.exe /DBinDir=build\bin installer\PCScript.iss` (output: `out\PCScript-Setup.exe`).

## Building from source

## Windows (Visual Studio 2022)

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\bin\pcscript.exe examples\01_variables.pcs
```

`pcscript.exe` is linked with a 128 MB stack so deeply recursive programs reach the interpreter's own
call-depth limit (default 5000, change with `--max-depth=N`) and fail with a clear error rather than crashing.

## Linux / macOS

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/bin/pcscript examples/01_variables.pcs
```

On these platforms the interpreter runs on a dedicated 256 MB thread for the same reason.

## Command-line options

| Option | Meaning |
|---|---|
| `--check` | Parse and analyse only |
| `--machine` | One-line diagnostics: `file:line:col: error E203: message` |
| `--trace` | Print `[trace] line N` to stderr before each statement |
| `--timeout=MS` | Stop with error R310 after MS milliseconds |
| `--seed=N` | Seed the random generator used by `RAND` |
| `--max-depth=N` | Maximum PCScript call depth |

Relative file names in `OPENFILE` are resolved against the folder that contains the `.pcs` file.
