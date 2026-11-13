#!/usr/bin/env bash
# Headless demo driver: titled segments with inter-segment pauses, no keyboard waits.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
DATA_DIR="$REPO_ROOT/output/data"

title() {
	sleep 1
	echo
	printf '\033[1;36m======================================================================\033[0m\n'
	printf '\033[1;36m %s\033[0m\n' "$1"
	printf '\033[1;36m======================================================================\033[0m\n'
	sleep 1
}

cd "$REPO_ROOT"

title "Segment 1/6: Code changes - shared quantization layer (no second parse branch)"
git --no-pager diff --stat
echo "--- shared extension query + merged validity predicate in cgltf.h ---"
grep -n "cgltf_data_has_extension\|cgltf_quantized_attribute_is_valid\|has_mesh_quantization" cgltf.h
sleep 1
echo "--- writer preserves the extension via a single flag ---"
grep -n "MESH_QUANTIZATION" cgltf_write.h

title "Segment 2/6: Build through the repository CMake entrypoint"
cmake --build "$BUILD_DIR" 2>&1 | grep -E "Built target|error|warning:" || true

title "Segment 3/6: Quantization unit tests (bounded error, invalid params, round-trip, zero regression)"
"$BUILD_DIR/test_quantization"

title "Segment 4/6: Full repository entrypoint test_all.py (726 sample assets, zero regression)"
( cd "$REPO_ROOT/test" && python3 -I test_all.py ) | tail -3

title "Segment 5/6: Real Khronos Avocado - decode vs float reference and write round-trip"
( cd "$DATA_DIR" && "$BUILD_DIR/verify_avocado" \
	"$DATA_DIR/float/Avocado.gltf" "$DATA_DIR/quant/Avocado.gltf" )

title "Segment 6/6: Illegal quantization parameters are rejected"
echo "(covered inside test_quantization [3/4]; re-asserting with explicit names)"
"$BUILD_DIR/test_quantization" 2>&1 | sed -n '/\[3\/4\]/,/\[4\/4\]/p'

sleep 1
echo
printf '\033[1;32mAll segments completed without any keyboard input.\033[0m\n'
sleep 1
