#!/usr/bin/env python3
"""Merge per-segment asciinema v2 casts into one continuous recording.

Each segment is shifted to start after the previous segment ends plus a
3-second idle gap, so section boundaries are visible as real pauses.
Header geometry is taken from the first segment.
"""
import json
import sys

GAP_SECONDS = 3.0


def load(path):
    rows = [json.loads(line) for line in open(path).read().splitlines()]
    return rows[0], rows[1:]


def main(out_path, inputs):
    header, _ = load(inputs[0])
    merged = []
    base = 0.0
    for index, path in enumerate(inputs):
        _, events = load(path)
        if index > 0:
            base += GAP_SECONDS
        for event in events:
            merged.append([base + float(event[0]), event[1], event[2]])
        base += float(events[-1][0])
    with open(out_path, "w") as f:
        f.write(json.dumps(header) + "\n")
        for event in merged:
            f.write(json.dumps(event) + "\n")
    print("wrote %s with %d events, %.1fs total" %
          (out_path, len(merged), base))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
