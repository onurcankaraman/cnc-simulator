# Third-party notices

The shipped runtime dynamically links the libraries below. This inventory and the bundled license texts identify dependencies; they are not a legal certification of distribution compliance.

| Component | Pinned source / version | License |
|---|---|---|
| Snap7 | SCADACS/snap7 commit `f6ff90317ca5d54250f4dcd29209689a74e26d82` | LGPL-3.0; GPL-3.0 text also bundled |
| open62541 | v1.5.0, `b90fbfac5ef484089447bf5e3fe6ad7baaaebada` | MPL-2.0; selected examples CC0 |
| libmosquitto | Debian package version in SBOM | EPL-2.0 / EDL-1.0 |
| Mosquitto broker | 2.1.2, image digest `sha256:6f8d8a947c506f8a2290ec65cd4bd2bc7cb4d43fb5f6271f861cb013e2ef9797` | EPL-2.0 / EDL-1.0 |
| PostgreSQL libpq | Debian libpq build/runtime package | PostgreSQL License |
| PgBouncer | Debian 1.18.0-1+deb12u1; optional in-image SQL proxy | ISC |
| TimescaleDB | Image digest pinned in compose.yaml | Apache-2.0 and Timescale License; see upstream image licenses |
| yaml-cpp | Debian package version in SBOM | MIT |
| nlohmann/json | Debian package version in SBOM | MIT |
| OpenSSL | Debian package version in SBOM | Apache-2.0 |
| Debian bookworm | image digest in Dockerfile | Component-specific |

`docs/SBOM-dependencies.tsv` and `docs/SBOM-runtime.tsv` are actual `dpkg-query -W` inventories. Debian copyright/license files remain in `/usr/share/doc/*/copyright` in the image. `LICENSES/` includes upstream Snap7 and open62541 texts. No modifications were made to their source. The Dockerfile fetches the exact source commits and builds shared libraries; rebuild/relink instructions are reproducible there. When distributing binaries, provide the corresponding source and required notices under the relevant terms, including LGPL replacement/relinking rights. Snap7 is not Siemens firmware.
