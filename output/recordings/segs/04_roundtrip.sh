#!/usr/bin/env bash
# Segment 4: write -> parse round trip preserves the extension (gltf and glb).
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; source "$HERE/../seg_helpers.sh"
title "SEGMENT 4/6 - Write round trip: extension and quantizedBits are preserved"
pause 2
step "writer emits the accessor extension block and registers extensionsUsed"
grep -n "MESH_QUANTIZATION" "$REPO/cgltf_write.h"
pause 2
step "round-trip JSON written by the acceptance test"
python3 -I -c "
import json
d=json.load(open('$OUT/roundtrip/rt_quant_i8.gltf'))
print('accessor extension:', json.dumps(d['accessors'][0]['extensions']))
print('extensionsUsed    :', d['extensionsUsed'])
"
pause 2
step "GLB binary chunk path round trips as well"
ls -1 "$OUT/roundtrip"/rt_quant_i8.glb && "$BUILD/cgltf_test" "$OUT/roundtrip/rt_quant_i8.glb" 2>/dev/null
pause 3
