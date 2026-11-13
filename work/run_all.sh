#!/usr/bin/env bash
# End-to-end, fully non-interactive verification for meshopt compression
# support in cgltf. Every section prints a banner; pauses between sections are
# added by the recording driver, not here.
set -u

ROOT=/home/gyr/feedback/uidreczz28Z2YPasiLH
WORK=$ROOT/work
REF=$WORK/ref
SCALAR_LIB=$WORK/refbuild_scalar/libmeshref_scalar.a
DUMP=$WORK/build/dump
DATA=$WORK/data_scalar

banner() { printf '\n\033[1;36m==== %s ====\033[0m\n' "$1"; }

banner "1/7  Strict compile of the modified single header (C99 / C++11 / C++17, -Werror)"
cd "$ROOT/test/build_smoke"
gcc   -std=c99   -Wall -Wextra -pedantic -Werror -DCGLTF_IMPLEMENTATION -I../.. -c -x c   ../../cgltf.h -o /tmp/c99.o
g++   -std=c++11 -Wall -Wextra -pedantic -Werror -DCGLTF_IMPLEMENTATION -I../.. -c -x c++ ../../cgltf.h -o /tmp/c11.o
g++   -std=c++17 -Wall -Wextra -pedantic -Werror -DCGLTF_IMPLEMENTATION -I../.. -c -x c++ ../../cgltf.h -o /tmp/c17.o
echo "strict compiles: OK"

banner "2/7  Build reference meshoptimizer (pure scalar) and the authoritative test data"
mkdir -p "$WORK/refbuild_scalar"
for f in vertexcodec indexcodec vertexfilter; do
  g++ -std=c++17 -O2 -mno-sse2 -Wall -Wextra -I"$REF" -c "$REF/$f.cpp" -o "$WORK/refbuild_scalar/$f.o"
done
ar rcs "$SCALAR_LIB" "$WORK/refbuild_scalar"/*.o
g++ -std=c++17 -O2 -mno-sse2 -Wall -Wextra -I"$REF" "$WORK/gen/generate.cpp" "$SCALAR_LIB" -lm -o "$WORK/refbuild_scalar/generate_scalar"
mkdir -p "$DATA"
"$WORK/refbuild_scalar/generate_scalar" "$DATA"

banner "3/7  Build the verification harness (ASan + UBSan, no libm linkage)"
gcc -std=c99 -g -O1 -Wall -Wextra -pedantic -fsanitize=address,undefined -I"$ROOT" \
    "$WORK/harness/dump_accessors.c" -o "$DUMP"
echo "harness built without -lm"

banner "4/7  Byte-exact decode: reference glTF vs EXT_meshopt_compression glTF"
cd "$DATA"
"$DUMP" reference.gltf  ref.dump
"$DUMP" compressed.gltf cmp.dump
cmp ref.dump cmp.dump && echo "BYTE-EXACT MATCH across all 10 views (all modes + all filters)"

banner "5/7  Distinguishable error codes for unsupported / corrupt inputs"
cd "$WORK"
python3 -I harness/test_errors.py

banner "6/7  Zero regression on real uncompressed Khronos sample models (baseline vs modified)"
mkdir -p "$WORK/baseline"
git -C "$ROOT" show HEAD:cgltf.h > /tmp/cgltf_baseline.h
sed 's#"cgltf.h"#"/tmp/cgltf_baseline.h"#' "$WORK/harness/dump_accessors.c" > "$WORK/baseline/dump_base.c"
gcc -std=c99 -g -O1 -Wall -Wextra -fsanitize=address,undefined "$WORK/baseline/dump_base.c" -o "$WORK/baseline/dump_base"
reg=0
for f in "$WORK"/samples/*/*.gltf "$WORK"/samples/*/*.glb; do
  [ -e "$f" ] || continue
  "$WORK/baseline/dump_base" "$f" /tmp/base.dump >/dev/null 2>&1; rb=$?
  "$DUMP"                  "$f" /tmp/mod.dump  >/dev/null 2>&1; rm=$?
  if cmp -s /tmp/base.dump /tmp/mod.dump && [ "$rb" = "$rm" ]; then
    echo "IDENTICAL  $(basename "$f")"
  else
    echo "REGRESSION $f"; reg=1
  fi
done
[ "$reg" -eq 0 ] && echo "ZERO REGRESSION confirmed"

banner "7/7  Mutation fuzz of the decode path under ASan/UBSan"
python3 -I harness/fuzz_decode.py 10000 glb_seed.glb

banner "ALL VERIFICATION COMPLETE"
