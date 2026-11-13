#!/usr/bin/env bash
# Segment 2: parsing and validation of illegal quantization parameters.
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; source "$HERE/../seg_helpers.sh"
title "SEGMENT 2/6 - JSON parsing and validation: illegal parameters are rejected"
pause 2
step "accessor-level KHR_mesh_quantization parser"
sed -n '/static int cgltf_parse_json_mesh_quantization/,/^}/p' "$REPO/cgltf.h" | head -40
pause 2
step "validation rules inside cgltf_validate"
sed -n '/if (accessor->has_mesh_quantization)/,/extension_declared/p' "$REPO/cgltf.h" | head -42
pause 2
step "each malformed asset is rejected with cgltf_result_invalid_gltf (4)"
for f in "$ASSETS"/invalid_*.gltf; do
	printf '%-32s ' "$(basename "$f")"
	"$BUILD/cgltf_test" "$f" 2>/dev/null | head -1
done
pause 3
