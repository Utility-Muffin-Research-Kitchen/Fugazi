# Fugazi (Leaf)

<p align="center">
  <a href="LICENSE"><img alt="license" src="https://img.shields.io/github/license/Utility-Muffin-Research-Kitchen/Fugazi?color=7FB069&labelColor=0F160E&cacheSeconds=3600"></a>
  <img alt="last commit" src="https://img.shields.io/github/last-commit/Utility-Muffin-Research-Kitchen/Fugazi?color=7FB069&labelColor=0F160E&cacheSeconds=3600">
  <img alt="platform" src="https://img.shields.io/badge/platform-Miniloong%20Pocket%201-7FB069?labelColor=0F160E&cacheSeconds=3600">
</p>

Live CRT shader tweaker for Leaf / Miniloong Pocket 1, built natively on
Catastrophe (the Leaf UI toolkit) + OpenGL ES 2.0.

Adjust curvature, glow, scanlines, gap, phosphor mask, vignette, brightness and
warmth with a full-screen live preview rendered through the shader. The CRT
effect model is shared with the original NextUI Fugazi; the app shell, UI and
packaging are Leaf-native.

## Build (MLP1)

    ./scripts/build-mlp1.sh   # cross-compiles in the mlp1-toolchain container

Produces `ports/mlp1/pak/bin/fugazi`. Requires sibling `Catastrophe/` and
`Jawaka/` (for cJSON) checkouts in the workspace.

## Status

- Phase 1: live tuner + preview (this).
- Phase 2: install to in-game RetroArch shader preset.
