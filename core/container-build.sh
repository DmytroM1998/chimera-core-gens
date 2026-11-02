#!/bin/sh
# Builds gens.chimeraCore inside the chimera-build container (chimera-build/driver.sh core).
#
# Environment: MINIBOX_DIR (a miniBox checkout), MINIBOX_BUILD (its built guest toolchain),
# ARTIFACT_DIR (where the package goes). The Gens checkout is under /sources/repository, or
# set GENS_REFERENCE.
set -eu
root="$(cd "$(dirname "$0")" && pwd)"
: "${MINIBOX_DIR:?set MINIBOX_DIR}"
: "${MINIBOX_BUILD:?set MINIBOX_BUILD}"
: "${ARTIFACT_DIR:?set ARTIFACT_DIR}"
reference="${GENS_REFERENCE:-$root/extern/gens-rerecording}"

mkdir -p "$root/build"
rm -rf "$root/build/gens-source" "$root/build/meson-guest" "$root/build/package-staging"
python3 "$root/tools/prepare_patched_source.py" --reference "$reference" \
	--destination "$root/build/gens-source"
MINIBOX_DIR="$MINIBOX_DIR" MINIBOX_BUILD="$MINIBOX_BUILD" sh "$root/waterbox/setup-guest.sh" \
	-m "$MINIBOX_DIR"
CORE_VERSION="${CORE_VERSION:-$(cat "$root/VERSION" 2>/dev/null || echo unversioned)+container}" \
	MINIBOX_DIR="$MINIBOX_DIR" sh "$root/waterbox/build-package.sh" -m "$MINIBOX_DIR" -o "$ARTIFACT_DIR"