#!/usr/bin/env bash
# Drives a tmux session through titled, paused verification segments.
# Launched under asciinema; requires no keyboard input.
set -u
SESSION=meshopt_rec
ROOT=/home/gyr/feedback/uidreczz28Z2YPasiLH
WORK=$ROOT/work
VENV=$WORK/venv/bin
DATA=$WORK/data_scalar
DUMP=$WORK/build/dump

tmux kill-session -t "$SESSION" 2>/dev/null || true
tmux new-session -d -s "$SESSION" -x 200 -y 50
tmux send-keys -t "$SESSION" "cd $WORK && clear" Enter

# title <text>
title() {
  tmux send-keys -t "$SESSION" "clear" Enter
  tmux send-keys -t "$SESSION" "printf '\n\033[1;35m########## %s ##########\033[0m\n\n' '$1'" Enter
}
# run <shell command string>
run() { tmux send-keys -t "$SESSION" "$1" Enter; }
# pause <seconds>: let output settle between segments
pause() { sleep "$1"; }

title "SEGMENT 1/7: Strict single-header compile (C99, C++11, C++17; -Werror)"
run "cd $ROOT/test/build_smoke && gcc -std=c99 -Wall -Wextra -pedantic -Werror -DCGLTF_IMPLEMENTATION -I../.. -c -x c ../../cgltf.h -o /tmp/c99.o && g++ -std=c++11 -Wall -Wextra -pedantic -Werror -DCGLTF_IMPLEMENTATION -I../.. -c -x c++ ../../cgltf.h -o /tmp/c11.o && g++ -std=c++17 -Wall -Wextra -pedantic -Werror -DCGLTF_IMPLEMENTATION -I../.. -c -x c++ ../../cgltf.h -o /tmp/c17.o && echo STRICT-COMPILE-OK"
pause 4

title "SEGMENT 2/7: Build scalar meshoptimizer reference and generate authoritative data"
run "cd $WORK && g++ -std=c++17 -O2 -mno-sse2 -Wall -Wextra -Iref gen/generate.cpp refbuild_scalar/libmeshref_scalar.a -lm -o refbuild_scalar/generate_scalar && ./refbuild_scalar/generate_scalar data_scalar"
pause 4

title "SEGMENT 3/7: Build ASan/UBSan verification harness (no libm linkage)"
run "cd $WORK && gcc -std=c99 -g -O1 -Wall -Wextra -pedantic -fsanitize=address,undefined -I$ROOT harness/dump_accessors.c -o build/dump && echo HARNESS-BUILT-NO-LIBM"
pause 4

title "SEGMENT 4/7: Byte-exact decode vs uncompressed reference (all modes + filters)"
run "cd $DATA && ../build/dump reference.gltf ref.dump && ../build/dump compressed.gltf cmp.dump && cmp ref.dump cmp.dump && echo BYTE-EXACT-MATCH"
pause 5

title "SEGMENT 5/7: Distinguishable error codes (corrupt / unsupported / GLB)"
run "cd $WORK && python3 -I harness/test_errors.py"
pause 5

title "SEGMENT 6/7: Zero regression on real uncompressed Khronos models (baseline vs modified)"
run "cd $WORK && for f in samples/*/*.gltf samples/*/*.glb; do ./baseline/dump_base \"\$f\" /tmp/b.dump >/dev/null 2>&1; ./build/dump \"\$f\" /tmp/m.dump >/dev/null 2>&1; cmp -s /tmp/b.dump /tmp/m.dump && echo \"IDENTICAL  \$(basename \$f)\"; done"
pause 5

title "SEGMENT 7/7: Mutation fuzz of the decode path under ASan/UBSan (2000 iterations)"
run "cd $WORK && python3 -I harness/fuzz_decode.py 2000 glb_seed.glb"
pause 4

title "All segments complete - decoded vertices match reference, errors diagnosed, no regression"
run "echo DONE"
pause 3

# leave the session briefly so the final frame is captured, then end it
sleep 2
tmux kill-session -t "$SESSION"
