#!/usr/bin/env python3
"""Generate self-contained glTF 2.0 assets exercising KHR_mesh_quantization.

Each asset embeds its binary payload as a data URI so the test executables need
no external files. The generated integer codes are exact; expected dequantized
values are computed from the spec formulas and written alongside the JSON.
"""
import base64
import json
import os
import struct
import sys

OUT_DIR = sys.argv[1]

# (file suffix, ctype code, struct format, signed, count_of_codes)
INTEGER_TYPES = [
    ("i8", 5120, "b", True),
    ("u8", 5121, "B", False),
    ("i16", 5122, "h", True),
    ("u16", 5123, "H", False),
]

# Exact representable codes: for i8/i16 use fractions of the max magnitude.
def codes_for(signed, bits):
    if signed:
        lo, hi = -(2 ** (bits - 1)), 2 ** (bits - 1) - 1
        # pick codes whose dequantized values are easy to check exactly
        return [lo, -1, 0, 1, hi // 2, hi]
    else:
        hi = 2 ** bits - 1
        return [0, 1, hi // 4, hi // 2, 3 * hi // 4, hi]


def dequant(code, signed, bits):
    if signed:
        return float(code) / float(2 ** (bits - 1) - 1)
    return float(code) / float(2 ** bits - 1)


def build_integer_asset(suffix, ctype, fmt, signed):
    bits = 8 if fmt in ("b", "B") else 16
    x_codes = codes_for(signed, bits)
    y_codes = codes_for(signed, bits)[:len(x_codes)]
    z_codes = codes_for(signed, bits)[:len(x_codes)]
    n = len(x_codes)

    # interleaved VEC3 stride = 3 * component size
    blob = b"".join(struct.pack("<" + fmt * 3, x_codes[i], y_codes[i], z_codes[i])
                    for i in range(n))
    uri = "data:application/octet-stream;base64," + base64.b64encode(blob).decode()

    expected = [[dequant(x_codes[i], signed, bits),
                 dequant(y_codes[i], signed, bits),
                 dequant(z_codes[i], signed, bits)] for i in range(n)]

    gltf = {
        "asset": {"version": "2.0", "generator": "quantization test"},
        "extensionsUsed": ["KHR_mesh_quantization"],
        "extensionsRequired": ["KHR_mesh_quantization"],
        "buffers": [{"uri": uri, "byteLength": len(blob)}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(blob),
                         "byteStride": 3 * (bits // 8)}],
        "accessors": [{
            "bufferView": 0, "componentType": ctype, "count": n,
            "type": "VEC3",
            # min/max intentionally omitted: their values live in dequantized
            # space, while cgltf_accessor_read_uint returns raw storage codes.
            "extensions": {
                "KHR_mesh_quantization": {"quantizedBits": ctype}
            }
        }],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
        "nodes": [{"mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
    }
    path = os.path.join(OUT_DIR, "quant_" + suffix + ".gltf")
    with open(path, "w") as f:
        json.dump(gltf, f)
    return path, expected, n


def build_float_storage_asset():
    """FLOAT accessor whose quantizedBits is SHORT: storage stays float."""
    n = 4
    vals = [-1.0, -0.25, 0.5, 1.0]
    blob = struct.pack("<" + "f" * n, *vals)
    uri = "data:application/octet-stream;base64," + base64.b64encode(blob).decode()
    gltf = {
        "asset": {"version": "2.0"},
        "extensionsUsed": ["KHR_mesh_quantization"],
        "buffers": [{"uri": uri, "byteLength": len(blob)}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(blob)}],
        "accessors": [{
            "bufferView": 0, "componentType": 5126, "count": n, "type": "SCALAR",
            "min": [min(vals)], "max": [max(vals)],
            "extensions": {
                "KHR_mesh_quantization": {"quantizedBits": 5122}
            }
        }],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
        "nodes": [{"mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
    }
    path = os.path.join(OUT_DIR, "quant_float_storage.gltf")
    with open(path, "w") as f:
        json.dump(gltf, f)
    return path, [[v] for v in vals], n


def build_invalid_asset(name, mutate):
    """A valid i8 quantized asset, then mutated into an invalid one."""
    template_path, _, _ = build_integer_asset("i8", 5120, "b", True)
    with open(template_path) as f:
        gltf = json.load(f)
    mutate(gltf)
    bad_path = os.path.join(OUT_DIR, name)
    with open(bad_path, "w") as f:
        json.dump(gltf, f)


def build_srgb_color_asset():
    """u8 VEC4 COLOR_0 with colorSpace=srgb: legal only on color attributes."""
    blob = struct.pack("<BBBB", 255, 0, 128, 255)
    uri = "data:application/octet-stream;base64," + base64.b64encode(blob).decode()
    gltf = {
        "asset": {"version": "2.0"},
        "extensionsUsed": ["KHR_mesh_quantization"],
        "buffers": [{"uri": uri, "byteLength": len(blob)}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(blob)}],
        "accessors": [{
            "bufferView": 0, "componentType": 5121, "count": 1, "type": "VEC4",
            "extensions": {
                "KHR_mesh_quantization": {"quantizedBits": 5121, "colorSpace": "srgb"}
            }
        }],
        "meshes": [{"primitives": [{"attributes": {"COLOR_0": 0}}]}],
        "nodes": [{"mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
    }
    path = os.path.join(OUT_DIR, "quant_srgb_color.gltf")
    with open(path, "w") as f:
        json.dump(gltf, f)
    return path, [[dequant(c, False, 8) for c in (255, 0, 128, 255)]], 1


def build_plain_asset():
    """Plain FLOAT accessor with no extension: behavior must be unchanged."""
    vals = [0.25, 0.5, 0.75]
    blob = struct.pack("<" + "f" * 3, *vals)
    uri = "data:application/octet-stream;base64," + base64.b64encode(blob).decode()
    gltf = {
        "asset": {"version": "2.0"},
        "buffers": [{"uri": uri, "byteLength": len(blob)}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(blob)}],
        "accessors": [{
            "bufferView": 0, "componentType": 5126, "count": 1, "type": "VEC3",
            "min": vals, "max": vals
        }],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
        "nodes": [{"mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
    }
    path = os.path.join(OUT_DIR, "plain.gltf")
    with open(path, "w") as f:
        json.dump(gltf, f)
    return path


def write_glb(path, gltf, blob):
    """Serialize an embedded glTF dict (data URI stripped) into a GLB file."""
    gltf = json.loads(json.dumps(gltf))
    gltf["buffers"][0].pop("uri", None)
    gltf["buffers"][0]["byteLength"] = len(blob)
    json_bytes = json.dumps(gltf, separators=(",", ":")).encode()
    json_pad = (4 - len(json_bytes) % 4) % 4
    json_bytes += b" " * json_pad
    bin_pad = (4 - len(blob) % 4) % 4
    bin_bytes = blob + b"\x00" * bin_pad
    total = 12 + 8 + len(json_bytes) + 8 + len(bin_bytes)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(json_bytes), 0x4E4F534A))
        f.write(json_bytes)
        f.write(struct.pack("<II", len(bin_bytes), 0x004E4942))
        f.write(bin_bytes)


def build_glb_asset():
    """Same i8 quantized payload, but carried in a GLB binary chunk."""
    path, expected, n = build_integer_asset("i8", 5120, "b", True)
    with open(path) as f:
        gltf = json.load(f)
    b64 = gltf["buffers"][0]["uri"].split(",", 1)[1]
    blob = base64.b64decode(b64)
    glb_path = os.path.join(OUT_DIR, "quant_i8.glb")
    write_glb(glb_path, gltf, blob)
    return glb_path, expected, n


def main():
    build_plain_asset()
    manifest = {}
    for suffix, ctype, fmt, signed in INTEGER_TYPES:
        path, expected, n = build_integer_asset(suffix, ctype, fmt, signed)
        manifest[os.path.basename(path)] = {"expected": expected, "count": n}
    path, expected, n = build_float_storage_asset()
    manifest[os.path.basename(path)] = {"expected": expected, "count": n}
    glb_path, glb_expected, glb_n = build_glb_asset()
    manifest[os.path.basename(glb_path)] = {"expected": glb_expected, "count": glb_n}
    srgb_path, srgb_expected, srgb_n = build_srgb_color_asset()
    manifest[os.path.basename(srgb_path)] = {"expected": srgb_expected, "count": srgb_n}

    # Invalid: quantizedBits width mismatches accessor componentType width.
    build_invalid_asset("invalid_width_mismatch.gltf",
                        lambda g: g["accessors"][0]["extensions"]
                        ["KHR_mesh_quantization"].__setitem__("quantizedBits", 5123))
    # Invalid: unsupported quantizedBits (UNSIGNED_INT).
    build_invalid_asset("invalid_unsigned_int.gltf",
                        lambda g: g["accessors"][0]["extensions"]
                        ["KHR_mesh_quantization"].__setitem__("quantizedBits", 5125))
    # Invalid: extension used on accessor but never declared in extensionsUsed.
    build_invalid_asset("invalid_not_declared.gltf",
                        lambda g: g.pop("extensionsUsed"))
    # Invalid: matrix accessor quantized.
    def to_matrix(g):
        g["accessors"][0]["type"] = "MAT4"
    build_invalid_asset("invalid_matrix.gltf", to_matrix)
    # Invalid: srgb colorSpace on a non-COLOR attribute.
    def srgb_on_position(g):
        g["accessors"][0]["extensions"]["KHR_mesh_quantization"]["colorSpace"] = "srgb"
    build_invalid_asset("invalid_srgb.gltf", srgb_on_position)

    # Invalid: FLOAT storage paired with unsigned quantizedBits.
    def float_unsigned(g):
        g["accessors"][0]["componentType"] = 5126
        g["accessors"][0]["extensions"]["KHR_mesh_quantization"]["quantizedBits"] = 5121
    build_invalid_asset("invalid_float_unsigned.gltf", float_unsigned)

    with open(os.path.join(OUT_DIR, "expected.json"), "w") as f:
        json.dump(manifest, f, indent=1)

    # Flat, parser-free companion consumed by test_quantization.cpp:
    # one block per asset; first line "<file> <count> <components>",
    # followed by <count> rows of expected dequantized values.
    with open(os.path.join(OUT_DIR, "expected_flat.txt"), "w") as f:
        for name, info in manifest.items():
            comps = len(info["expected"][0])
            f.write("%s %d %d\n" % (name, info["count"], comps))
            for row in info["expected"]:
                f.write(" ".join(repr(v) for v in row) + "\n")


if __name__ == "__main__":
    main()
