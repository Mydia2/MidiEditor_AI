# Playground - MidiEditor AI

Experimental work that is merged on `main` but not part of any official release:
source-only, community-maintained, no prebuilt binaries and no auto-updates.
The official [CHANGELOG](CHANGELOG.md) documents releases only; this file keeps
every experimental merge documented until an entry graduates into a release
section of the CHANGELOG.

Newest entries first, one section per merge (entry template at the end of this
file). Status values: `experimental` -> `graduated to X.Y.Z` (entry moves into
that CHANGELOG release section) or `retired`.

---

## [PR #14] - 2026-07-19 - macOS app icon and bundle metadata

**Status:** experimental · **PR:** [#14](https://github.com/happytunesai/MidiEditor_AI/pull/14)

The official app icon as `MidiEditorAI.icns` (bundled into `Resources/`) plus a
custom `macos/Info.plist.in`: bundle identity, MIDI file-type associations
(`.mid`/`.midi`/`.smf` as Editor role) and the matching UTI declaration, wired
through the `MACOSX_BUNDLE_*` CMake properties. The local `.app` from
`make mac-app` now looks and registers like a native macOS application.

---

## [PR #12] - 2026-07-14 - macOS build-from-source support

**Status:** experimental · **PR:** [#12](https://github.com/happytunesai/MidiEditor_AI/pull/12)

The project builds and runs on macOS: `make mac-setup && make mac-build`
(Homebrew Bundle with Qt 6 and FluidSynth, CMake bundle configuration, optional
local `.app` packaging via `make mac-app`). No prebuilt binaries and no
auto-updates; a build-only macOS CI job guards the platform against
regressions. Data-path migration for any *distributed* macOS build is tracked
in [issue #13](https://github.com/happytunesai/MidiEditor_AI/issues/13).

Note: the FluidSynth audio-driver hardening from the same PR benefits all
platforms and therefore ships as regular release content (see CHANGELOG,
2.1.0).

<!--
Entry template - copy directly below the first "---" (newest first):

## [PR #NN] - YYYY-MM-DD - Title

**Status:** experimental · **PR:** [#NN](https://github.com/happytunesai/MidiEditor_AI/pull/NN)

What it does, how to use it, and what is explicitly out of scope.
Plain ASCII hyphens only (dedash.py does not cover this file).
-->
