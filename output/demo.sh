#!/usr/bin/env bash
# Non-interactive segmented demo for the KHR/EXT_meshopt_compression work.
# Each segment prints a titled banner, pauses, then runs its commands.
set -u
REPO=/home/gyr/feedback/uid5205
cd "$REPO/test"

banner() {
	printf '\n\033[1;36m======================================================================\033[0m\n'
	printf '\033[1;36m  %s\033[0m\n' "$1"
	printf '\033[1;36m======================================================================\033[0m\n'
	sleep 2
}
run() {
	printf '\033[1;33m$ %s\033[0m\n' "$*"
	sleep 1
	"$@"
	sleep 1
}

banner "Segment 1/5  Architecture: shared read entry point, no duplicated parser"
run grep -n "cgltf_decode_meshopt_views\|cgltf_buffer_view_data(view" ../cgltf.h
run sed -n '1546,1561p' ../cgltf.h
sleep 1

banner "Segment 2/5  Distinguishable error codes"
run grep -n "cgltf_result_unsupported_extension\|cgltf_result_invalid_compressed_data" ../cgltf.h
run ./build/cgltf_test_nohook build/meshopt-fixtures/ext.gltf
sleep 1

banner "Segment 3/5  Build the decoder layer and the end-to-end tests"
run cmake --build build --target cgltf_meshopt test_meshopt gen_meshopt_fixtures -j4
sleep 1

banner "Segment 4/5  Generate fixtures and verify decode == reference, errors diagnosable"
run rm -rf build/meshopt-fixtures
run mkdir -p build/meshopt-fixtures
run ./build/gen_meshopt_fixtures build/meshopt-fixtures
run ./build/test_meshopt build/meshopt-fixtures
sleep 1

banner "Segment 5/5  Zero regression: full official suite on 726 sample models"
run python3 test_all.py
printf '\n\033[1;32mAll segments complete.\033[0m\n'
sleep 1
