#!/bin/bash
# Syntax/type-checks every GUI source in editor/ with g++ against a HAND-WRITTEN STUB of the Win32 API subset the GUI uses
# (tools/gui-typecheck/stub). It catches ordinary C++ mistakes (typos, missing includes, wrong member names, mismatched
# signatures between our own files) on a machine without the Windows SDK. It does NOT prove the code builds with MSVC:
# the stub reflects the author's memory of the Win32 API, not the real headers. The real check is the GitHub Actions build.
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
status=0
for f in win_util widget editor_view dialogs app_core app_ui app_run app_selftest main; do
  echo "== editor/$f.cpp"
  g++ -std=c++20 -fsyntax-only -Wall -Wextra -Wno-missing-field-initializers -DUNICODE -D_UNICODE -DNOMINMAX \
      -I"$ROOT/tools/gui-typecheck/stub" -I"$ROOT/src" -I"$ROOT/studio" -I"$ROOT/editor" "$ROOT/editor/$f.cpp" || status=1
done
[ $status -eq 0 ] && echo "All GUI sources type-check against the stub." || echo "Type errors found."
exit $status
