#!/usr/bin/env bash
# Shared presentation helpers for the segmented KHR_mesh_quantization recording.
# Fully non-interactive: every segment prints its own title and pauses itself.
set -u
shopt -s globstar nullglob

REPO=/home/gyr/feedback/uid5241
BUILD=$REPO/test/build
ASSETS=$REPO/test/data/quantization
OUT=$REPO/output

pause() { sleep "${1:-2}"; }

title() {
	local text="$1"
	echo
	printf '########################################################################\n'
	printf '# %s\n' "$text"
	printf '########################################################################\n'
	echo
}

step() {
	echo
	printf '\033[1;36m$ %s\033[0m\n' "$*"
}
