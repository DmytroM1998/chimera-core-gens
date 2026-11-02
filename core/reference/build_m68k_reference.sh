#!/bin/sh
set -eu

fail() { printf '%s\n' "m68k reference: $*" >&2; exit 1; }
[ "$#" -eq 2 ] || fail "usage: build_m68k_reference.sh <Gens Git repository> <new output directory>"
[ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ] ||
    fail "this build requires Linux x86-64 with i386 multilib support"

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
reference=$1
output=$2
case "$output" in
    /*) ;;
    *) fail "output directory must be an absolute path" ;;
esac
[ -d "$(dirname "$output")" ] || fail "output parent does not exist"
[ ! -e "$output" ] && [ ! -L "$output" ] || fail "output already exists: $output"
python3 "$root/tools/verify_source.py" --reference "$reference"
mkdir -- "$output"
output=$(CDPATH= cd -- "$output" && pwd)
source="$root/extern/gens-rerecording/Gens-rr"

gcc -m32 -std=gnu99 -O2 -fno-pie -no-pie \
    "$source/Starscream/Main68k/Star.c" -o "$output/star-generator"
"$output/star-generator" "$output/main68k.asm" -quiet -hog -name main68k_
nasm -f elf32 "$output/main68k.asm" -o "$output/main68k.o"
nasm -f elf32 "$root/reference/m68k_bus_i386.asm" -o "$output/m68k_bus_i386.o"
gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -fno-pie -no-pie \
    -mstackrealign -fno-omit-frame-pointer -I"$source/src" \
    "$root/reference/m68k_reference.c" "$output/main68k.o" "$output/m68k_bus_i386.o" \
    -Wl,-z,noexecstack -o "$output/m68k-reference"

LC_ALL=C timeout 30s "$output/m68k-reference" > "$output/trace.first.part"
LC_ALL=C timeout 30s "$output/m68k-reference" > "$output/trace.second.part"
cmp "$output/trace.first.part" "$output/trace.second.part" ||
    fail "reference traces differ between clean processes"
python3 "$root/tools/check_m68k_trace.py" "$output/trace.first.part"
mv "$output/trace.first.part" "$output/trace.jsonl"
rm "$output/trace.second.part"
{
    printf 'gens=b948995d15034983cbf5562610586d93e0b25a9b\n'
    printf 'generator_options=-quiet -hog -name main68k_\n'
    printf 'compiler=%s\n' "$(gcc --version | head -n 1)"
    printf 'assembler=%s\n' "$(nasm -v)"
    (cd "$output" && sha256sum star-generator main68k.asm m68k-reference trace.jsonl)
} > "$output/provenance.txt"
printf '68000 reference output: %s\n' "$output"
