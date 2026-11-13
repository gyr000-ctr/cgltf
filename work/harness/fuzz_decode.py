#!/usr/bin/env python3
"""Mutation fuzz for the meshopt decode path.

Takes the valid compressed GLB, applies random byte mutations (including the
JSON and the BIN), and runs the ASan/UBSan harness. A healthy library must
either succeed or return a normal cgltf_result; an ASan/UBSan fault or a signal
shows up as an exit status >= 128 and fails the run.
"""
import os
import random
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DUMP = os.path.join(HERE, "..", "build", "dump")
SEED_GLB = os.path.join(HERE, "..", "data_scalar", "..", "glb_seed.glb")


def main():
    iters = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    seed_path = sys.argv[2] if len(sys.argv) > 2 else SEED_GLB
    seed = bytearray(open(seed_path, "rb").read())
    rng = random.Random(0xC6C6)
    tmp = tempfile.mkdtemp(prefix="meshopt_fuzz_")
    crashes = 0
    codes = {}
    for i in range(iters):
        b = bytearray(seed)
        for _ in range(rng.randint(1, 6)):
            kind = rng.choice(("flip", "zero", "set", "trunc", "dup"))
            if kind in ("flip", "zero", "set") and len(b) > 12:
                pos = rng.randrange(12, len(b))
                b[pos] = rng.choice((b[pos] ^ rng.randrange(1, 256), 0, 0xFF))
            elif kind == "trunc" and len(b) > 40:
                del b[rng.randrange(20, len(b)):]
            elif kind == "dup" and len(b) > 40:
                pos = rng.randrange(12, len(b))
                b[pos:pos] = bytes([b[pos]])
        p = os.path.join(tmp, "mut.glb")
        open(p, "wb").write(b)
        proc = subprocess.run([DUMP, p], capture_output=True)
        rc = proc.returncode
        if rc >= 128 or rc < 0:
            crashes += 1
            keep = os.path.join(HERE, "..", "crash_%d.glb" % i)
            open(keep, "wb").write(b)
            print("CRASH at iter", i, "signal/rc", rc, "saved", keep)
            if crashes >= 5:
                break
        else:
            codes[rc] = codes.get(rc, 0) + 1
    print("iters=%d crashes=%d result_hist=%s" % (iters, crashes, dict(sorted(codes.items()))))
    return 1 if crashes else 0


if __name__ == "__main__":
    sys.exit(main())
