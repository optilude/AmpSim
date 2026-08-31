#!/bin/sh
# Instruction mix of the NAM A2 hot path as actually compiled for the pedal.
#
# The host benchmark (tools/nam_bench.cpp) can prove an optimisation is
# bit-exact, but it cannot tell you whether it is faster on the target: an
# out-of-order arm64 laptop with megabytes of cache hides precisely the load
# stalls that bottleneck an in-order Cortex-M7. This counts the instructions
# GCC emits for the M7 instead, which is the thing being optimised.
#
# Usage: tools/nam_codegen.sh [output.txt]
# Pair with `git stash` to compare a working tree against HEAD.
set -e

TC=/Applications/ArmGNUToolchain/15.3.rel1/arm-none-eabi/bin
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/tu.cpp" <<'EOF'
#include "nam_a2_runtime.h"
static nam_a2::SharedWeights g_w;
static nam_a2::HotState g_h;
static nam_a2::State g_st;
void bench(const float* in, float* out) {
    nam_a2::process_block_48(g_st, g_h, g_w, in, out);
}
EOF

"$TC/arm-none-eabi-g++" -std=gnu++17 -Ofast \
    -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard \
    -c -I"$ROOT/src" "$TMP/tu.cpp" -o "$TMP/tu.o"
"$TC/arm-none-eabi-objdump" -d "$TMP/tu.o" > "$TMP/dis.txt"

count() { grep -cE "$1" "$TMP/dis.txt" || true; }

{
    printf 'text bytes  : %s\n' "$("$TC/arm-none-eabi-size" "$TMP/tu.o" | tail -1 | awk '{print $1}')"
    printf 'instructions: %s\n' "$(count '^[[:space:]]+[0-9a-f]+:')"
    printf 'vldr (loads): %s\n' "$(count 'vldr')"
    printf 'vstr        : %s\n' "$(count 'vstr')"
    printf 'vfma/vmla   : %s\n' "$(count 'vfma|vmla')"
    printf 'vmul        : %s\n' "$(count 'vmul')"
    printf 'vadd/vsub   : %s\n' "$(count 'vadd|vsub')"
} > "${1:-/dev/stdout}"

# Keep the disassembly around: the counts say whether the balance moved, but
# only the listing says whether the accumulators stayed in registers.
cp "$TMP/dis.txt" "$ROOT/build/nam_hot.dis" 2>/dev/null || true
