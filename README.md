# AERA RetroArch Plugin

RetroArch 1.22.2 adapted as a signed, optional AERA Recovery Project app.

The plugin keeps RetroArch outside `recovery.img`. AERA verifies its Ed25519
manifest and payload hashes, expands it into RAM on demand, and launches it as
an unprivileged process. Frames cross a sealed double-buffered shared-memory
channel; touch events cross a bounded packet channel; stereo PCM uses AERA's
protected device audio bridge. RetroArch never owns the recovery framebuffer or
raw input devices.

The runtime uses RetroArch's MaterialUI, official icons, phone touch controls,
and a public-domain 2048 libretro core for installation testing. No copyrighted
ROM or BIOS content is included. User content is exposed read-only under
`/storage` inside the jail.

## Versions and source

- RetroArch v1.22.2, commit `69a4f0ea1e8aaf442ae4858f2e7f2b31a1776576`
- libretro-2048 commit `39333f7b13dc4daea7c151d9c38d22b961246343`
- Official `retroarch-assets` MaterialUI assets (CC BY 4.0)
- Official `common-overlays` RetroPad overlay assets

RetroArch and the AERA driver adaptation are GPLv3-or-later. The 2048 core is
public domain. Third-party asset licensing remains described by the upstream
repositories.

## Build

`source/build-runtime.sh` pins the source commits, applies the AERA drivers,
builds an ARM64 musl runtime, and stages the selected assets. Then run:

```sh
python3 source/pack.py STAGED_RUNTIME build
```

Update `plugin.json` from `build/metadata.json`, sign the exact manifest with
the AERA release key, and attach `runtime.xz` to the matching GitHub release.
Never commit the private signing key.
