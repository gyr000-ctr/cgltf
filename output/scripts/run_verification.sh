#!/usr/bin/env bash
# Non-interactive verification for KHR_mesh_quantization support.
# Uses only the repository's own CMake/Python test entrypoints plus the
# real-asset end-to-end checker in output/scripts.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
DATA_DIR="$REPO_ROOT/output/data"

echo "== Repo: $REPO_ROOT"

echo
echo "### [1/4] Build via the repository CMake entrypoint"
mkdir -p "$BUILD_DIR"
cmake -S "$REPO_ROOT/test" -B "$BUILD_DIR" >/dev/null
cmake --build "$BUILD_DIR"
g++ -std=c++11 -g -fsanitize=address -I"$REPO_ROOT" \
	"$SCRIPT_DIR/verify_avocado.cpp" -o "$BUILD_DIR/verify_avocado"

echo
echo "### [2/4] Quantization unit tests (decode bounds, invalid params, round-trip, no-extension)"
"$BUILD_DIR/test_quantization"

echo
echo "### [3/4] Repository test_all.py (726 sample assets, zero-regression gate)"
( cd "$REPO_ROOT/test" && python3 -I test_all.py ) | tail -4

echo
echo "### [4/4] Real quantized asset: bounded decode error vs float reference + write round-trip"
( cd "$DATA_DIR" && "$BUILD_DIR/verify_avocado" \
	"$DATA_DIR/float/Avocado.gltf" "$DATA_DIR/quant/Avocado.gltf" )

echo
echo "ALL VERIFICATION STAGES PASSED"
