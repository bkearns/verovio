# Music Stand Verovio Upstream Policy

This repository is a narrow downstream fork of Verovio for first-class JSON
Score Model (JSM) input. JSM changes must remain separable from upstream
engraving changes so that release updates can be audited and tested.

## Initial Baseline

- Upstream repository: <https://github.com/rism-digital/verovio>
- Upstream release: `version-6.2.0`
- Peeled release commit: `43f806031bfff2c64003fc8ddd9910820445f6ab`
- Baseline date: 2026-07-10
- C++ standard: C++20

The `origin` remote is the Music Stand fork and `upstream` is the official
Verovio repository. The initial JSM branch starts directly at the peeled
release commit, not at either repository's moving development branch.

## JSM Schema Target

The initial adapter targets draft JSM `0.1.0` from the Music Stand repository,
under `specs/jsm/`. At bootstrap, the authoritative draft files have these
SHA-256 hashes:

| File | SHA-256 |
|---|---|
| `jsm-canonical.schema.json` | `93151584ba59ccdb225b03460f3922268c58d7303b5485816530d8820bd966b1` |
| `jsm-compact.schema.json` | `434fa8973e769047cd947749d9832be2c198cf00524d04834a76b0a464530554` |

These hashes identify an unshipped draft, not a released schema artifact. Do
not implement or release the adapter until the schemas and canonical/compact
converter are committed and versioned. Any synchronized schema copy in this
fork must record both the JSM version and source commit in its generated
manifest.

## Updating From Upstream

1. Fetch signed tags from `upstream` and choose a published release tag.
2. Record the tag, peeled commit, release date, and release notes in this file.
3. Create a dedicated `chore/upstream-<version>` branch.
4. Merge the release tag without mixing JSM feature work into the sync commit.
5. Run the unchanged upstream build and regression suites first.
6. Run the JSM semantic, rendering, identity, geometry, and binding suites.
7. Review changes to toolkit APIs, input dispatch, SVG IDs, bounding boxes,
   resources, and every distributed binding before merging.

Review published upstream releases quarterly and apply security or critical
correctness releases promptly. Do not track the upstream `develop` branch in
production.

## Test Policy

Every downstream pull request must preserve the unmodified upstream gates and
run downstream tests after them. Required evidence is:

- C++20 CLI and shared-library builds on Linux and macOS.
- The upstream MEI and MusicXML regression suites, pinned to an explicit
  `verovio.org` test-corpus commit rather than a moving branch.
- Python, Java, Swift, and JavaScript/Wasm binding builds and fixture smoke
  tests when a public toolkit API changes.
- JSM canonical/compact input parity, strict capability diagnostics, stable
  SVG identity, layout modes, structured bounding-box maps, and MusicXML
  semantic rendering parity.

The upstream regression data is stored outside this repository and upstream's
workflow also contains publication/VPN steps. This fork must pin the external
corpus and omit only publication infrastructure; it must not weaken or rewrite
the actual regression comparisons.

## Bootstrap Verification

The unmodified `version-6.2.0` source was verified on 2026-07-10 using Ubuntu
24.04, GCC 13.3.0, CMake 4.4.0, and Ninja 1.13.0. Build output was kept outside
the repository.

| Gate | Result |
|---|---|
| Default C++20 CLI configure and build | Pass; all 299 Ninja steps completed |
| CLI version | Pass; `Verovio 6.2.0-43f8060` |
| MusicXML-to-SVG smoke render | Pass; Music Stand's `del-ray-pristine.musicxml` produced a 308,704-byte SVG |
| CTest | No tests registered by the upstream CMake project |
| Upstream visual and MusicXML suites | Not run; fixtures live in the external `verovio.org` gh-pages repository and require a built Python binding plus Python image/diff dependencies |
| Python binding | Not built; SWIG is unavailable on the bootstrap host |
| Java binding | Not built; SWIG and Maven are unavailable on the bootstrap host (Java is present) |
| Swift binding | Not built; the Swift toolchain requires macOS/Xcode for the supported Apple targets and is unavailable on this Linux host |
| JavaScript/Wasm binding | Not built; Emscripten is unavailable on the bootstrap host |

The GCC build emitted upstream warnings in Humdrum articulation code from
`iohumdrum.cpp`/generated attribute setters (`-Warray-bounds`) and noted that
GCC does not recognize `-Wno-dollar-in-identifier-extension`; neither warning
failed the upstream build. Treat warning changes as baseline drift when syncing
future releases.

## Licensing

Verovio remains licensed under the GNU Lesser General Public License. Keep
`COPYING`, `COPYING.LESSER`, copyright notices, and third-party notices intact.
Downstream binary distributions must satisfy the LGPL relinking and
corresponding-source requirements appropriate to their linkage and platform.
JSM-specific code must carry compatible notices and must not modify generated
LibMEI code solely to transport JSM metadata.
