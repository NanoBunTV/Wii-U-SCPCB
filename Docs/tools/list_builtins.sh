#!/usr/bin/env bash
# Prints the Blitz3D builtins referenced by a disassembled Blitz3D exe.
# Usage: Docs/tools/list_builtins.sh _ref/decomp/scpcb_disasm
# Run Blitz3DDisasm on the exe first (see Docs/BUILDING.md).
set -euo pipefail
dir="${1:?usage: list_builtins.sh <exe>_disasm directory}"
echo "== commands"
sed -nE 's/.*_builtIn_f(.*)$/\1/p' "$dir/BuiltIn/BuiltIn.txt" | sort -u
echo "== compiler intrinsics"
sed -nE 's/.*_builtIn__(.*)$/\1/p' "$dir/BuiltIn/BuiltIn.txt" | sort -u
echo "== DLL imports"
cat "$dir"/Libs/*.txt
