#!/usr/bin/env python3
"""Corruption / error-case tests for meshopt support in cgltf.

Each case mutates a copy of the generated data (never the originals) and runs
the cgltf harness, asserting the numeric cgltf_result is distinguishable and
that no uncompressed/silent-wrong output is produced.

Result code numbers come from cgltf_result:
  0 success, 1 data_too_short, 2 unknown_format, 3 invalid_json, 4 invalid_gltf,
  5 invalid_options, 6 file_not_found, 7 io_error, 8 out_of_memory,
  9 legacy_gltf, 10 unsupported_extension, 11 decode_error
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "..", "data_scalar")
DUMP = os.path.join(HERE, "..", "build", "dump")

RESULT = {
    0: "success", 1: "data_too_short", 2: "unknown_format", 3: "invalid_json",
    4: "invalid_gltf", 5: "invalid_options", 6: "file_not_found", 7: "io_error",
    8: "out_of_memory", 9: "legacy_gltf", 10: "unsupported_extension",
    11: "decode_error",
}


def run(gltf_path):
    proc = subprocess.run([DUMP, gltf_path], capture_output=True, text=True)
    line = next((l for l in proc.stdout.splitlines() if l.startswith("Result:")), "")
    code = int(line.split(":")[1])
    return code, proc.returncode


def make_case(src_dir, dst_dir, mutator):
    os.makedirs(dst_dir, exist_ok=True)
    for fn in os.listdir(src_dir):
        shutil.copy2(os.path.join(src_dir, fn), os.path.join(dst_dir, fn))
    mutator(dst_dir)


def truncate_blob(d):
    p = os.path.join(d, "compressed.bin")
    b = bytearray(open(p, "rb").read())
    del b[len(b) // 2:]  # chop the second half: every later view is truncated
    open(p, "wb").write(b)


def flip_header(d):
    p = os.path.join(d, "compressed.bin")
    b = bytearray(open(p, "rb").read())
    b[0] = 0x00  # destroy vertex stream magic 0xa0
    open(p, "wb").write(b)


def flip_index_header(d):
    m = json.load(open(os.path.join(d, "manifest.json")))
    off = next(v["encOffset"] for v in m["views"] if v["name"] == "triangles")
    p = os.path.join(d, "compressed.bin")
    b = bytearray(open(p, "rb").read())
    b[off] = 0x00  # destroy triangle stream magic 0xe0
    open(p, "wb").write(b)


def corrupt_midstream(d):
    # The reference codec is documented to tolerate some interior corruption
    # (it may emit garbage rather than fail), so hit the structural tail of
    # every stream: the vertex last-vertex/channel tail and the index codeaux
    # table, whose exact consumption is validated at decode time.
    m = json.load(open(os.path.join(d, "manifest.json")))
    p = os.path.join(d, "compressed.bin")
    b = bytearray(open(p, "rb").read())
    for v in m["views"]:
        end = v["encOffset"] + v["encSize"]
        for k in range(1, 9):
            b[end - k] ^= 0xFF
    open(p, "wb").write(b)


def unknown_required(d):
    p = os.path.join(d, "compressed.gltf")
    s = open(p).read().replace(
        'EXT_meshopt_compression', 'KHR_meshopt_compression_future')
    open(p, "w").write(s)


def khr_variant(d):
    p = os.path.join(d, "compressed.gltf")
    s = open(p).read().replace(
        'EXT_meshopt_compression', 'KHR_meshopt_compression')
    open(p, "w").write(s)


def main():
    tmp = tempfile.mkdtemp(prefix="meshopt_corrupt_")
    cases = [
        # truncating the on-disk bin trips the loader's size check (io_error=7);
        # a view that survives the load but has a short stream yields decode_error=11,
        # and gltf-level size mismatch can surface as invalid_gltf=4.
        ("truncated_blob", truncate_blob, {4, 7, 11}),
        ("bad_vertex_magic", flip_header, {11}),
        ("bad_index_magic", flip_index_header, {11}),
        ("scrambled_stream_tails", corrupt_midstream, {4, 11}),
        ("unknown_required_extension", unknown_required, {10}),
        ("khr_extension_variant", khr_variant, {0}),
    ]
    failures = 0
    for name, mutator, expected in cases:
        cdir = os.path.join(tmp, name)
        make_case(DATA, cdir, mutator)
        code, rc = run(os.path.join(cdir, "compressed.gltf"))
        ok = code in expected
        failures += 0 if ok else 1
        print(f"[{'PASS' if ok else 'FAIL'}] {name:28s} result={code} "
              f"({RESULT.get(code, '?')}) expected={sorted(expected)}")

    # sanity: the untouched reference and compressed files must still succeed
    for fn in ("reference.gltf", "compressed.gltf"):
        code, _ = run(os.path.join(DATA, fn))
        ok = code == 0
        failures += 0 if ok else 1
        print(f"[{'PASS' if ok else 'FAIL'}] clean_{fn:20s} result={code}")

    # GLB cases: embedded BIN bypasses the file-size check, so a damaged
    # stream reaches the codec and must surface as decode_error, never silent.
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "make_glb", os.path.join(HERE, "make_glb.py"))
    make_glb = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(make_glb)
    glb_dir = os.path.join(tmp, "glb")
    sys.argv = ["make_glb.py", glb_dir]
    make_glb.main()
    for fn, expected in (("valid.glb", 0), ("corrupt.glb", 11)):
        code, _ = run(os.path.join(glb_dir, fn))
        ok = code == expected
        failures += 0 if ok else 1
        print(f"[{'PASS' if ok else 'FAIL'}] glb_{fn:16s} result={code} "
              f"({RESULT.get(code, '?')}) expected={expected}")

    shutil.rmtree(tmp, ignore_errors=True)
    print("ALL ERROR CASES PASSED" if failures == 0 else f"{failures} FAILURE(S)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
