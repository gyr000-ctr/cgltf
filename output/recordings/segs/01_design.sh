#!/usr/bin/env bash
# Segment 1: problem statement and the single read-path design.
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; source "$HERE/../seg_helpers.sh"
title "SEGMENT 1/6 - Problem and design: merge quantization into the existing read path"
pause 2
step "grep the existing normalized read path that every accessor already uses"
grep -n "cgltf_component_read_float" "$REPO/cgltf.h" | head -6
pause 2
step "new public types on cgltf_accessor (no parallel parser is introduced)"
grep -n "has_mesh_quantization\|cgltf_mesh_quantization" "$REPO/cgltf.h" | head -8
pause 2
step "the dequant transform is one extra parameter on the shared function"
sed -n '/static cgltf_float cgltf_component_read_float/,/return (cgltf_float)cgltf_component_read_integer/p' "$REPO/cgltf.h" | head -45
pause 3
