# Fugazi (Leaf)

<p align="center">
  <a href="LICENSE"><img alt="license" src="https://img.shields.io/github/license/Utility-Muffin-Research-Kitchen/Fugazi?color=7FB069&labelColor=0F160E&cacheSeconds=3600"></a>
  <img alt="last commit" src="https://img.shields.io/github/last-commit/Utility-Muffin-Research-Kitchen/Fugazi?color=7FB069&labelColor=0F160E&cacheSeconds=3600">
  <img alt="platform" src="https://img.shields.io/badge/platform-Miniloong%20Pocket%201-7FB069?labelColor=0F160E&cacheSeconds=3600">
</p>

Live CRT shader tweaker for Leaf / Miniloong Pocket 1, built natively on
Catastrophe (the Leaf UI toolkit) + OpenGL ES 2.0.

Adjust curvature, glow, scanlines, gap darkness, mask, vignette, brightness and
warmth against a full-screen live preview rendered through the shader, then
install your tuning so it applies automatically in every RetroArch game - no
need to touch RetroArch's shader menus. The CRT effect model is shared with the
original NextUI Fugazi; the app shell, UI and packaging are Leaf-native.

## Controls

- **Up / Down** - select a parameter
- **Left / Right** - adjust the selected parameter
- **X** - toggle between the game image and the test pattern
- **Y** - reset the tuning values (back to no visible effect)
- **A** - apply (bake the current tuning into RetroArch)
- **START** - remove Fugazi, or resolve a conflicting preset state
- **B** - quit

**Reset is not an uninstall.** `Y` only returns the tuning values to their
no-effect defaults; the shader stays applied in RetroArch. Removing Fugazi is
`START`, and it appears in the footer only when there is something to remove.

## How apply works

Apply bakes the eight live values into a two-pass GLSL preset on the SD card
and registers it as RetroArch's **global automatic preset** (`global.glslp`), so
the shader loads for every core on the next game launch. RetroArch does not
auto-load the global `video_shader` config value at boot; the automatic preset
in its config dir is the mechanism that does. The config dir comes from the Leaf
env contract (`UMRK_RETROARCH_CONFIG_DIR`), so no device paths are hardcoded.

Fugazi no longer writes `video_shader` or `video_shader_enable` into
`retroarch.cfg`. Jawaka protects `video_shader_enable` in the launch config
because RetroArch will otherwise skip automatic shader discovery when a saved
value is false. Fugazi owns the automatic preset, not that launch-time gate.

## Ownership: Fugazi will not eat your preset

A global preset is durable user state, so Fugazi acts only on one it recognizes
as its own - decided from the file's **content**, not from the file existing.
The status line above the parameter row always says which of these is true:

| Status | Meaning |
| --- | --- |
| `Not applied` | No global preset. Apply installs one |
| `Applied globally` | Fugazi owns the global preset |
| `Applied globally - previous shader preserved` | Fugazi owns it and is holding one preset it displaced. Remove restores that preset |
| `Another global shader is active` | Someone else's preset. Apply asks before replacing it; Remove refuses outright |
| `State needs attention` | Another preset is active *and* Fugazi still holds a backup. Press START for the resolver |

When Apply replaces a preset Fugazi does not own, it first moves that file to
`global.glslp.fugazi-backup` beside it, and `Remove` puts it back byte-for-byte.
Only one predecessor is kept. If a second replacement would destroy it, Apply
stops and opens a resolver offering exactly **Keep current**, **Restore
previous**, or **Cancel**, each naming the file it discards. Cancel leaves both
files untouched.

Ownership and the atomic install/remove operations are covered by a native
test that needs no device:

    make preset-ownership-test

## Build (MLP1)

    ./scripts/build-mlp1.sh              # cross-compile the binary in the mlp1-toolchain container
    make package-platform PLATFORM=mlp1  # build + assemble the pak

These produce `ports/mlp1/pak/bin/fugazi` and `build/mlp1/package/Fugazi.pak`.
Requires sibling `Catastrophe/` and `Jawaka/` (for cJSON) checkouts in the
workspace.

Fugazi is a first-party Leaf app, so from the `Leaf` repo you can also build and
deploy it directly:

    make stage-app APP=Fugazi DEVICE=mlp1   # build + deploy to a connected device

It is included in the default Leaf release ZIPs and installs to
`Apps/mlp1/Fugazi.pak`.

## Status

Phase 1 (live tuner + preview) and Phase 2 (in-game install + auto-apply) are
complete.
