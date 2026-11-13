#!/usr/bin/env bash
# Record six titled asciinema segments, one .cast file per segment, with a
# pause between segments. No keyboard input is required anywhere.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
SEGS="$HERE/segs"
OUT="$HERE"

run_segment() {
	local script="$1" name="$2"
	chmod +x "$script"
	# stty gives the headless pty a real size; each script titles and pauses itself.
	script -qec "stty cols 100 rows 32; asciinema rec --overwrite -c 'bash $script' '$OUT/$name'" /dev/null
}

for s in \
	"01_design.sh:segment1_design.cast" \
	"02_validate.sh:segment2_validate.cast" \
	"03_dequant.sh:segment3_dequant.cast" \
	"04_roundtrip.sh:segment4_roundtrip.cast" \
	"05_regression.sh:segment5_regression.cast" \
	"06_acceptance.sh:segment6_acceptance.cast"
do
	script_file="${s%%:*}"
	cast_file="${s##*:}"
	echo ">>> recording $cast_file"
	run_segment "$SEGS/$script_file" "$cast_file"
	echo ">>> $cast_file saved; pausing 3s before next segment"
	sleep 3
done

echo "All segments recorded in $OUT"
ls -1 "$OUT"/segment*.cast
