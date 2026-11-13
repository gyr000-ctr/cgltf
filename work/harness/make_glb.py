#!/usr/bin/env python3
"""Build GLB variants (valid + tail-corrupted) for meshopt decode testing."""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "..", "data_scalar")


def pad4(b, fill):
    while len(b) % 4:
        b += bytes([fill])
    return b


def build_glb(json_obj, bin_blob):
    js = pad4(json.dumps(json_obj, separators=(",", ":")).encode(), 0x20)
    bn = pad4(bin_blob, 0x00)
    total = 12 + 8 + len(js) + 8 + len(bn)
    out = b"glTF" + struct.pack("<II", 2, total)
    out += struct.pack("<II", len(js), 0x4E4F534A) + js
    out += struct.pack("<II", len(bn), 0x004E4942) + bn
    return out


def main():
    manifest = json.load(open(os.path.join(DATA, "manifest.json")))
    blob = bytearray(open(os.path.join(DATA, "compressed.bin"), "rb").read())
    pos = next(v for v in manifest["views"] if v["name"] == "position")

    gltf = {
        "asset": {"version": "2.0", "generator": "meshopt-cgltf-glb-test"},
        "extensionsUsed": ["EXT_meshopt_compression"],
        "extensionsRequired": ["EXT_meshopt_compression"],
        "buffers": [{"byteLength": len(blob)}],
        "bufferViews": [{
            "buffer": 0,
            "byteOffset": 0,
            "byteLength": pos["stride"] * pos["count"],
            "byteStride": pos["stride"],
            "extensions": {"EXT_meshopt_compression": {
                "buffer": 0,
                "byteOffset": pos["encOffset"],
                "byteLength": pos["encSize"],
                "byteStride": pos["stride"],
                "count": pos["count"],
                "mode": "ATTRIBUTES",
            }},
        }],
        "accessors": [{
            "bufferView": 0, "componentType": 5126,
            "type": "VEC3", "count": pos["count"],
        }],
    }

    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    open(os.path.join(outdir, "valid.glb"), "wb").write(build_glb(gltf, bytes(blob)))

    # Corrupt the per-block control bytes right after the stream header.
    # A bogus bit-width group makes the block decoder run out of bounds, which
    # is a deterministic structural failure (decode_error), unlike damaging
    # only the last-vertex tail, which the format permits to yield garbage.
    corrupt = bytearray(blob)
    for k in range(1, 4):
        corrupt[pos["encOffset"] + k] = 0xFF
    open(os.path.join(outdir, "corrupt.glb"), "wb").write(build_glb(gltf, bytes(corrupt)))
    print("wrote valid.glb and corrupt.glb to", outdir)


if __name__ == "__main__":
    main()
