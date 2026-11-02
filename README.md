# Chimera Gens core

This repository contains the Linux container build files at its root and the
emulator core under `core/`. The required `chimera-common-minibox` repository
sits beside this repository in the parent workspace.

The upstream Gens-Rerecording submodule is `core/extern/gens-rerecording`,
pinned to [b948995d15034983cbf5562610586d93e0b25a9b](https://github.com/TASEmulators/gens-rerecording/commit/b948995d15034983cbf5562610586d93e0b25a9b).
This is the revision taken for porting. Its correspondence to SVN r341 remains
unverified.

From this repository's root:

```sh
git submodule update --init
export CHIMERA_UID="$(id -u)" CHIMERA_GID="$(id -g)"
docker compose build
docker compose run --rm init
docker compose run --rm prepare
docker compose run --rm compile core
```

`compile core` builds miniBox and the core package.
`compile all` additionally runs the comparison checks.
Neither builds the Chimera application or Qt. Output is written to
`build/artifacts/gens.chimeraCore`. An existing Chimera installation is needed
only to load the package, not to build it.

See [the core overview](core/README.md) and
[the build instructions](core/docs/BUILDING.md) for prerequisites, validation
limits and licensing restrictions.
