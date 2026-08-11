#!/usr/bin/env python3
"""Create BOOT_SRAM app + capture data combined QSPI image."""

import argparse
from pathlib import Path

APP_RESERVED_SIZE = 512 * 1024


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--app", required=True)
    p.add_argument("--captures", required=True)
    p.add_argument("--out", required=True)
    args = p.parse_args()

    app = Path(args.app).read_bytes()
    captures = Path(args.captures).read_bytes()
    if len(app) > APP_RESERVED_SIZE:
        raise SystemExit(f"app too large: {len(app)} > {APP_RESERVED_SIZE}")

    out = bytearray(app)
    out.extend(b"\0" * (APP_RESERVED_SIZE - len(out)))
    out.extend(captures)
    Path(args.out).write_bytes(out)
    print(f"Combined image: app={len(app)} bytes, pad={APP_RESERVED_SIZE - len(app)} bytes, captures={len(captures)} bytes, total={len(out)} bytes")


if __name__ == "__main__":
    main()
