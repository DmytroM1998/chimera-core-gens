#!/bin/sh
set -eu

work=/work
out=/out
gens_revision=b948995d15034983cbf5562610586d93e0b25a9b
gens=/sources/repository/core/extern/gens-rerecording
export GENS_REFERENCE="$gens"

fail() { printf '%s\n' "error: $*" >&2; exit 1; }

mkdir -p "$HOME"
exec 9>"$work/.lock"
flock -n 9 || fail "another build or preparation is using the work volume"

git config --global core.autocrlf false
git config --global core.eol lf
for repo in /sources/minibox "$gens"; do
    git config --global --get-all safe.directory | grep -Fxq "$repo" ||
        git config --global --add safe.directory "$repo"
done

ensure_clean() {
    repo=$1
    status=$(git -C "$repo" status --porcelain=v1 --untracked-files=all)
    [ -z "$status" ] || fail "$repo has local changes. Commit or preserve them before preparation."
}

check_core_links() {
    find "$work/core" -path "$work/core/build" -prune -o \
        -type l -print > "$work/core-links.txt"
    [ ! -s "$work/core-links.txt" ] || fail "prepared core contains symbolic links"
    rm "$work/core-links.txt"
}

prepare() {
    [ ! -e "$work/prepared" ] || fail "work volume already has a prepared source snapshot"
    for path in minibox core; do
        [ ! -e "$work/$path" ] || fail "$work/$path already exists. Use a fresh work volume."
    done
    ensure_clean /sources/minibox
    ensure_clean "$gens"
    minibox_rev=$(git -C /sources/minibox rev-parse HEAD)
    [ "$(git -C "$gens" rev-parse HEAD)" = "$gens_revision" ] ||
        fail "Gens checkout is not at $gens_revision"
    git clone --no-hardlinks --no-checkout /sources/minibox "$work/minibox"
    git -C "$work/minibox" checkout --detach "$minibox_rev"
    git -C "$work/minibox" submodule update --init --recursive
    git -C "$work/minibox" submodule status --recursive > "$work/submodules.txt"
    if grep -Eq '^[+-U]' "$work/submodules.txt"; then
        fail "a miniBox submodule is uninitialized, changed, or conflicted"
    fi
    mkdir "$work/core"
    tar -C /sources/core --exclude=./build --exclude=./.git --exclude=./extern/gens-rerecording/.git \
        --exclude='*/__pycache__' --exclude='*.pyc' -cf "$work/core-snapshot.tar" .
    tar -C "$work/core" -xf "$work/core-snapshot.tar"
    rm "$work/core-snapshot.tar"
    check_core_links
    python3 "$work/core/tools/verify_source.py" --reference "$gens"

    [ "$(git -C /sources/minibox rev-parse HEAD)" = "$minibox_rev" ] ||
        fail "miniBox HEAD changed during preparation"
    [ "$(git -C "$gens" rev-parse HEAD)" = "$gens_revision" ] ||
        fail "Gens HEAD changed during preparation"
    ensure_clean /sources/minibox
    ensure_clean "$gens"

    mkdir -p "$work/downloads"
    version=$(gcc -dumpfullversion)
    case "$version" in
        13.3.0)
            expected=ed5f2f4c6ed2c796fcf2c93707159e9dbd3ddb1ba063d549804dd68cdabbb6d550985ae1c8465ae9a336cfe29274a6eb0f42e21924360574ebd8e5d5c7c9a801
            ;;
        13.4.0)
            expected=9b4b83ecf51ef355b868608b8d257b2fa435c06d2719cb86657a7c2c2a0828ff4ce04e9bac1055bbcad8ed5b4da524cafaef654785e23a50233d95d89201e35f
            ;;
        *) fail "GCC $version has no pinned source checksum. Update the builder before continuing." ;;
    esac
    archive="$work/downloads/gcc-$version.tar.xz"
    curl -fsSL --retry 3 -o "$archive.part" \
        "https://gcc.gnu.org/pub/gcc/releases/gcc-$version/gcc-$version.tar.xz"
    printf '%s  %s\n' "$expected" "$archive.part" | sha512sum -c -
    mv "$archive.part" "$archive"
    python3 "$work/core/tools/fetch_test_roms.py" "$work/core/tests/headless-roms.txt" \
        "$work/test-roms"
    find "$work/core" -path "$work/core/build" -prune -o \
        -type f -print0 > "$work/core-list.unsorted"
    sort -z "$work/core-list.unsorted" > "$work/core-files.list"
    xargs -0 sha256sum < "$work/core-files.list" > "$work/core-sources.sha256"
    rm "$work/core-list.unsorted"
    {
        printf 'minibox=%s\ngens=%s\ngcc=%s\n' \
            "$minibox_rev" "$gens_revision" "$version"
        printf 'gcc_sha512=%s\n' "$expected"
        printf 'meson=%s\nninja=%s\ncmake=%s\nnasm=%s\n' \
            "$(meson --version)" "$(ninja --version)" \
            "$(cmake --version | head -n 1)" "$(nasm -v)"
        cat "$work/submodules.txt"
    } > "$work/prepared"
    echo "Source preparation complete. The prepared snapshot is $work/prepared."
}

check_prepared() {
    [ -f "$work/prepared" ] || fail "run the networked prepare service first"
    [ "$(git -C "$work/minibox" rev-parse HEAD)" = \
        "$(sed -n 's/^minibox=//p' "$work/prepared")" ] || fail "prepared miniBox revision changed"
    [ "$(gcc -dumpfullversion)" = "$(sed -n 's/^gcc=//p' "$work/prepared")" ] ||
        fail "compiler version changed after preparation"
    ensure_clean "$work/minibox"
    git -C "$work/minibox" submodule status --recursive > "$work/current-submodules.txt"
    cmp "$work/submodules.txt" "$work/current-submodules.txt" ||
        fail "prepared miniBox submodule revisions changed"
    rm "$work/current-submodules.txt"
    check_core_links
    find "$work/core" -path "$work/core/build" -prune -o \
        -type f -print0 > "$work/core-current.unsorted"
    sort -z "$work/core-current.unsorted" > "$work/core-current.list"
    cmp "$work/core-files.list" "$work/core-current.list" ||
        fail "prepared core source file set changed"
    rm "$work/core-current.unsorted" "$work/core-current.list"
    sha256sum -c "$work/core-sources.sha256" >/dev/null
    python3 "$work/core/tools/verify_source.py" --reference "$gens"
}

build_minibox() {
    build="$work/minibox-build"
    if [ ! -f "$build/build.ninja" ]; then
        meson setup "$build" "$work/minibox" -Dguest_cpp=true
    fi
    # Bound musl's nested make, which otherwise ignores Ninja's job limit.
    python3 - "$build/build.ninja" <<'PYJOBS'
from pathlib import Path
import sys

path = Path(sys.argv[1])
recipe = path.read_text()
unbounded = "/usr/bin/make -j install"
bounded = "/usr/bin/make -j1 install"
if unbounded not in recipe and bounded not in recipe:
    sys.exit("error: miniBox musl make command changed. Cannot safely limit its parallelism.")
path.write_text(recipe.replace(unbounded, bounded))
PYJOBS
    printf '%s\n' "Building miniBox with one Ninja job and one musl make job."
    version=$(gcc -dumpfullversion)
    mkdir -p "$build/cpp-toolchain"
    cp "$work/downloads/gcc-$version.tar.xz" "$build/cpp-toolchain/gcc-$version.tar.xz"
    expected=$(sed -n 's/^gcc_sha512=//p' "$work/prepared")
    printf '%s  %s\n' "$expected" "$build/cpp-toolchain/gcc-$version.tar.xz" | sha512sum -c -
    meson compile -C "$build" -j 1
}

build_core() {
    script="$work/core/container-build.sh"
    [ -f "$script" ] || fail "Gens core build is not implemented: $script is missing"
    MINIBOX_DIR="$work/minibox" MINIBOX_BUILD="$work/minibox-build" \
        ARTIFACT_DIR="$out" sh "$script"
}

check_input() {
    mkdir -p "$work/input-check"
    gcc -std=c11 -Wall -Wextra -Werror -UNDEBUG \
        -I"$work/core/waterbox" -I"$work/core/extern/gens-rerecording/Gens-rr/src" \
        "$work/core/waterbox/gens_input.c" "$work/core/tests/test_gens_input.c" \
        -o "$work/input-check/test-gens-input"
    "$work/input-check/test-gens-input"
}

check_rom() {
    mkdir -p "$work/rom-check"
    gcc -std=c11 -Wall -Wextra -Werror -UNDEBUG \
        -I"$work/core/waterbox" \
        "$work/core/waterbox/gens_rom.c" "$work/core/tests/test_gens_rom.c" \
        -o "$work/rom-check/test-gens-rom"
    "$work/rom-check/test-gens-rom"
}

check_frame() {
    mkdir -p "$work/frame-check"
    gcc -std=c11 -Wall -Wextra -Werror -UNDEBUG \
        -I"$work/core/waterbox" -I"$work/core/extern/gens-rerecording/Gens-rr/src" \
        "$work/core/waterbox/gens_frame.c" "$work/core/tests/test_gens_frame.c" \
        "$work/core/waterbox/gens_vdp_dma.c" \
        "$work/core/waterbox/gens_vdp_irq.c" \
        -o "$work/frame-check/test-gens-frame"
    "$work/frame-check/test-gens-frame"
}

check_audio() {
    mkdir -p "$work/audio-check"
    gcc -std=c11 -Wall -Wextra -Werror -UNDEBUG \
        -I"$work/core/waterbox" \
        "$work/core/waterbox/gens_audio.c" "$work/core/tests/test_gens_audio.c" \
        -o "$work/audio-check/test-gens-audio"
    "$work/audio-check/test-gens-audio"
    python3 "$work/core/tools/run_patched_audio_check.py" --reference "$gens"
}

check_m68k() {
    directory=$(mktemp -d "$out/m68k-reference.XXXXXXXX")
    sh "$work/core/reference/build_m68k_reference.sh" "$gens" "$directory/output"
}

check_fetch() {
    mkdir -p "$work/fetch-check"
    gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mcmodel=large -mno-red-zone -I"$work/core/waterbox" \
        "$work/core/waterbox/gens_m68k_fetch.c" "$work/core/tests/test_gens_m68k_fetch.c" \
        -o "$work/fetch-check/test-gens-m68k-fetch"
    "$work/fetch-check/test-gens-m68k-fetch"
}

check_bus() {
    mkdir -p "$work/bus-check"
    gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mcmodel=large -mno-red-zone -I"$work/core/waterbox" \
        "$work/core/waterbox/gens_m68k_bus.c" "$work/core/tests/test_gens_m68k_bus.c" \
        -o "$work/bus-check/test-gens-m68k-bus"
    "$work/bus-check/test-gens-m68k-bus"
}

check_bridge() {
    mkdir -p "$work/bridge-check"
    nasm -f elf64 "$work/core/waterbox/gens_m68k_bridge_x64.asm" \
        -o "$work/bridge-check/bridge.o"
    nasm -f elf64 "$work/core/tests/m68k_bridge_harness_x64.asm" \
        -o "$work/bridge-check/harness.o"
    gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mcmodel=large -mno-red-zone -I"$work/core/waterbox" \
        "$work/core/waterbox/gens_m68k_bus.c" "$work/core/tests/test_gens_m68k_bridge.c" \
        "$work/bridge-check/bridge.o" "$work/bridge-check/harness.o" \
        -Wl,--wrap=gens_m68k_read -Wl,--wrap=gens_m68k_write \
        -o "$work/bridge-check/test-gens-m68k-bridge"
    "$work/bridge-check/test-gens-m68k-bridge"
}

check_controller() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory="$work/controller-check"
    mkdir -p "$directory"
    nasm -f elf32 -I"$source/" "$source/io.asm" -o "$directory/original-io.o"
    nasm -f elf32 "$work/core/reference/io_reference_i386.asm" \
        -o "$directory/reference-wrapper.o"
    gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mstackrealign -fno-omit-frame-pointer -DGENS_IO_REFERENCE \
        -I"$work/core/waterbox" -I"$source" \
        "$work/core/waterbox/gens_input.c" "$work/core/tests/test_gens_io_trace.c" \
        "$directory/original-io.o" "$directory/reference-wrapper.o" \
        -Wl,-z,noexecstack -o "$directory/reference"
    nasm -f elf64 "$work/core/waterbox/gens_io_data_x64.asm" -o "$directory/io-data.o"
    gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mcmodel=large -mno-red-zone -I"$work/core/waterbox" -I"$source" \
        "$work/core/waterbox/gens_input.c" "$work/core/waterbox/gens_io.c" \
        "$work/core/tests/test_gens_io_trace.c" "$directory/io-data.o" \
        -o "$directory/ported"
    LC_ALL=C timeout 120s "$directory/reference" > "$directory/reference.trace" ||
        fail "original controller reference failed or timed out"
    LC_ALL=C timeout 120s "$directory/ported" > "$directory/ported.trace" ||
        fail "ported controller test failed or timed out"
    [ "$(wc -l < "$directory/reference.trace")" -eq 134 ] ||
        fail "controller reference produced an incomplete trace"
    [ "$(wc -l < "$directory/ported.trace")" -eq 134 ] ||
        fail "ported controller test produced an incomplete trace"
    cmp "$directory/reference.trace" "$directory/ported.trace" ||
        fail "controller traces differ: $directory/reference.trace and $directory/ported.trace"
    printf '%s\n' "Original and ported controller trace digests match."
}

check_genesis_bus() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory="$work/genesis-bus-check"
    mkdir -p "$directory"
    nasm -f elf64 "$work/core/waterbox/gens_io_data_x64.asm" -o "$directory/io-data.o"
    gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mcmodel=large -mno-red-zone -I"$work/core/waterbox" -I"$source" \
        "$work/core/waterbox/gens_m68k_bus.c" "$work/core/waterbox/gens_genesis_bus.c" \
        "$work/core/waterbox/gens_vdp.c" "$work/core/waterbox/gens_vdp_read.c" \
        "$work/core/waterbox/gens_vdp_write.c" "$work/core/waterbox/gens_vdp_registers.c" \
        "$work/core/waterbox/gens_vdp_dma.c" "$work/core/waterbox/gens_vdp_memory_dma.c" \
        "$work/core/waterbox/gens_z80_control.c" \
        "$work/core/waterbox/gens_z80_memory.c" \
        "$work/core/waterbox/gens_io.c" "$work/core/waterbox/gens_input.c" \
        "$work/core/tests/test_gens_genesis_bus.c" "$directory/io-data.o" \
        -o "$directory/test-genesis-bus"
    "$directory/test-genesis-bus"
}

check_genesis_bus_reference() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/genesis-bus-reference.XXXXXXXX")
    python3 "$work/core/tools/extract_genesis_bus_reference.py" \
        "$source/Mem_M68k.asm" "$directory/original-bus.asm"
    nasm -f elf32 -I"$source/" "$directory/original-bus.asm" -o "$directory/original-bus.o"
    nasm -f elf32 -I"$source/" "$source/io.asm" -o "$directory/original-io.o"
    nasm -f elf32 -I"$source/" "$source/Mem_Z80.asm" -o "$directory/original-z80-memory.o"
    nasm -f elf32 "$work/core/reference/genesis_bus_stubs_i386.asm" -o "$directory/stubs.o"
    gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mstackrealign -fno-omit-frame-pointer -DGENS_GENESIS_REFERENCE \
        -I"$work/core/waterbox" -I"$source" \
        "$work/core/waterbox/gens_input.c" "$work/core/tests/test_gens_genesis_bus_trace.c" \
        "$directory/original-bus.o" "$directory/original-io.o" \
        "$directory/original-z80-memory.o" "$directory/stubs.o" \
        -Wl,-z,noexecstack -o "$directory/reference"
    nasm -f elf64 "$work/core/waterbox/gens_io_data_x64.asm" -o "$directory/io-data.o"
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_GENESIS_NEGATIVE_CONTROL
        fi
        gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
            -mcmodel=large -mno-red-zone $define -I"$work/core/waterbox" -I"$source" \
            "$work/core/waterbox/gens_genesis_bus.c" "$work/core/waterbox/gens_io.c" \
            "$work/core/waterbox/gens_vdp.c" "$work/core/waterbox/gens_vdp_read.c" \
            "$work/core/waterbox/gens_vdp_write.c" "$work/core/waterbox/gens_vdp_registers.c" \
            "$work/core/waterbox/gens_vdp_dma.c" "$work/core/waterbox/gens_vdp_memory_dma.c" \
            "$work/core/waterbox/gens_z80_control.c" \
            "$work/core/waterbox/gens_z80_memory.c" \
            "$work/core/waterbox/gens_input.c" "$work/core/tests/test_gens_genesis_bus_trace.c" \
            "$directory/io-data.o" -Wl,-z,noexecstack -o "$directory/$variant"
    done
    LC_ALL=C timeout 120s "$directory/reference" > "$directory/reference.trace" ||
        fail "original Genesis bus reference failed or timed out: $directory"
    LC_ALL=C timeout 120s "$directory/ported" > "$directory/ported.trace" ||
        fail "ported Genesis bus test failed or timed out: $directory"
    [ "$(wc -l < "$directory/reference.trace")" -eq 9 ] ||
        fail "Genesis bus reference produced an incomplete trace: $directory"
    [ "$(wc -l < "$directory/ported.trace")" -eq 9 ] ||
        fail "ported Genesis bus test produced an incomplete trace: $directory"
    cmp "$directory/reference.trace" "$directory/ported.trace" ||
        fail "Genesis bus traces differ: $directory/reference.trace and $directory/ported.trace"
    LC_ALL=C timeout 120s "$directory/negative" > "$directory/negative.trace" ||
        fail "Genesis bus negative control failed or timed out: $directory"
    [ "$(wc -l < "$directory/negative.trace")" -eq 9 ] ||
        fail "Genesis bus negative control produced an incomplete trace: $directory"
    if cmp -s "$directory/reference.trace" "$directory/negative.trace"; then
        fail "Genesis bus comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original and ported Genesis bus trace digests match: $directory"
}

check_z80_control() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory="$work/z80-control-check"
    mkdir -p "$directory"
    nasm -f elf64 "$work/core/waterbox/gens_io_data_x64.asm" -o "$directory/io-data.o"
    gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mcmodel=large -mno-red-zone -I"$work/core/waterbox" -I"$source" \
        "$work/core/waterbox/gens_z80_control.c" "$work/core/tests/test_gens_z80_control.c" \
        "$directory/io-data.o" -Wl,-z,noexecstack -o "$directory/test-z80-control"
    "$directory/test-z80-control"
}

check_z80_memory() {
    directory="$work/z80-memory-check"
    mkdir -p "$directory"
    gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mcmodel=large -mno-red-zone -I"$work/core/waterbox" \
        "$work/core/waterbox/gens_z80_memory.c" "$work/core/tests/test_gens_z80_memory.c" \
        -o "$directory/test-z80-memory"
    "$directory/test-z80-memory"
}

check_z80_native() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/z80-native-check.XXXXXXXX")
    nasm -f elf32 -I"$source/" "$source/Mem_Z80.asm" -o "$directory/original-memory.o"
    nasm -f elf32 "$work/core/reference/z80_native_reference_i386.asm" \
        -o "$directory/reference-wrapper.o"
    gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mstackrealign -fno-omit-frame-pointer -DGENS_Z80_NATIVE_REFERENCE \
        -I"$work/core/waterbox" "$work/core/tests/test_gens_z80_native.c" \
        "$directory/original-memory.o" "$directory/reference-wrapper.o" \
        -Wl,-z,noexecstack -o "$directory/reference"
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_Z80_NATIVE_NEGATIVE_CONTROL
        fi
        gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
            -mcmodel=large -mno-red-zone $define -I"$work/core/waterbox" \
            "$work/core/waterbox/gens_z80_memory.c" "$work/core/waterbox/gens_z80_native.c" \
            "$work/core/tests/test_gens_z80_native.c" -o "$directory/$variant"
    done
    for variant in reference ported negative; do
        LC_ALL=C timeout 120s "$directory/$variant" > "$directory/$variant.trace" ||
            fail "Z80 native $variant failed or timed out: $directory"
        [ "$(wc -l < "$directory/$variant.trace")" -eq 4 ] ||
            fail "Z80 native $variant produced an incomplete trace: $directory"
    done
    cmp "$directory/reference.trace" "$directory/ported.trace" ||
        fail "Z80 native traces differ: $directory/reference.trace and $directory/ported.trace"
    if cmp -s "$directory/reference.trace" "$directory/negative.trace"; then
        fail "Z80 native comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original and ported native Z80 memory trace digests match: $directory"
}

check_sound_chips() {
    original="$work/core/extern/gens-rerecording/Gens-rr/src"
    mkdir -p "$work/core/build"
    directory=$(mktemp -d "$work/core/build/sound-chip-check.XXXXXXXX")
    python3 "$work/core/tools/prepare_patched_source.py" --reference "$gens" \
        --destination "$directory/patched"
    patched="$directory/patched/Gens-rr/src"
    gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -finput-charset=ISO-8859-1 \
        -I"$work/core/waterbox" -I"$patched" \
        "$work/core/waterbox/gens_sound.c" "$work/core/waterbox/gens_z80_memory.c" \
        "$work/core/waterbox/gens_pcm.c" \
        "$work/core/tests/test_gens_sound.c" \
        -o "$directory/adapter-check"
    "$directory/adapter-check"
    for architecture in reference ported; do
        if [ "$architecture" = reference ]; then
            bits=-m32
            source="$original"
        else
            bits='-m64 -mcmodel=large -mno-red-zone'
            source="$patched"
        fi
        for chip in ym2612 psg; do
            gcc $bits -std=gnu99 -O2 -fgnu89-inline -fwrapv -fno-strict-aliasing \
                -msse2 -mfpmath=sse -ffp-contract=off -fexcess-precision=standard \
                -fno-pie -finput-charset=ISO-8859-1 \
                -I"$work/core/waterbox" -I"$source" \
                -c "$source/$chip.c" -o "$directory/$architecture-$chip.o"
        done
    done
    gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -msse2 -mfpmath=sse -ffp-contract=off -fexcess-precision=standard \
        -finput-charset=ISO-8859-1 -DGENS_SOUND_REFERENCE -I"$original" \
        "$work/core/tests/test_gens_sound_chips.c" \
        "$directory/reference-ym2612.o" "$directory/reference-psg.o" \
        -lm -o "$directory/reference"
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_SOUND_NEGATIVE_CONTROL
        fi
        gcc -m64 -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
            -mcmodel=large -mno-red-zone -finput-charset=ISO-8859-1 $define \
            -I"$work/core/waterbox" -I"$patched" \
            "$work/core/waterbox/gens_audio.c" "$work/core/waterbox/gens_sound.c" \
            "$work/core/waterbox/gens_pcm.c" \
            "$work/core/tests/test_gens_sound_chips.c" \
            "$directory/ported-ym2612.o" "$directory/ported-psg.o" \
            -lm -o "$directory/$variant"
    done
    for variant in reference ported negative; do
        LC_ALL=C timeout 120s "$directory/$variant" > "$directory/$variant.trace" ||
            fail "sound chip $variant failed or timed out: $directory"
        [ "$(wc -l < "$directory/$variant.trace")" -eq 24 ] ||
            fail "sound chip $variant produced an incomplete trace: $directory"
    done
    cmp "$directory/reference.trace" "$directory/ported.trace" ||
        fail "sound chip traces differ: $directory/reference.trace and $directory/ported.trace"
    if cmp -s "$directory/reference.trace" "$directory/negative.trace"; then
        fail "sound chip comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original i386 and candidate x86-64 sound chip trace digests match: $directory"
}

check_pcm() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/pcm-check.XXXXXXXX")
    python3 "$work/core/tools/extract_pcm_reference.py" "$source" "$directory/reference-source"
    nasm -f elf32 -I"$source/" "$directory/reference-source/pcm_reference.asm" \
        -o "$directory/original-mmx.o"
    gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -UNDEBUG \
        -fwrapv -fno-pie -no-pie -msse2 -mfpmath=sse -DGENS_PCM_REFERENCE \
        -I"$work/core/waterbox" -I"$directory/reference-source" \
        "$work/core/tests/test_gens_pcm.c" "$directory/original-mmx.o" \
        -Wl,-z,noexecstack -o "$directory/reference"
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_PCM_NEGATIVE_CONTROL
        fi
        gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
            -mcmodel=large -mno-red-zone $define -I"$work/core/waterbox" \
            "$work/core/waterbox/gens_pcm.c" "$work/core/tests/test_gens_pcm.c" \
            -o "$directory/$variant"
    done
    for variant in reference ported negative; do
        LC_ALL=C timeout 120s "$directory/$variant" > "$directory/$variant.trace" ||
            fail "PCM $variant failed or timed out: $directory"
        [ "$(wc -l < "$directory/$variant.trace")" -eq 16 ] ||
            fail "PCM $variant produced an incomplete trace: $directory"
    done
    cmp "$directory/reference.trace" "$directory/ported.trace" ||
        fail "PCM traces differ: $directory/reference.trace and $directory/ported.trace"
    if cmp -s "$directory/reference.trace" "$directory/negative.trace"; then
        fail "PCM comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original and ported PCM trace digests match: $directory"
}

check_vdp_read() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/vdp-read-check.XXXXXXXX")
    python3 "$work/core/tools/extract_vdp_read_reference.py" \
        "$source/vdp_io.asm" "$directory/reference.asm"
    nasm -f elf32 -I"$source/" "$directory/reference.asm" -o "$directory/reference.o"
    gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mstackrealign -fno-omit-frame-pointer -DGENS_VDP_READ_REFERENCE \
        -I"$work/core/waterbox" "$work/core/tests/test_gens_vdp_read.c" \
        "$directory/reference.o" -Wl,-z,noexecstack -o "$directory/reference"
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_VDP_READ_NEGATIVE_CONTROL
        fi
        gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
            -mcmodel=large -mno-red-zone $define -I"$work/core/waterbox" \
            "$work/core/waterbox/gens_vdp_read.c" "$work/core/waterbox/gens_z80_native.c" \
            "$work/core/waterbox/gens_z80_memory.c" "$work/core/tests/test_gens_vdp_read.c" \
            -o "$directory/$variant"
    done
    for variant in reference ported negative; do
        LC_ALL=C timeout 120s "$directory/$variant" > "$directory/$variant.trace" ||
            fail "VDP read $variant failed or timed out: $directory"
        [ "$(wc -l < "$directory/$variant.trace")" -eq 4 ] ||
            fail "VDP read $variant produced an incomplete trace: $directory"
    done
    cmp "$directory/reference.trace" "$directory/ported.trace" ||
        fail "VDP read traces differ: $directory/reference.trace and $directory/ported.trace"
    if cmp -s "$directory/reference.trace" "$directory/negative.trace"; then
        fail "VDP read comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original and ported VDP read trace digests match: $directory"
}

check_vdp_write() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/vdp-write-check.XXXXXXXX")
    gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG \
        -fno-pie -no-pie -mcmodel=large -mno-red-zone \
        -I"$work/core/waterbox" "$work/core/waterbox/gens_vdp_write.c" \
        "$work/core/tests/test_gens_vdp_write.c" -o "$directory/test-vdp-write"
    "$directory/test-vdp-write"
    python3 "$work/core/tools/extract_vdp_copy_reference.py" \
        "$source/vdp_io.asm" "$directory/reference.asm"
    nasm -f elf32 -I"$source/" "$directory/reference.asm" -o "$directory/reference.o"
    gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mstackrealign -fno-omit-frame-pointer -DGENS_VDP_COPY_REFERENCE \
        -I"$work/core/waterbox" "$work/core/tests/test_gens_vdp_copy.c" \
        "$directory/reference.o" -Wl,-z,noexecstack -o "$directory/reference"
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_VDP_COPY_NEGATIVE_CONTROL
        fi
        gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
            -mcmodel=large -mno-red-zone $define -I"$work/core/waterbox" \
            "$work/core/waterbox/gens_vdp_write.c" \
            "$work/core/tests/test_gens_vdp_copy.c" -o "$directory/$variant"
    done
    for variant in reference ported negative; do
        LC_ALL=C timeout 120s "$directory/$variant" > "$directory/$variant.trace" ||
            fail "VDP copy $variant failed or timed out: $directory"
        [ "$(wc -l < "$directory/$variant.trace")" -eq 2 ] ||
            fail "VDP copy $variant produced an incomplete trace: $directory"
    done
    cmp "$directory/reference.trace" "$directory/ported.trace" ||
        fail "VDP copy traces differ: $directory"
    if cmp -s "$directory/reference.trace" "$directory/negative.trace"; then
        fail "VDP copy comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original and ported VDP copy trace digests match: $directory"
}

check_vdp_integration() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/vdp-integration-check.XXXXXXXX")
    nasm -f elf64 "$work/core/waterbox/gens_io_data_x64.asm" -o "$directory/io-data.o"
    gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mcmodel=large -mno-red-zone -I"$work/core/waterbox" -I"$source" \
        "$work/core/waterbox/gens_vdp.c" "$work/core/waterbox/gens_vdp_read.c" \
        "$work/core/waterbox/gens_vdp_write.c" "$work/core/waterbox/gens_vdp_registers.c" \
        "$work/core/waterbox/gens_vdp_dma.c" "$work/core/waterbox/gens_vdp_memory_dma.c" \
        "$work/core/waterbox/gens_vdp_irq.c" \
        "$work/core/waterbox/gens_m68k_bus.c" "$work/core/waterbox/gens_genesis_bus.c" \
        "$work/core/waterbox/gens_z80_control.c" "$work/core/waterbox/gens_z80_memory.c" \
        "$work/core/waterbox/gens_io.c" "$work/core/waterbox/gens_input.c" \
        "$work/core/tests/test_gens_vdp.c" "$directory/io-data.o" \
        -Wl,-z,noexecstack -o "$directory/integration"
    "$directory/integration"
}

check_vdp_scroll() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/vdp-scroll-check.XXXXXXXX")
    python3 "$work/core/tools/extract_vdp_scroll_reference.py" \
        "$source/vdp_rend.asm" "$directory/reference.asm"
    nasm -f elf32 -I"$source/" "$directory/reference.asm" -o "$directory/reference.o"
    gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mstackrealign -fno-omit-frame-pointer -DGENS_VDP_SCROLL_REFERENCE \
        -I"$work/core/waterbox" "$work/core/tests/test_gens_vdp_scroll.c" \
        "$directory/reference.o" -Wl,-z,noexecstack -o "$directory/reference"
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_VDP_SCROLL_NEGATIVE_CONTROL
        fi
        gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
            -mcmodel=large -mno-red-zone $define -I"$work/core/waterbox" \
            "$work/core/waterbox/gens_vdp_sprites.c" "$work/core/waterbox/gens_vdp_pixels.c" \
            "$work/core/waterbox/gens_vdp_scroll.c" \
            "$work/core/tests/test_gens_vdp_scroll.c" -o "$directory/$variant"
    done
    for variant in reference ported negative; do
        LC_ALL=C timeout 120s "$directory/$variant" > "$directory/$variant.trace" ||
            fail "VDP scroll $variant failed or timed out: $directory"
        [ "$(wc -l < "$directory/$variant.trace")" -eq 2 ] ||
            fail "VDP scroll $variant produced an incomplete trace: $directory"
    done
    cmp "$directory/reference.trace" "$directory/ported.trace" ||
        fail "VDP scroll traces differ: $directory"
    if cmp -s "$directory/reference.trace" "$directory/negative.trace"; then
        fail "VDP scroll comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original and ported scroll A/B digests match: $directory"
}

check_z80_cpu() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/z80-cpu-check.XXXXXXXX")
    python3 "$work/core/tools/generate_z80_tables.py" "$source/z80.asm" "$directory"
    cmp "$directory/gens_z80_tables.c" "$work/core/waterbox/gens_z80_tables.c" ||
        fail "committed Z80 tables differ from the pinned source: $directory"
    cmp "$directory/gens_z80_tables.h" "$work/core/waterbox/gens_z80_tables.h" ||
        fail "committed Z80 table header differs from the pinned source: $directory"
    nasm -w-label-orphan -f elf32 "$source/z80.asm" -o "$directory/z80.o"
    nasm -f elf32 "$work/core/tests/test_z80_diff_thunks.asm" -o "$directory/thunks.o"
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_Z80_DIFF_NEGATIVE_CONTROL
        fi
        gcc -m32 -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -UNDEBUG \
            -fno-pie -no-pie -mstackrealign $define -I"$work/core/waterbox" -I"$source" \
            "$work/core/tests/test_z80_diff.c" "$work/core/waterbox/gens_z80_cpu.c" \
            "$work/core/waterbox/gens_z80_tables.c" "$directory/z80.o" "$directory/thunks.o" \
            -Wl,-z,noexecstack -o "$directory/$variant"
    done
    LC_ALL=C timeout 900s "$directory/ported" 20000 > "$directory/ported.log" ||
        fail "Z80 differential fuzzing found a mismatch: $directory/ported.log"
    grep -q ', 0 mismatched$' "$directory/ported.log" ||
        fail "Z80 differential fuzzing did not report a clean run: $directory/ported.log"
    if LC_ALL=C timeout 900s "$directory/negative" 2000 > "$directory/negative.log"; then
        fail "Z80 differential fuzzing did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original and ported Z80 cores agree: $(tail -n 1 "$directory/ported.log")"
}

check_m68k_cpu() {
    star="$work/core/extern/gens-rerecording/Gens-rr/Starscream/Main68k/Star.c"
    directory=$(mktemp -d "$work/m68k-cpu-check.XXXXXXXX")
    python3 "$work/core/tools/generate_m68k_tables.py" --cc gcc "$star" "$directory"
    cmp "$directory/gens_m68k_tables.c" "$work/core/waterbox/gens_m68k_tables.c" ||
        fail "committed 68000 tables differ from the pinned generator: $directory"
    cmp "$directory/gens_m68k_tables.h" "$work/core/waterbox/gens_m68k_tables.h" ||
        fail "committed 68000 table header differs from the pinned generator: $directory"
    gcc -O1 -w "$star" -o "$directory/star"
    (cd "$directory" && ./star main68k.asm -quiet -hog -name main68k_) ||
        fail "the pinned Starscream generator failed: $directory"
    nasm -w-label-orphan -f elf32 "$directory/main68k.asm" -o "$directory/main68k.o"
    nasm -f elf32 "$work/core/tests/test_m68k_diff_glue.asm" -o "$directory/glue.o"
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_M68K_DIFF_NEGATIVE_CONTROL
        fi
        gcc -m32 -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -UNDEBUG \
            -fno-pie -no-pie -mstackrealign $define -I"$work/core/waterbox" \
            "$work/core/tests/test_m68k_diff.c" "$work/core/waterbox/gens_m68k_cpu.c" \
            "$work/core/waterbox/gens_m68k_tables.c" "$directory/main68k.o" "$directory/glue.o" \
            -Wl,-z,noexecstack -o "$directory/$variant"
    done
    LC_ALL=C timeout 1800s "$directory/ported" 20000 > "$directory/ported.log" ||
        fail "68000 differential fuzzing found a mismatch: $directory/ported.log"
    grep -q ', 0 mismatched$' "$directory/ported.log" ||
        fail "68000 differential fuzzing did not report a clean run: $directory/ported.log"
    if LC_ALL=C timeout 900s "$directory/negative" 2000 > "$directory/negative.log"; then
        fail "68000 differential fuzzing did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original and ported 68000 cores agree: $(tail -n 1 "$directory/ported.log")"
}

# Builds the headless reference from the unchanged i386 Gens modules, the ported machine, and
# the core's native build, then compares them frame by frame on freely licensed programs.
check_machine() {
    printf '\n%s\n' "=== Whole-machine comparison: building reference and port ===" >&2
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    star="$work/core/extern/gens-rerecording/Gens-rr/Starscream/Main68k/Star.c"
    tools="$work/core/tools"
    roms="$work/test-roms"
    [ -d "$roms" ] || fail "test programs are missing: $roms. Run the networked prepare service."
    mkdir -p "$work/core/build"
    directory=$(mktemp -d "$work/core/build/machine-check.XXXXXXXX")
    python3 "$tools/prepare_patched_source.py" --reference "$gens" \
        --destination "$directory/patched"
    patched="$directory/patched/Gens-rr/src"

    # the original: Starscream as Gens generates it, Gens' assembly and C, unchanged
    gcc -O1 -w "$star" -o "$directory/star"
    (cd "$directory" && ./star main68k.asm -quiet -hog -name main68k_) ||
        fail "the pinned Starscream generator failed: $directory"
    mkdir "$directory/reference-objects"
    nasm -w-label-orphan -f elf32 "$directory/main68k.asm" -o "$directory/reference-objects/main68k.o"
    for module in Mem_M68k Mem_Z80 vdp_io vdp_rend io z80 Misc; do
        nasm -w-label-orphan -w-other -f elf32 -I"$source/" "$source/$module.asm" \
            -o "$directory/reference-objects/$module.o"
    done
    nasm -f elf32 "$work/core/reference/gens_headless_stubs_i386.asm" \
        -o "$directory/reference-objects/stubs.o"
    nasm -f elf32 "$work/core/reference/gens_headless_sound_abi_i386.asm" \
        -o "$directory/reference-objects/sound-abi.o"
    for file in "$work/core/reference/gens_headless_reference.c" \
        "$work/core/waterbox/gens_input.c" "$work/core/waterbox/gens_color_lookup.c" \
        "$work/core/waterbox/gens_pcm.c" "$source/Cpu_68k.c" "$source/Cpu_Z80.c" \
        "$source/ym2612.c" "$source/psg.c"; do
        gcc -m32 -std=gnu99 -O2 -fgnu89-inline -fwrapv -fno-strict-aliasing -msse2 \
            -mfpmath=sse -ffp-contract=off -fexcess-precision=standard -fno-pie \
            -finput-charset=ISO-8859-1 -w '-D__fastcall=__attribute__((fastcall))' \
            -DYM2612_Write=YM2612_Write_C -DYM2612_Read=YM2612_Read_C -DPSG_Write=PSG_Write_C \
            -I"$work/core/reference/winshim" -I"$work/core/reference/winshim/cpuz80" \
            -I"$source" -I"$work/core/waterbox" \
            -I"$work/core/tests" -c "$file" \
            -o "$directory/reference-objects/$(basename "$file" .c).o"
    done
    python3 "$tools/elf_aliases.py" "$directory/reference-objects/"*.o > "$directory/aliases.ld"
    gcc -m32 -no-pie "$directory/reference-objects/"*.o "$directory/aliases.ld" -lm \
        -Wl,-z,noexecstack -o "$directory/reference"

    # the port: the machine, and the core's own exports over it
    nasm -f elf64 "$work/core/waterbox/gens_io_data_x64.asm" -o "$directory/io-data.o"
    gcc -std=gnu99 -O2 -fgnu89-inline -fwrapv -fno-strict-aliasing -msse2 -ffp-contract=off \
        -finput-charset=ISO-8859-1 -w -I"$work/core/waterbox" -I"$patched" \
        -DYM2612_Write=YM2612_Write_C -c "$patched/ym2612.c" -o "$directory/ym2612-traced.o"
    machine="cinterface gens_gmv gens_machine gens_m68k_cpu gens_m68k_tables gens_z80_cpu
        gens_z80_tables gens_genesis_bus gens_io gens_input gens_vdp gens_vdp_read
        gens_vdp_write gens_vdp_registers gens_vdp_dma gens_vdp_memory_dma gens_vdp_irq
        gens_vdp_reset gens_vdp_sprites gens_vdp_sprite_draw gens_vdp_scroll gens_vdp_pixels
        gens_vdp_palette gens_vdp_render gens_vdp_video gens_vdp_machine gens_color_lookup
        gens_z80_control gens_z80_memory gens_z80_native gens_sound gens_pcm gens_audio gens_rom"
    port_sources=
    for name in $machine; do
        [ "$name" = cinterface ] || [ "$name" = gens_gmv ] ||
            port_sources="$port_sources $work/core/waterbox/$name.c"
    done
    native_sources=
    for name in $machine; do native_sources="$native_sources $work/core/waterbox/$name.c"; done
    cflags="-std=gnu11 -O2 -fgnu89-inline -fwrapv -fno-strict-aliasing -msse2 -ffp-contract=off
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -finput-charset=ISO-8859-1"
    for variant in port negative; do
        define=
        [ "$variant" = negative ] && define=-DGENS_MACHINE_NEGATIVE_CONTROL
        # shellcheck disable=SC2086
        gcc $cflags $define -DGENS_HEADLESS_TRACE_YM -I"$work/core/waterbox" \
            -I"$work/core/tests" -I"$patched" $port_sources "$work/core/tests/gens_headless_port.c" \
            "$directory/ym2612-traced.o" "$patched/psg.c" "$directory/io-data.o" -lm \
            -Wl,-z,noexecstack -o "$directory/$variant"
    done
    # shellcheck disable=SC2086
    gcc $cflags -I"$work/core/waterbox/native-shim" -I"$work/core/waterbox" \
        -I"$work/core/tests" -I"$patched" -I"$work/minibox/source/guest/include" \
        -I"$work/minibox/extern/jsmn" $native_sources "$work/core/waterbox/run-native.c" \
        "$patched/ym2612.c" "$patched/psg.c" "$directory/io-data.o" -lm \
        -Wl,-z,noexecstack -o "$directory/run-native"

    # shellcheck disable=SC2086
    gcc $cflags -UNDEBUG -I"$work/core/waterbox/native-shim" -I"$work/core/waterbox" \
        -I"$patched" -I"$work/minibox/source/guest/include" -I"$work/minibox/extern/jsmn" \
        $native_sources "$work/core/tests/test_gens_tooling.c" \
        "$patched/ym2612.c" "$patched/psg.c" "$directory/io-data.o" -lm \
        -Wl,-z,noexecstack -o "$directory/test-tooling"
    "$directory/test-tooling" || fail "core tooling exports failed: $directory"

    # shellcheck disable=SC2086
    gcc $cflags -UNDEBUG -I"$work/core/waterbox/native-shim" -I"$work/core/waterbox" \
        -I"$work/core/tests" -I"$patched" -I"$work/minibox/source/guest/include" \
        -I"$work/minibox/extern/jsmn" $native_sources "$work/core/tests/test_gens_memory_callbacks.c" \
        "$patched/ym2612.c" "$patched/psg.c" "$directory/io-data.o" -lm \
        -Wl,-z,noexecstack -o "$directory/test-memory-callbacks"
    python3 "$work/core/tests/run_memory_callbacks.py" "$directory/test-memory-callbacks" ||
        fail "memory callback witness failed: $directory"

    # every program: the original against the port, with the same scripted input.
    # These programs run a Z80 sound driver, so a Z80 that never runs fails the check.
    z80_programs=" sgdk_game_platformer sgdk_game_sonic sgdk_project_template sgdk_snd_xgm-player "
    count=0
    printf '\n%s\n' "=== Synthetic-input tests: original Gens vs port (6000 frames each) ===" >&2
    for rom in "$roms"/*.bin; do
        name=$(basename "$rom" .bin)
        printf '\n%s\n' "Testing ROM: $name.bin" >&2
        require=
        case "$z80_programs" in *" $name "*) require=--require-changing=z80,ramz80 ;; esac
        python3 "$tools/headless_input.py" 6000 11 "$directory/$name.input"
        printf '%s\n' "  Running original Gens reference..." >&2
        LC_ALL=C timeout 900s "$directory/reference" "$rom" 6000 "$directory/$name.input" \
            "$directory/$name.reference" || fail "the reference failed on $name: $directory"
        printf '%s\n' "  Running port..." >&2
        LC_ALL=C timeout 900s "$directory/port" "$rom" 6000 "$directory/$name.input" \
            "$directory/$name.port" || fail "the port failed on $name: $directory"
        # shellcheck disable=SC2086
        python3 "$tools/compare_headless_traces.py" $require "$directory/$name.reference" \
            "$directory/$name.port" > "$directory/$name.result" ||
            fail "$name: the port differs from the original: $(cat "$directory/$name.result")"
        count=$((count + 1))
        printf '%s\n' "  PASS: $name.bin ($count programs matched)" >&2
    done
    [ "$count" -gt 0 ] || fail "no test programs in $roms"
    printf '\n%s\n' "=== Negative control: 240p.bin (300 frames) ===" >&2
    LC_ALL=C timeout 900s "$directory/negative" "$roms/240p.bin" 300 - "$directory/negative.trace" ||
        fail "the negative control failed to run: $directory"
    if python3 "$tools/compare_headless_traces.py" "$directory/240p.reference" \
        "$directory/negative.trace" > /dev/null 2>&1; then
        fail "the machine comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "  PASS: intentional difference detected" >&2

    # Gens movies: the original's own playback code against the core's import tool and FrameAdvance
    printf '\n%s\n' "=== Generated GMV movie tests: original Gens vs Chimera core (5000 frames each) ===" >&2
    for movie in "240p 6 6 0" "sgdk_game_platformer 6 6 0" "sgdk_snd_xgm-player 3 6 0" \
        "sgdk_joy-test 6 3 0x20" "battle-4tris 6 6 0xa0"; do
        set -- $movie
        printf '\n%s\n' "Testing ROM: $1.bin (generated GMV, P1=$2-button, P2=$3-button, flags=$4)" >&2
        run="$directory/movie-$1-$4"
        require=
        case "$z80_programs" in *" $1 "*) require=--require-changing=z80,ramz80 ;; esac
        mkdir "$run"
        python3 "$tools/headless_input.py" 5000 21 "$run/script"
        python3 "$tools/make_test_gmv.py" "$run/script" "$run/movie" --p1 "$2" --p2 "$3" --flags "$4"
        cp "$roms/$1.bin" "$run/game.bin"
        cp "$run/game.bin" "$run/cartridge"
        printf '{"tool":"import_gmv","package":{"sha1":"","version":""},"options":{},"inputs":{"movie":"movie.gmv","cartridge":"game.bin"},"outputs":{"project":"movie.chimeraProject"}}' \
            > "$run/tool"
        "$directory/run-native" "$run" tool > "$run/answer.json"
        rm "$run/cartridge"
        python3 - "$run" <<'PYIMPORT'
import hashlib, json, sys
run = sys.argv[1]
answer = json.load(open(run + "/answer.json"))
if "done" not in answer:
    sys.exit("the import was refused: " + json.dumps(answer))
project = json.load(open(run + "/project.json"))
rom = project["files"][0]
if rom["name"] != "game.bin" or rom["sha1"] != hashlib.sha1(open(run + "/game.bin", "rb").read()).hexdigest().upper():
    sys.exit("the project does not name the cartridge by its name and SHA-1: " + json.dumps(rom))
open(run + "/settings", "w").write(json.dumps(project["settings"]))
open(run + "/slots", "w").write(json.dumps({"rom": ["game.bin"]}))
open(run + "/input.log", "w").write(open(run + "/project.input").read())
PYIMPORT
        cp "$run/movie" "$run/movie.gmv"
        printf '%s\n' "  Running original Gens movie playback..." >&2
        LC_ALL=C timeout 900s "$directory/reference" "$run/game.bin" 5000 "$run/movie.gmv" \
            "$run/reference.trace" || fail "the reference failed on the movie: $run"
        printf '%s\n' "  Running Chimera core movie playback..." >&2
        LC_ALL=C timeout 900s "$directory/run-native" "$run" play 5000 "$run/input.log" \
            "$run/native.trace" || fail "the core failed on the movie: $run"
        # shellcheck disable=SC2086
        python3 "$tools/compare_headless_traces.py" --skip audio_l,audio_r,ym $require \
            "$run/reference.trace" "$run/native.trace" > "$run/result" ||
            fail "an imported Gens movie plays differently: $(cat "$run/result") ($run)"
        printf '%s\n' "  PASS: $1.bin movie traces match" >&2
    done
    printf '%s\n' "The ported machine matches the original Gens on $count programs and 5 movies: $directory"
}

check_vdp_machine() {
    directory=$(mktemp -d "$work/vdp-machine-check.XXXXXXXX")
    gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mcmodel=large -mno-red-zone -I"$work/core/waterbox" \
        "$work/core/waterbox/gens_vdp.c" "$work/core/waterbox/gens_vdp_read.c" \
        "$work/core/waterbox/gens_vdp_write.c" "$work/core/waterbox/gens_vdp_registers.c" \
        "$work/core/waterbox/gens_vdp_dma.c" "$work/core/waterbox/gens_vdp_memory_dma.c" \
        "$work/core/waterbox/gens_vdp_irq.c" "$work/core/waterbox/gens_vdp_sprites.c" \
        "$work/core/waterbox/gens_vdp_pixels.c" "$work/core/waterbox/gens_vdp_scroll.c" \
        "$work/core/waterbox/gens_vdp_sprite_draw.c" "$work/core/waterbox/gens_vdp_palette.c" \
        "$work/core/waterbox/gens_vdp_render.c" "$work/core/waterbox/gens_vdp_video.c" \
        "$work/core/waterbox/gens_color_lookup.c" "$work/core/waterbox/gens_vdp_machine.c" \
        "$work/core/tests/test_gens_vdp_machine.c" -o "$directory/machine"
    [ "$(LC_ALL=C timeout 120s "$directory/machine")" = "vdp machine ok" ] ||
        fail "VDP machine wiring test failed: $directory"
    printf '%s\n' "VDP machine wiring test passed: $directory"
}

check_vdp_render() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/vdp-render-check.XXXXXXXX")
    python3 "$work/core/tools/extract_vdp_render_reference.py" \
        "$source/vdp_rend.asm" "$directory/reference.asm"
    nasm -f elf32 -I"$source/" "$directory/reference.asm" -o "$directory/reference.o"
    gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mstackrealign -fno-omit-frame-pointer -DGENS_VDP_RENDER_REFERENCE \
        -I"$work/core/waterbox" "$work/core/tests/test_gens_vdp_render.c" \
        "$directory/reference.o" -Wl,-z,noexecstack -o "$directory/reference"
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_VDP_RENDER_NEGATIVE_CONTROL
        fi
        gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
            -mcmodel=large -mno-red-zone $define -I"$work/core/waterbox" \
            "$work/core/waterbox/gens_vdp_sprites.c" "$work/core/waterbox/gens_vdp_pixels.c" \
            "$work/core/waterbox/gens_vdp_scroll.c" "$work/core/waterbox/gens_vdp_sprite_draw.c" \
            "$work/core/waterbox/gens_vdp_palette.c" "$work/core/waterbox/gens_vdp_render.c" \
            "$work/core/tests/test_gens_vdp_render.c" -o "$directory/$variant"
    done
    for variant in reference ported negative; do
        LC_ALL=C timeout 300s "$directory/$variant" > "$directory/$variant.trace" ||
            fail "VDP render $variant failed or timed out: $directory"
        [ "$(wc -l < "$directory/$variant.trace")" -eq 1 ] ||
            fail "VDP render $variant produced an incomplete trace: $directory"
    done
    cmp "$directory/reference.trace" "$directory/ported.trace" ||
        fail "VDP render traces differ: $directory"
    if cmp -s "$directory/reference.trace" "$directory/negative.trace"; then
        fail "VDP render comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original and ported Render_Line digests match: $directory"
}

check_vdp_sprite_draw() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/vdp-sprite-draw-check.XXXXXXXX")
    python3 "$work/core/tools/extract_vdp_sprite_draw_reference.py" \
        "$source/vdp_rend.asm" "$directory/reference.asm"
    nasm -f elf32 -I"$source/" "$directory/reference.asm" -o "$directory/reference.o"
    gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mstackrealign -fno-omit-frame-pointer -DGENS_VDP_SPRITE_DRAW_REFERENCE \
        -I"$work/core/waterbox" "$work/core/tests/test_gens_vdp_sprite_draw.c" \
        "$directory/reference.o" -Wl,-z,noexecstack -o "$directory/reference"
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_VDP_SPRITE_DRAW_NEGATIVE_CONTROL
        fi
        gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
            -mcmodel=large -mno-red-zone $define -I"$work/core/waterbox" \
            "$work/core/waterbox/gens_vdp_sprites.c" "$work/core/waterbox/gens_vdp_pixels.c" \
            "$work/core/waterbox/gens_vdp_sprite_draw.c" \
            "$work/core/tests/test_gens_vdp_sprite_draw.c" -o "$directory/$variant"
    done
    for variant in reference ported negative; do
        LC_ALL=C timeout 120s "$directory/$variant" > "$directory/$variant.trace" ||
            fail "VDP sprite draw $variant failed or timed out: $directory"
        [ "$(wc -l < "$directory/$variant.trace")" -eq 1 ] ||
            fail "VDP sprite draw $variant produced an incomplete trace: $directory"
    done
    cmp "$directory/reference.trace" "$directory/ported.trace" ||
        fail "VDP sprite drawing traces differ: $directory"
    if cmp -s "$directory/reference.trace" "$directory/negative.trace"; then
        fail "VDP sprite drawing comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original and ported sprite traversal/pixel digests match: $directory"
}

check_vdp_pixels() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/vdp-pixels-check.XXXXXXXX")
    python3 "$work/core/tools/extract_vdp_pixels_reference.py" \
        "$source/vdp_rend.asm" "$directory/reference.asm"
    nasm -f elf32 -I"$source/" "$directory/reference.asm" -o "$directory/reference.o"
    gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mstackrealign -fno-omit-frame-pointer -DGENS_VDP_PIXELS_REFERENCE \
        -I"$work/core/waterbox" "$work/core/tests/test_gens_vdp_pixels.c" \
        "$directory/reference.o" -Wl,-z,noexecstack -o "$directory/reference"
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_VDP_PIXELS_NEGATIVE_CONTROL
        fi
        gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
            -mcmodel=large -mno-red-zone $define -I"$work/core/waterbox" \
            "$work/core/waterbox/gens_vdp_pixels.c" \
            "$work/core/tests/test_gens_vdp_pixels.c" -o "$directory/$variant"
    done
    for variant in reference ported negative; do
        LC_ALL=C timeout 120s "$directory/$variant" > "$directory/$variant.trace" ||
            fail "VDP pixels $variant failed or timed out: $directory"
        [ "$(wc -l < "$directory/$variant.trace")" -eq 2 ] ||
            fail "VDP pixels $variant produced an incomplete trace: $directory"
    done
    cmp "$directory/reference.trace" "$directory/ported.trace" ||
        fail "VDP pixel traces differ: $directory"
    if cmp -s "$directory/reference.trace" "$directory/negative.trace"; then
        fail "VDP pixel comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original and ported eight-pixel composition digests match: $directory"
}

check_vdp_video() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/vdp-video-check.XXXXXXXX")
    python3 "$work/core/tools/extract_vdp_video_reference.py" \
        "$source/Gens.cpp" "$directory/reference.cpp"
    for architecture in reference32 reference64; do
        if [ "$architecture" = reference32 ]; then
            bits='-m32 -mstackrealign'
        else
            bits='-m64 -mcmodel=large -mno-red-zone'
        fi
        g++ $bits -std=c++11 -O2 -Wall -Wextra -Werror -fno-pie \
            -c "$directory/reference.cpp" -o "$directory/$architecture.o"
        gcc $bits -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie \
            -DGENS_VDP_VIDEO_REFERENCE -I"$work/core/waterbox" \
            -c "$work/core/tests/test_gens_vdp_video.c" -o "$directory/$architecture-fixture.o"
        g++ $bits -fno-pie -no-pie "$directory/$architecture.o" \
            "$directory/$architecture-fixture.o" -o "$directory/$architecture"
    done
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_VDP_VIDEO_NEGATIVE_CONTROL
        fi
        gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
            -mcmodel=large -mno-red-zone -msse2 -mfpmath=sse -ffp-contract=off \
            -fexcess-precision=standard $define -I"$work/core/waterbox" \
            "$work/core/waterbox/gens_vdp_video.c" "$work/core/waterbox/gens_color_lookup.c" \
            "$work/core/tests/test_gens_vdp_video.c" -o "$directory/$variant"
    done
    for variant in reference32 reference64 ported negative; do
        LC_ALL=C timeout 120s "$directory/$variant" > "$directory/$variant.trace" ||
            fail "VDP video $variant failed or timed out: $directory"
        [ "$(wc -l < "$directory/$variant.trace")" -eq 1 ] ||
            fail "VDP video $variant produced an incomplete trace: $directory"
    done
    cmp "$directory/reference32.trace" "$directory/reference64.trace" ||
        fail "VDP video source references differ across architectures: $directory"
    cmp "$directory/reference32.trace" "$directory/ported.trace" ||
        fail "VDP video traces differ: $directory"
    if cmp -s "$directory/reference32.trace" "$directory/negative.trace"; then
        fail "VDP video comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original-source and candidate Genesis video conversion digests match: $directory"
}

check_color_lookup() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/color-lookup-check.XXXXXXXX")
    python3 "$work/core/tools/extract_color_lookup_reference.py" \
        "$source/Gens.cpp" "$directory/reference.cpp"
    for architecture in reference32 reference64; do
        if [ "$architecture" = reference32 ]; then
            bits='-m32 -mstackrealign'
        else
            bits='-m64 -mcmodel=large -mno-red-zone'
        fi
        g++ $bits -std=c++11 -O2 -Wall -Wextra -Werror -fno-pie \
            -msse2 -mfpmath=sse -ffp-contract=off -c "$directory/reference.cpp" \
            -o "$directory/$architecture.o"
        gcc $bits -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie \
            -msse2 -mfpmath=sse -ffp-contract=off -fexcess-precision=standard \
            -DGENS_COLOR_LOOKUP_REFERENCE -I"$work/core/waterbox" \
            -c "$work/core/tests/test_gens_color_lookup.c" -o "$directory/$architecture-fixture.o"
        g++ $bits -fno-pie -no-pie "$directory/$architecture.o" \
            "$directory/$architecture-fixture.o" -o "$directory/$architecture"
    done
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_COLOR_LOOKUP_NEGATIVE_CONTROL
        fi
        gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
            -mcmodel=large -mno-red-zone -msse2 -mfpmath=sse -ffp-contract=off \
            -fexcess-precision=standard $define -I"$work/core/waterbox" \
            "$work/core/waterbox/gens_color_lookup.c" "$work/core/waterbox/gens_vdp_palette.c" \
            "$work/core/tests/test_gens_color_lookup.c" -o "$directory/$variant"
    done
    for variant in reference32 reference64 ported negative; do
        LC_ALL=C timeout 120s "$directory/$variant" > "$directory/$variant.trace" ||
            fail "Color lookup $variant failed or timed out: $directory"
        [ "$(wc -l < "$directory/$variant.trace")" -eq 320 ] ||
            fail "Color lookup $variant produced an incomplete trace: $directory"
    done
    cmp "$directory/reference32.trace" "$directory/reference64.trace" ||
        fail "Color lookup source references differ across architectures: $directory"
    cmp "$directory/reference32.trace" "$directory/ported.trace" ||
        fail "Color lookup traces differ: $directory"
    if cmp -s "$directory/reference32.trace" "$directory/negative.trace"; then
        fail "Color lookup comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original-source and candidate Genesis color lookup digests match: $directory"
}

check_vdp_palette() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/vdp-palette-check.XXXXXXXX")
    python3 "$work/core/tools/extract_vdp_palette_reference.py" \
        "$source/vdp_rend.asm" "$directory/reference.asm"
    nasm -f elf32 -I"$source/" "$directory/reference.asm" -o "$directory/reference.o"
    gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mstackrealign -fno-omit-frame-pointer -DGENS_VDP_PALETTE_REFERENCE \
        -I"$work/core/waterbox" "$work/core/tests/test_gens_vdp_palette.c" \
        "$directory/reference.o" -Wl,-z,noexecstack -o "$directory/reference"
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_VDP_PALETTE_NEGATIVE_CONTROL
        fi
        gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
            -mcmodel=large -mno-red-zone $define -I"$work/core/waterbox" \
            "$work/core/waterbox/gens_vdp_palette.c" \
            "$work/core/tests/test_gens_vdp_palette.c" -o "$directory/$variant"
    done
    for variant in reference ported negative; do
        LC_ALL=C timeout 120s "$directory/$variant" > "$directory/$variant.trace" ||
            fail "VDP palette $variant failed or timed out: $directory"
        [ "$(wc -l < "$directory/$variant.trace")" -eq 1 ] ||
            fail "VDP palette $variant produced an incomplete trace: $directory"
    done
    cmp "$directory/reference.trace" "$directory/ported.trace" ||
        fail "VDP palette traces differ: $directory"
    if cmp -s "$directory/reference.trace" "$directory/negative.trace"; then
        fail "VDP palette comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original and ported VDP palette trace digests match: $directory"
}

check_vdp_sprite_line() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/vdp-sprite-line-check.XXXXXXXX")
    python3 "$work/core/tools/extract_vdp_sprite_line_reference.py" \
        "$source/vdp_rend.asm" "$directory/reference.asm"
    nasm -f elf32 -I"$source/" "$directory/reference.asm" -o "$directory/reference.o"
    gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mstackrealign -fno-omit-frame-pointer -DGENS_VDP_SPRITE_LINE_REFERENCE \
        -I"$work/core/waterbox" "$work/core/tests/test_gens_vdp_sprite_line.c" \
        "$directory/reference.o" -Wl,-z,noexecstack -o "$directory/reference"
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_VDP_SPRITE_LINE_NEGATIVE_CONTROL
        fi
        gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
            -mcmodel=large -mno-red-zone $define -I"$work/core/waterbox" \
            "$work/core/waterbox/gens_vdp_sprites.c" \
            "$work/core/tests/test_gens_vdp_sprite_line.c" -o "$directory/$variant"
    done
    for variant in reference ported negative; do
        LC_ALL=C timeout 120s "$directory/$variant" > "$directory/$variant.trace" ||
            fail "VDP sprite line $variant failed or timed out: $directory"
        [ "$(wc -l < "$directory/$variant.trace")" -eq 1 ] ||
            fail "VDP sprite line $variant produced an incomplete trace: $directory"
    done
    cmp "$directory/reference.trace" "$directory/ported.trace" ||
        fail "VDP sprite-line traces differ: $directory"
    if cmp -s "$directory/reference.trace" "$directory/negative.trace"; then
        fail "VDP sprite-line comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original and ported scanline sprite selection digests match: $directory"
}

check_vdp_sprites() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/vdp-sprites-check.XXXXXXXX")
    python3 "$work/core/tools/extract_vdp_sprites_reference.py" \
        "$source/vdp_rend.asm" "$directory/reference.asm"
    nasm -f elf32 -I"$source/" "$directory/reference.asm" -o "$directory/reference.o"
    gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mstackrealign -fno-omit-frame-pointer -DGENS_VDP_SPRITES_REFERENCE \
        -I"$work/core/waterbox" "$work/core/tests/test_gens_vdp_sprites.c" \
        "$directory/reference.o" -Wl,-z,noexecstack -o "$directory/reference"
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_VDP_SPRITES_NEGATIVE_CONTROL
        fi
        gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
            -mcmodel=large -mno-red-zone $define -I"$work/core/waterbox" \
            "$work/core/waterbox/gens_vdp_sprites.c" \
            "$work/core/tests/test_gens_vdp_sprites.c" -o "$directory/$variant"
    done
    for variant in reference ported negative; do
        LC_ALL=C timeout 120s "$directory/$variant" > "$directory/$variant.trace" ||
            fail "VDP sprites $variant failed or timed out: $directory"
        [ "$(wc -l < "$directory/$variant.trace")" -eq 1 ] ||
            fail "VDP sprites $variant produced an incomplete trace: $directory"
    done
    cmp "$directory/reference.trace" "$directory/ported.trace" ||
        fail "VDP sprite traces differ: $directory"
    if cmp -s "$directory/reference.trace" "$directory/negative.trace"; then
        fail "VDP sprite comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original and ported Genesis sprite preparation trace digests match: $directory"
}

check_vdp_reset() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/vdp-reset-check.XXXXXXXX")
    python3 "$work/core/tools/extract_vdp_reset_reference.py" \
        "$source/vdp_io.asm" "$directory/reference.asm"
    nasm -f elf32 -I"$source/" "$directory/reference.asm" -o "$directory/reference.o"
    gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mstackrealign -fno-omit-frame-pointer -DGENS_VDP_RESET_REFERENCE \
        -I"$work/core/waterbox" "$work/core/tests/test_gens_vdp_reset.c" \
        "$directory/reference.o" -Wl,-z,noexecstack -o "$directory/reference"
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_VDP_RESET_NEGATIVE_CONTROL
        fi
        gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
            -mcmodel=large -mno-red-zone $define -I"$work/core/waterbox" \
            "$work/core/waterbox/gens_vdp.c" "$work/core/waterbox/gens_vdp_read.c" \
            "$work/core/waterbox/gens_vdp_write.c" "$work/core/waterbox/gens_vdp_registers.c" \
            "$work/core/waterbox/gens_vdp_dma.c" "$work/core/waterbox/gens_vdp_memory_dma.c" \
            "$work/core/waterbox/gens_vdp_irq.c" "$work/core/waterbox/gens_vdp_reset.c" \
            "$work/core/tests/test_gens_vdp_reset.c" -o "$directory/$variant"
    done
    for variant in reference ported negative; do
        LC_ALL=C timeout 120s "$directory/$variant" > "$directory/$variant.trace" ||
            fail "VDP reset $variant failed or timed out: $directory"
        [ "$(wc -l < "$directory/$variant.trace")" -eq 1 ] ||
            fail "VDP reset $variant produced an incomplete trace: $directory"
    done
    cmp "$directory/reference.trace" "$directory/ported.trace" ||
        fail "VDP reset traces differ: $directory"
    if cmp -s "$directory/reference.trace" "$directory/negative.trace"; then
        fail "VDP reset comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original and ported VDP reset trace digests match: $directory"
}

check_vdp_irq() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/vdp-irq-check.XXXXXXXX")
    python3 "$work/core/tools/extract_vdp_irq_reference.py" \
        "$source/vdp_io.asm" "$directory/reference.asm"
    nasm -f elf32 -I"$source/" "$directory/reference.asm" -o "$directory/reference.o"
    gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mstackrealign -fno-omit-frame-pointer -DGENS_VDP_IRQ_REFERENCE \
        -I"$work/core/waterbox" "$work/core/tests/test_gens_vdp_irq.c" \
        "$directory/reference.o" -Wl,-z,noexecstack -o "$directory/reference"
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_VDP_IRQ_NEGATIVE_CONTROL
        fi
        gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
            -mcmodel=large -mno-red-zone $define -I"$work/core/waterbox" \
            "$work/core/waterbox/gens_vdp_irq.c" \
            "$work/core/tests/test_gens_vdp_irq.c" -o "$directory/$variant"
    done
    for variant in reference ported negative; do
        LC_ALL=C timeout 120s "$directory/$variant" > "$directory/$variant.trace" ||
            fail "VDP IRQ $variant failed or timed out: $directory"
        [ "$(wc -l < "$directory/$variant.trace")" -eq 2 ] ||
            fail "VDP IRQ $variant produced an incomplete trace: $directory"
    done
    cmp "$directory/reference.trace" "$directory/ported.trace" ||
        fail "VDP IRQ traces differ: $directory"
    if cmp -s "$directory/reference.trace" "$directory/negative.trace"; then
        fail "VDP IRQ comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original and ported VDP IRQ trace digests match: $directory"
}

check_vdp_memory_dma() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/vdp-memory-dma-check.XXXXXXXX")
    python3 "$work/core/tools/extract_vdp_memory_dma_reference.py" \
        "$source/vdp_io.asm" "$directory/reference.asm"
    nasm -f elf32 -I"$source/" "$directory/reference.asm" -o "$directory/reference.o"
    gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mstackrealign -fno-omit-frame-pointer -DGENS_VDP_MEMORY_DMA_REFERENCE \
        -I"$work/core/waterbox" "$work/core/tests/test_gens_vdp_memory_dma.c" \
        "$directory/reference.o" -Wl,-z,noexecstack -o "$directory/reference"
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_VDP_MEMORY_DMA_NEGATIVE_CONTROL
        fi
        gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
            -mcmodel=large -mno-red-zone $define -I"$work/core/waterbox" \
            "$work/core/waterbox/gens_vdp_write.c" "$work/core/waterbox/gens_vdp_dma.c" \
            "$work/core/waterbox/gens_vdp_memory_dma.c" \
            "$work/core/tests/test_gens_vdp_memory_dma.c" -o "$directory/$variant"
    done
    for variant in reference ported negative; do
        LC_ALL=C timeout 120s "$directory/$variant" > "$directory/$variant.trace" ||
            fail "VDP memory DMA $variant failed or timed out: $directory"
        [ "$(wc -l < "$directory/$variant.trace")" -eq 1 ] ||
            fail "VDP memory DMA $variant produced an incomplete trace: $directory"
    done
    cmp "$directory/reference.trace" "$directory/ported.trace" ||
        fail "VDP memory DMA traces differ: $directory"
    if cmp -s "$directory/reference.trace" "$directory/negative.trace"; then
        fail "VDP memory DMA comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original and ported Genesis memory DMA trace digests match: $directory"
}

check_vdp_dma() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/vdp-dma-check.XXXXXXXX")
    python3 "$work/core/tools/extract_vdp_dma_reference.py" \
        "$source/vdp_io.asm" "$directory/reference.asm"
    nasm -f elf32 -I"$source/" "$directory/reference.asm" -o "$directory/reference.o"
    gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mstackrealign -fno-omit-frame-pointer -DGENS_VDP_DMA_REFERENCE \
        -I"$work/core/waterbox" "$work/core/tests/test_gens_vdp_dma.c" \
        "$directory/reference.o" -Wl,-z,noexecstack -o "$directory/reference"
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_VDP_DMA_NEGATIVE_CONTROL
        fi
        gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
            -mcmodel=large -mno-red-zone $define -I"$work/core/waterbox" \
            "$work/core/waterbox/gens_vdp_dma.c" \
            "$work/core/tests/test_gens_vdp_dma.c" -o "$directory/$variant"
    done
    for variant in reference ported negative; do
        LC_ALL=C timeout 120s "$directory/$variant" > "$directory/$variant.trace" ||
            fail "VDP DMA timing $variant failed or timed out: $directory"
        [ "$(wc -l < "$directory/$variant.trace")" -eq 2 ] ||
            fail "VDP DMA timing $variant produced an incomplete trace: $directory"
    done
    cmp "$directory/reference.trace" "$directory/ported.trace" ||
        fail "VDP DMA timing traces differ: $directory"
    if cmp -s "$directory/reference.trace" "$directory/negative.trace"; then
        fail "VDP DMA timing comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original and ported VDP DMA timing trace digests match: $directory"
}

check_vdp_registers() {
    source="$work/core/extern/gens-rerecording/Gens-rr/src"
    directory=$(mktemp -d "$work/vdp-register-check.XXXXXXXX")
    gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG \
        -fno-pie -no-pie -mcmodel=large -mno-red-zone \
        -I"$work/core/waterbox" "$work/core/waterbox/gens_vdp_write.c" \
        "$work/core/waterbox/gens_vdp_registers.c" \
        "$work/core/tests/test_gens_vdp_registers.c" -o "$directory/test-vdp-registers"
    "$directory/test-vdp-registers"
    python3 "$work/core/tools/extract_vdp_register_reference.py" \
        "$source/vdp_io.asm" "$directory/reference.asm"
    nasm -f elf32 -I"$source/" "$directory/reference.asm" -o "$directory/reference.o"
    gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
        -mstackrealign -fno-omit-frame-pointer -DGENS_VDP_REGISTER_REFERENCE \
        -I"$work/core/waterbox" "$work/core/tests/test_gens_vdp_register_trace.c" \
        "$directory/reference.o" -Wl,-z,noexecstack -o "$directory/reference"
    for variant in ported negative; do
        define=
        if [ "$variant" = negative ]; then
            define=-DGENS_VDP_REGISTER_NEGATIVE_CONTROL
        fi
        gcc -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -fno-pie -no-pie \
            -mcmodel=large -mno-red-zone $define -I"$work/core/waterbox" \
            "$work/core/waterbox/gens_vdp_registers.c" \
            "$work/core/tests/test_gens_vdp_register_trace.c" -o "$directory/$variant"
    done
    for variant in reference ported negative; do
        LC_ALL=C timeout 120s "$directory/$variant" > "$directory/$variant.trace" ||
            fail "VDP register $variant failed or timed out: $directory"
        [ "$(wc -l < "$directory/$variant.trace")" -eq 2 ] ||
            fail "VDP register $variant produced an incomplete trace: $directory"
    done
    cmp "$directory/reference.trace" "$directory/ported.trace" ||
        fail "VDP register traces differ: $directory"
    if cmp -s "$directory/reference.trace" "$directory/negative.trace"; then
        fail "VDP register comparison did not detect the negative control: $directory"
    fi
    printf '%s\n' "Original and ported VDP register trace digests match: $directory"
}

case "${1:-}" in
    prepare) prepare ;;
    minibox) check_prepared; build_minibox ;;
    input-check) check_prepared; check_input ;;
    rom-check) check_prepared; check_rom ;;
    frame-check) check_prepared; check_frame ;;
    audio-check) check_prepared; check_audio ;;
    m68k-check) check_prepared; check_m68k ;;
    fetch-check) check_prepared; check_fetch ;;
    bus-check) check_prepared; check_bus ;;
    bridge-check) check_prepared; check_bridge ;;
    controller-check) check_prepared; check_controller ;;
    genesis-bus-check) check_prepared; check_genesis_bus ;;
    genesis-bus-reference-check) check_prepared; check_genesis_bus_reference ;;
    z80-control-check) check_prepared; check_z80_control ;;
    z80-memory-check) check_prepared; check_z80_memory ;;
    z80-native-check) check_prepared; check_z80_native ;;
    sound-chip-check) check_prepared; check_sound_chips ;;
    pcm-check) check_prepared; check_pcm ;;
    vdp-read-check) check_prepared; check_vdp_read ;;
    vdp-write-check) check_prepared; check_vdp_write ;;
    vdp-dma-check) check_prepared; check_vdp_dma ;;
    vdp-memory-dma-check) check_prepared; check_vdp_memory_dma ;;
    vdp-integration-check) check_prepared; check_vdp_integration ;;
    vdp-irq-check) check_prepared; check_vdp_irq ;;
    vdp-reset-check) check_prepared; check_vdp_reset ;;
    vdp-sprites-check) check_prepared; check_vdp_sprites ;;
    vdp-sprite-line-check) check_prepared; check_vdp_sprite_line ;;
    vdp-palette-check) check_prepared; check_vdp_palette ;;
    color-lookup-check) check_prepared; check_color_lookup ;;
    vdp-video-check) check_prepared; check_vdp_video ;;
    vdp-pixels-check) check_prepared; check_vdp_pixels ;;
    vdp-sprite-draw-check) check_prepared; check_vdp_sprite_draw ;;
    vdp-scroll-check) check_prepared; check_vdp_scroll ;;
    vdp-render-check) check_prepared; check_vdp_render ;;
    vdp-machine-check) check_prepared; check_vdp_machine ;;
    z80-cpu-check) check_prepared; check_z80_cpu ;;
    m68k-cpu-check) check_prepared; check_m68k_cpu ;;
    machine-check) check_prepared; check_machine ;;
    vdp-register-check) check_prepared; check_vdp_registers ;;
    core) check_prepared; build_minibox; build_core ;;
    all)
        check_prepared
        [ -f "$work/core/container-build.sh" ] ||
            fail "Gens core build is not implemented. Run minibox individually."
        printf '\n%s\n' "=== Component and synthetic tests ===" >&2
        check_input
        check_rom
        check_frame
        check_audio
        check_m68k
        check_fetch
        check_bus
        check_bridge
        check_controller
        check_genesis_bus
        check_genesis_bus_reference
        check_z80_control
        check_z80_memory
        check_z80_native
        check_sound_chips
        check_pcm
        check_vdp_read
        check_vdp_write
        check_vdp_dma
        check_vdp_memory_dma
        check_vdp_integration
        check_vdp_irq
        check_vdp_reset
        check_vdp_sprites
        check_vdp_sprite_line
        check_vdp_palette
        check_color_lookup
        check_vdp_video
        check_vdp_pixels
        check_vdp_sprite_draw
        check_vdp_registers
        check_z80_cpu
        check_m68k_cpu
        check_machine
        printf '\n%s\n' "=== Building miniBox ===" >&2
        build_minibox
        printf '\n%s\n' "=== Building Gens core package ===" >&2
        build_core
        ;;
    *) fail "expected prepare, minibox, input-check, rom-check, frame-check, audio-check, m68k-check, fetch-check, bus-check, bridge-check, genesis-bus-check, controller-check, genesis-bus-reference-check, z80-control-check, z80-memory-check, z80-native-check, sound-chip-check, pcm-check, vdp-read-check, vdp-write-check, vdp-dma-check, vdp-memory-dma-check, vdp-integration-check, vdp-irq-check, vdp-reset-check, vdp-sprites-check, vdp-sprite-line-check, vdp-palette-check, color-lookup-check, vdp-video-check, vdp-pixels-check, vdp-sprite-draw-check, vdp-scroll-check, vdp-render-check, vdp-machine-check, z80-cpu-check, m68k-cpu-check, machine-check, vdp-register-check, core, or all" ;;
esac
