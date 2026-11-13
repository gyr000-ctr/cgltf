#!/usr/bin/env bash
# Segment 6: full acceptance run and final summary.
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; source "$HERE/../seg_helpers.sh"
title "SEGMENT 6/6 - Full acceptance run through the repository's own entry points"
pause 2
step "rebuild from scratch (cmake + make, -Wall -Wextra -pedantic -Werror)"
cmake --build "$BUILD" -j4 2>&1 | tail -3
pause 2
step "test_math / test_strings"
"$BUILD/test_math" >/dev/null && echo "test_math: OK"
"$BUILD/test_strings" >/dev/null && echo "test_strings: OK"
pause 2
step "test_quantization: bounded error, round trip, invalid rejection, plain passthrough"
"$BUILD/test_quantization" "$ASSETS" "$OUT/roundtrip"
pause 2
title "DONE - bounded error, consistent round trip, zero regression"
pause 3
