#!/bin/sh
# Builds the Gens-Rerecording core package, gens.chimeraCore, from the guest build.
#
# A package is core.wbx + waterbox.config + default_keybinds.json + file_slots.json + the
# English text table + the licences, loaded through Chimera's generic adapter.
#
# Usage: ./build-package.sh [-m <miniBox dir>] [-o <output dir>]
# The output directory defaults to this repository's build/artifacts.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
mb="${MINIBOX_DIR:-}"
cores_dir=""
while getopts "m:o:" opt; do
	case "$opt" in
		m) mb="$OPTARG" ;;
		o) cores_dir="$OPTARG" ;;
		*) exit 2 ;;
	esac
done
[ -n "$mb" ] || mb="$root/../../chimera-common-minibox"
mb="$(cd "$mb" && pwd)"
[ -n "$cores_dir" ] || cores_dir="$root/../build/artifacts"
mkdir -p "$cores_dir"
cores_dir="$(cd "$cores_dir" && pwd)"

[ -f "$root/build/gens-source/Gens-rr/src/ym2612.c" ] || {
	echo "the patched Gens source is missing: run tools/prepare_patched_source.py" >&2; exit 1; }
[ -f "$root/build/meson-guest/build.ninja" ] || MINIBOX_DIR="$mb" sh "$here/setup-guest.sh"
ninja -C "$root/build/meson-guest" core.wbx
sh "$mb/source/guest/check-wbx.sh" "$root/build/meson-guest/core.wbx"

pkg=gens
staging="$root/build/package-staging/$pkg"
rm -rf "$staging"
mkdir -p "$staging/languages"
cp "$root/build/meson-guest/core.wbx" "$staging/core.wbx"
cp "$here/waterbox.config" "$here/default_keybinds.json" "$here/file_slots.json" "$staging/"
# English only: Chimera shows a key's English text in every language.
cp "$here/languages/en.json" "$staging/languages/en.json"
python3 "$mb/source/guest/package-licenses.py" "$root" "$staging"

# the version is the commit, and its date the commit's date (never the build's)
core_version="${CORE_VERSION:-}"
if [ -z "$core_version" ]; then
	if commit="$(git -C "$root" rev-parse --short=12 HEAD 2>/dev/null)"; then
		git -C "$root" diff --quiet HEAD 2>/dev/null || commit="$commit-dirty"
		core_version="$commit+local"
	else
		core_version="unversioned+local"
	fi
fi
core_version_date="$(TZ=UTC git -C "$root" log -1 --date=format-local:%Y-%m-%dT%H:%M:%SZ --format=%cd HEAD 2>/dev/null || true)"
python3 - "$staging/waterbox.config" "$core_version" "$core_version_date" <<'PYVER'
import json, sys
path, version, date = sys.argv[1], sys.argv[2], sys.argv[3]
with open(path) as f:
    cfg = json.load(f)
cfg["version"] = version
if date:
    cfg["versionDate"] = date
with open(path, "w") as f:
    json.dump(cfg, f, indent=2)
    f.write("\n")
PYVER

python3 - "$staging/build.json" "$core_version" "$(gcc -dumpfullversion)" \
	"$(cat "$mb/extern/musl/VERSION" 2>/dev/null || echo unknown)" \
	"$(git -C "$mb" rev-parse --short=12 HEAD 2>/dev/null || echo unknown)" <<'PYPROV'
import json, sys
path, version, gcc, musl, kit = sys.argv[1:6]
json.dump({
    "version": version,
    "toolchain": {"compiler": "gcc " + gcc, "target": "x86_64-linux-musl", "musl": musl},
    "guestKit": {"name": "miniBox", "commit": kit},
    "upstream": {"name": "Gens-Rerecording", "pin": "b948995d15034983cbf5562610586d93e0b25a9b"},
}, open(path, "w"), indent=2, sort_keys=True)
PYPROV

# deterministic packaging: sorted entries, fixed time and permissions. The package's SHA1 is
# the core's identity, which movies cite.
zip_path="$cores_dir/$pkg.chimeraCore"
rm -f "$zip_path"
python3 - "$staging" "$zip_path" <<'PYEOF'
import hashlib, os, sys, tempfile, zipfile

staging, zip_path = sys.argv[1], sys.argv[2]
FIXED_DATE = (1980, 1, 1, 0, 0, 0)

def write_package(path):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for root, dirs, files in os.walk(staging):
            dirs.sort()
            for name in sorted(files):
                full = os.path.join(root, name)
                info = zipfile.ZipInfo(os.path.relpath(full, staging), date_time=FIXED_DATE)
                info.compress_type = zipfile.ZIP_DEFLATED
                info.create_system = 3
                info.external_attr = 0o644 << 16
                with open(full, "rb") as f:
                    z.writestr(info, f.read())

write_package(zip_path)
with tempfile.NamedTemporaryFile(suffix=".zip") as tmp:
    write_package(tmp.name)
    again = hashlib.sha1(open(tmp.name, "rb").read()).hexdigest()
first = hashlib.sha1(open(zip_path, "rb").read()).hexdigest()
if first != again:
    sys.exit(f"packaging is not deterministic: {first} then {again}")
print(f"gens sha1 {first}")
PYEOF
echo "packaged -> $zip_path"