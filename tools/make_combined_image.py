#!/usr/bin/env python3
"""Create BOOT_SRAM app + capture data combined QSPI image.

The QSPI layout is owned by build_capture_blob.py; import it rather than
restating the offsets here. Getting these out of step silently shifts every
model and IR the firmware reads.
"""

import argparse
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    "build_capture_blob", Path(__file__).with_name("build_capture_blob.py"))
_layout = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_layout)

APP_BASE_OFFSET = _layout.APP_BASE_OFFSET
APP_RESERVED_SIZE = _layout.APP_RESERVED_SIZE
SETTINGS_OFFSET = _layout.SETTINGS_OFFSET
SETTINGS_SIZE = _layout.SETTINGS_SIZE
CAPTURE_DATA_OFFSET = _layout.CAPTURE_DATA_OFFSET

# Offsets within the combined image, which is flashed at APP_BASE_OFFSET.
SETTINGS_IMAGE_OFFSET = SETTINGS_OFFSET - APP_BASE_OFFSET
CAPTURE_IMAGE_OFFSET = CAPTURE_DATA_OFFSET - APP_BASE_OFFSET


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
    out.extend(b"\0" * (SETTINGS_IMAGE_OFFSET - len(out)))
    # Leave the settings sector looking erased so PersistentStorage restores
    # defaults on a fresh flash instead of reading whatever was there before.
    out.extend(b"\xff" * SETTINGS_SIZE)
    assert len(out) == CAPTURE_IMAGE_OFFSET, (len(out), CAPTURE_IMAGE_OFFSET)
    out.extend(captures)
    Path(args.out).write_bytes(out)
    print(f"Combined image: app={len(app)} bytes at 0x{APP_BASE_OFFSET:06x}, "
          f"settings 0x{SETTINGS_SIZE:x} bytes at 0x{SETTINGS_OFFSET:06x}, "
          f"captures={len(captures)} bytes at 0x{CAPTURE_DATA_OFFSET:06x}, "
          f"total={len(out)} bytes")


if __name__ == "__main__":
    main()
