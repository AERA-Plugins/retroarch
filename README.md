# AERA RetroArch Plugin

RetroArch 1.22.2 adapted as a signed, optional AERA Recovery Project app.

The plugin keeps RetroArch outside `recovery.img`. AERA verifies its Ed25519
manifest and payload hashes, expands it into RAM on demand, and launches it as
an unprivileged process. Frames cross a sealed double-buffered shared-memory
channel; touch events cross a bounded packet channel; stereo PCM uses AERA's
protected device audio bridge. RetroArch never owns the recovery framebuffer or
raw input devices.

The runtime uses RetroArch's phone-oriented GLUI/MaterialUI menu, phone touch
controls, Gambatte for Game Boy and Game Boy Color software, and a public-domain
2048 libretro core for installation testing. AERA supplies a compact software
menu-display and overlay compositor, so GLUI does not require an OpenGL stack in
the recovery jail. An AERA pointer driver handles touch while an inert AERA joypad driver keeps
RetroArch's menu input probes valid on a device with no exposed gamepad nodes.
No copyrighted ROM or BIOS content is included. Game Boy BIOS files are optional.
User content is exposed
read-only under `/storage` inside the jail.

GLUI is both the shipped configuration and the compiled AERA default, so a
missing or regenerated configuration cannot silently fall back to RGUI. The
AERA video driver also handles RGUI's late first texture submission. This
prevents RetroArch's dummy startup frame from leaving the recovery viewport
black while the otherwise idle menu waits for another core frame.

Video setup explicitly declines RetroArch's video-coupled input shortcut so
the normal AERA input initialization also installs its joypad shim. This keeps
the menu's unconditional analog scan valid while touch remains the real input
source.

## Versions and source

- RetroArch v1.22.2, commit `69a4f0ea1e8aaf442ae4858f2e7f2b31a1776576`
- libretro-2048 commit `39333f7b13dc4daea7c151d9c38d22b961246343`
- Gambatte libretro commit `d9d6cd06382d1ced30de34d56d3609452323dab1`
- `retroarch-assets` commit `73106363e14e34c08a5854b4cfbc29f184e3b783`
  MaterialUI assets (CC BY 4.0)
- `common-overlays` commit `271f0b55c0716c7a18eb960a6b65b9ad6e2ea1cb`
  RetroPad overlay assets
- `libretro-core-info` commit `7e6b39632e6041e406794a22fd952e205c87e049`

RetroArch and the AERA driver adaptation are GPLv3-or-later. Gambatte is GPLv2,
and the 2048 core is public domain. Third-party asset licensing remains
described by the upstream repositories and is included in the staged runtime.

## Build

`source/build-runtime.sh` pins the source commits, applies the AERA drivers,
builds an ARM64 musl runtime, and stages the selected assets. It expects the
cross compiler wrappers in `AERA_CC`/`AERA_CXX` and LLVM strip in
`AERA_STRIP`. Then run:

```sh
python3 source/pack.py STAGED_RUNTIME build
```

Update `plugin.json` from `build/metadata.json`, sign the exact manifest with
the AERA release key, and attach `runtime.xz` to the matching GitHub release.
Never commit the private signing key.
