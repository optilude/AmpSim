#!/usr/bin/env python3
"""Build the QSPI capture blob and C++ index for AmpSim.

Only exact NAM A2 Lite/Nano captures are accepted for now:
WaveNet, C=3, 23 layers, 1871 weights.
"""

import argparse
import json
import math
import struct
import sys
import wave
import zlib
from pathlib import Path

MAX_CAPTURES = 128
QSPI_BASE = 0x90000000
APP_BASE_OFFSET = 0x00040000
APP_RESERVED_SIZE = 0x00080000
SETTINGS_OFFSET = APP_BASE_OFFSET + APP_RESERVED_SIZE
SETTINGS_SIZE = 0x00001000
CAPTURE_DATA_OFFSET = SETTINGS_OFFSET + SETTINGS_SIZE
QSPI_SIZE = 0x00800000

A2_KERNELS = [6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 15, 15, 6, 6, 6, 6, 6, 6, 6]
A2_DILATIONS = [1, 3, 7, 17, 41, 101, 239, 1, 3, 7, 17, 41, 101, 239, 1, 13, 1, 3, 7, 17, 41, 101, 239]
A2_WEIGHT_COUNT = 1871
IR_SAMPLE_RATE = 48000
IR_MAX_SAMPLES = 4096


def cpp_string(s):
    return json.dumps(s)


def cpp_float(v):
    s = f"{float(v):.9g}"
    if "." not in s and "e" not in s and "E" not in s:
        s += ".0"
    return s + "f"


def align(data, n=4):
    while len(data) % n:
        data.append(0)


def extract_model(data):
    def is_a2(model):
        if not isinstance(model, dict) or model.get("architecture") != "WaveNet":
            return False
        cfg = model.get("config", {})
        layers = cfg.get("layers", [])
        if len(layers) != 1:
            return False
        layer = layers[0]
        return (layer.get("channels") == 3
                and layer.get("kernel_sizes") == A2_KERNELS
                and layer.get("dilations") == A2_DILATIONS
                and len(model.get("weights", [])) == A2_WEIGHT_COUNT)

    if is_a2(data):
        return data
    if data.get("architecture") == "SlimmableContainer":
        for submodel in data.get("config", {}).get("submodels", []):
            model = submodel.get("model")
            if is_a2(model):
                return model
    return None


def find_nam_files(captures_dir):
    return sorted(Path(captures_dir).rglob("*.nam"))


def find_ir_files(irs_dir):
    path = Path(irs_dir)
    if not path.exists():
        return []
    return sorted(path.rglob("*.wav"))


def read_wav_mono(path):
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        width = wav.getsampwidth()
        rate = wav.getframerate()
        frames = wav.getnframes()
        if rate != IR_SAMPLE_RATE:
            raise ValueError(f"expected {IR_SAMPLE_RATE} Hz, got {rate} Hz")
        if channels < 1 or channels > 2:
            raise ValueError(f"expected mono/stereo WAV, got {channels} channels")
        raw = wav.readframes(frames)

    samples = []
    if width == 2:
        vals = struct.unpack("<" + "h" * (frames * channels), raw)
        samples = [vals[i * channels] / 32768.0 for i in range(frames)]
    elif width == 3:
        for i in range(frames):
            j = i * channels * 3
            b0, b1, b2 = raw[j], raw[j + 1], raw[j + 2]
            v = b0 | (b1 << 8) | (b2 << 16)
            if v & 0x800000:
                v |= 0xFF000000
            v = struct.unpack("<i", struct.pack("<I", v & 0xFFFFFFFF))[0]
            samples.append(v / 8388608.0)
    elif width == 4:
        vals = struct.unpack("<" + "i" * (frames * channels), raw)
        samples = [vals[i * channels] / 2147483648.0 for i in range(frames)]
    else:
        raise ValueError(f"unsupported bit depth: {width * 8}")

    if len(samples) > IR_MAX_SAMPLES:
        samples = samples[:IR_MAX_SAMPLES]
    elif len(samples) < IR_MAX_SAMPLES:
        samples.extend([0.0] * (IR_MAX_SAMPLES - len(samples)))
    return normalize_ir(samples)


def normalize_ir(samples):
    energy = sum(x * x for x in samples)
    if energy <= 1e-12:
        return samples
    gain = 1.0 / math.sqrt(energy)
    return [x * gain for x in samples]


def build(args):
    nam_files = find_nam_files(args.captures)
    ir_files = find_ir_files(args.irs)
    if not nam_files and not ir_files:
        raise SystemExit(f"No .nam files found in {args.captures} and no .wav IRs found in {args.irs}")
    if len(nam_files) + len(ir_files) > MAX_CAPTURES:
        raise SystemExit(f"Too many captures: {len(nam_files) + len(ir_files)} > {MAX_CAPTURES}")

    entries = []
    blob = bytearray()
    captures_root = Path(args.captures)

    for path in nam_files:
        with path.open("r") as f:
            data = json.load(f)
        model = extract_model(data)
        if model is None:
            raise SystemExit(f"Unsupported NAM (not exact A2 Lite): {path}")

        rel = path.relative_to(captures_root)
        model_name = rel.parent.name if rel.parent.name != "." else "Default"
        variant_name = path.stem
        weights = [float(x) for x in model["weights"]]
        loudness = float(model.get("metadata", {}).get("loudness", 0.0))
        has_loudness = "loudness" in model.get("metadata", {})

        align(blob, 4)
        offset = len(blob)
        raw = struct.pack("<" + "f" * len(weights), *weights)
        blob.extend(raw)
        crc = zlib.crc32(raw) & 0xFFFFFFFF
        entries.append({
            "type": "NamA2Lite",
            "model": model_name,
            "variant": variant_name,
            "offset": offset,
            "bytes": len(raw),
            "count": len(weights),
            "loudness": loudness,
            "has_loudness": has_loudness,
            "crc": crc,
        })

    irs_root = Path(args.irs)
    for path in ir_files:
        rel = path.relative_to(irs_root)
        model_name = rel.parent.name if rel.parent.name != "." else "IR"
        variant_name = path.stem
        try:
            samples = read_wav_mono(path)
        except Exception as exc:
            raise SystemExit(f"Unsupported IR WAV {path}: {exc}") from exc

        align(blob, 4)
        offset = len(blob)
        raw = struct.pack("<" + "f" * len(samples), *samples)
        blob.extend(raw)
        crc = zlib.crc32(raw) & 0xFFFFFFFF
        entries.append({
            "type": "CabinetIr",
            "model": model_name,
            "variant": variant_name,
            "offset": offset,
            "bytes": len(raw),
            "count": len(samples),
            "loudness": 0.0,
            "has_loudness": False,
            "crc": crc,
        })

    capture_end = CAPTURE_DATA_OFFSET + len(blob)
    if capture_end > QSPI_SIZE:
        raise SystemExit(f"Capture blob exceeds QSPI: end=0x{capture_end:x} > 0x{QSPI_SIZE:x}")

    Path(args.out_bin).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out_header).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out_map).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out_bin).write_bytes(blob)

    with Path(args.out_header).open("w") as h:
        h.write("// Auto-generated by tools/build_capture_blob.py\n#pragma once\n\n")
        h.write("#include <cstddef>\n#include <cstdint>\n\n")
        h.write("enum class CaptureType : uint8_t { NamA2Lite = 0, CabinetIr = 1 };\n\n")
        h.write("struct CaptureEntry {\n")
        h.write("    CaptureType type;\n    const char* model_name;\n    const char* variant_name;\n")
        h.write("    uintptr_t qspi_address;\n    uint32_t byte_count;\n    uint32_t item_count;\n")
        h.write("    float loudness_db;\n    uint8_t has_loudness;\n    uint32_t crc32;\n};\n\n")
        h.write(f"static constexpr int MAX_CAPTURE_COUNT = {MAX_CAPTURES};\n")
        h.write(f"static constexpr int CAPTURE_COUNT = {len(entries)};\n")
        h.write(f"static constexpr uintptr_t CAPTURE_DATA_QSPI_BASE = 0x{QSPI_BASE + CAPTURE_DATA_OFFSET:08x};\n")
        h.write(f"static constexpr uint32_t SETTINGS_QSPI_OFFSET = 0x{SETTINGS_OFFSET:08x};\n\n")
        h.write("static const CaptureEntry capture_entries[CAPTURE_COUNT] = {\n")
        for e in entries:
            h.write(f"    {{ CaptureType::{e['type']}, ")
            h.write(f"{cpp_string(e['model'])}, {cpp_string(e['variant'])}, ")
            h.write(f"CAPTURE_DATA_QSPI_BASE + 0x{e['offset']:x}, {e['bytes']}, {e['count']}, ")
            h.write(f"{cpp_float(e['loudness'])}, {1 if e['has_loudness'] else 0}, 0x{e['crc']:08x} }},\n")
        h.write("};\n")

    with Path(args.out_map).open("w") as m:
        m.write(f"App window:       0x{QSPI_BASE + APP_BASE_OFFSET:08x}..0x{QSPI_BASE + SETTINGS_OFFSET - 1:08x}\n")
        m.write(f"Settings sector:  0x{QSPI_BASE + SETTINGS_OFFSET:08x}..0x{QSPI_BASE + SETTINGS_OFFSET + SETTINGS_SIZE - 1:08x}\n")
        m.write(f"Capture blob:     0x{QSPI_BASE + CAPTURE_DATA_OFFSET:08x}..0x{QSPI_BASE + capture_end - 1:08x}\n")
        m.write(f"Capture count:    {len(entries)} / {MAX_CAPTURES}\n")
        m.write(f"Capture bytes:    {len(blob)}\n\n")
        for i, e in enumerate(entries):
            m.write(f"[{i:03d}] {e['type']:9s} {e['model']} / {e['variant']} {e['bytes']} bytes crc=0x{e['crc']:08x}\n")

    print(f"Generated {len(entries)} captures, {len(blob)} bytes")
    print(Path(args.out_map).read_text())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("captures")
    p.add_argument("--irs", default="IRs")
    p.add_argument("--out-bin", default="build/capture_data.bin")
    p.add_argument("--out-header", default="src/capture_index.h")
    p.add_argument("--out-map", default="build/capture_data.map")
    build(p.parse_args())


if __name__ == "__main__":
    main()
