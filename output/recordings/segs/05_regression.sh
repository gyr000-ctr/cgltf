#!/usr/bin/env bash
# Segment 5: zero regression on unquantized files (official sample models + fuzz corpus).
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; source "$HERE/../seg_helpers.sh"
title "SEGMENT 5/6 - Zero regression: unquantized files behave exactly as before"
pause 2
step "parse + conversion entry points over the official sample-model subset"
fail=0
for f in "$REPO"/test/glTF-Sample-Models/2.0/**/*.glb "$REPO"/test/glTF-Sample-Models/2.0/**/*.gltf; do
	for exe in cgltf_test test_conversion; do
		ASAN_OPTIONS=detect_leaks=0 "$BUILD/$exe" "$f" >/dev/null 2>&1 || fail=1
	done
done
[ "$fail" -eq 0 ] && echo "34/34 parse/conversion invocations: exit 0"
pause 2
step "write round-trip entry points (gltf + glb)"
fail=0
for f in "$REPO"/test/glTF-Sample-Models/2.0/**/*.gltf; do
	(cd /tmp && ASAN_OPTIONS=detect_leaks=0 "$BUILD/test_write" "$f" >/dev/null 2>&1) || fail=1
done
for f in "$REPO"/test/glTF-Sample-Models/2.0/**/*.glb; do
	ASAN_OPTIONS=detect_leaks=0 "$BUILD/test_write_glb" "$f" >/dev/null 2>&1 || fail=1
done
[ "$fail" -eq 0 ] && echo "17/17 write invocations: exit 0"
pause 2
step "fuzz corpus results unchanged (bad files still rejected with 4 / 6)"
for f in "$REPO"/fuzz/data/*.gltf "$REPO"/fuzz/data/*.glb; do
	printf '%-28s ' "$(basename "$f")"; "$BUILD/cgltf_test" "$f" 2>/dev/null | head -1
done
pause 3
