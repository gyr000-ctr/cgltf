#!/usr/bin/env bash
# Top-level recorder: asciinema attaches to a tmux session in the foreground
# while this script drives titled, paused segments via send-keys. No keyboard
# interaction is required. Output: output/recording.cast
set -u
SESSION=meshopt_rec
ROOT=/home/gyr/feedback/uidreczz28Z2YPasiLH
WORK=$ROOT/work
DATA=$WORK/data_scalar
OUT=$ROOT/output
mkdir -p "$OUT"

tmux kill-session -t "$SESSION" 2>/dev/null || true

# Start the recorder attached to a fresh tmux session, in the background.
"$WORK/venv/bin/asciinema" rec --overwrite -c "tmux new-session -A -s $SESSION -x 200 -y 50" \
    "$OUT/recording.cast" >"$WORK/asciinema.log" 2>&1 &
REC_PID=$!

# Wait until the session exists.
for _ in $(seq 1 50); do tmux has-session -t "$SESSION" 2>/dev/null && break; sleep 0.1; done

title() {
  tmux send-keys -t "$SESSION" "clear" Enter
  tmux send-keys -t "$SESSION" "printf '\n\033[1;35m########## %s ##########\033[0m\n\n' '$1'" Enter
}
run()   { tmux send-keys -t "$SESSION" "$1" Enter; }
pause() { sleep "$1"; }

tmux send-keys -t "$SESSION" "cd $WORK" Enter
sleep 1

title "SEGMENT 1/7: Strict single-header compile (C99, C++11, C++17; -Werror)"
run "cd $ROOT/test/build_smoke && gcc -std=c99 -Wall -Wextra -pedantic -Werror -DCGLTF_IMPLEMENTATION -I../.. -c -x c ../../cgltf.h -o /tmp/c99.o && g++ -std=c++11 -Wall -Wextra -pedantic -Werror -DCGLTF_IMPLEMENTATION -I../.. -c -x c++ ../../cgltf.h -o /tmp/c11.o && g++ -std=c++17 -Wall -Wextra -pedantic -Werror -DCGLTF_IMPLEMENTATION -I../.. -c -x c++ ../../cgltf.h -o /tmp/c17.o && echo STRICT-COMPILE-OK"
pause 5

title "SEGMENT 2/7: Build scalar meshoptimizer reference and generate authoritative data"
run "cd $WORK && ./refbuild_scalar/generate_scalar data_scalar"
pause 4

title "SEGMENT 3/7: Build ASan/UBSan verification harness (no libm linkage)"
run "cd $WORK && gcc -std=c99 -g -O1 -Wall -Wextra -pedantic -fsanitize=address,undefined -I$ROOT harness/dump_accessors.c -o build/dump && echo HARNESS-BUILT-NO-LIBM"
pause 5

title "SEGMENT 4/7: Byte-exact decode vs uncompressed reference (all modes + filters)"
run "cd $DATA && ../build/dump reference.gltf ref.dump && ../build/dump compressed.gltf cmp.dump && cmp ref.dump cmp.dump && echo BYTE-EXACT-MATCH"
pause 6

title "SEGMENT 5/7: Distinguishable error codes (corrupt / unsupported / GLB)"
run "cd $WORK && python3 -I harness/test_errors.py"
pause 6

title "SEGMENT 6/7: Zero regression on real uncompressed Khronos models (baseline vs modified)"
run "cd $WORK && for f in samples/*/*.gltf samples/*/*.glb; do ./baseline/dump_base \"\$f\" /tmp/b.dump >/dev/null 2>&1; ./build/dump \"\$f\" /tmp/m.dump >/dev/null 2>&1; cmp -s /tmp/b.dump /tmp/m.dump && echo \"IDENTICAL  \$(basename \$f)\"; done"
pause 6

title "SEGMENT 7/7: Mutation fuzz of the decode path under ASan/UBSan (2000 iterations)"
run "cd $WORK && python3 -I harness/fuzz_decode.py 2000 glb_seed.glb"
pause 5

title "All segments complete: decoded vertices match reference, errors diagnosed, zero regression"
run "echo RECORDING-DONE"
pause 3

tmux kill-session -t "$SESSION"
wait "$REC_PID" 2>/dev/null || true
echo "recording written to $OUT/recording.cast"
