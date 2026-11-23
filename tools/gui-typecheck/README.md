# GUI type-check (developer aid)

`typecheck.sh` runs `g++ -fsyntax-only` over the Windows GUI sources using the headers in `stub/`, which are a small,
hand-written imitation of the Win32 API subset the IDE uses. It exists because the IDE was written on a machine
without the Windows SDK. It is **not** a substitute for building with MSVC: if the stub disagrees with the real
`windows.h` the real compiler wins. Do not add `stub/` to the include path of a real build.
