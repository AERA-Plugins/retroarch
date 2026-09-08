#!/bin/bash
set -euo pipefail

retroarch_source=${RETROARCH_SOURCE:?Set RETROARCH_SOURCE to RetroArch v1.22.2}
core_source=${CORE_2048_SOURCE:?Set CORE_2048_SOURCE to libretro-2048}
assets_source=${RETROARCH_ASSETS_SOURCE:?Set RETROARCH_ASSETS_SOURCE}
overlays_source=${RETROARCH_OVERLAYS_SOURCE:?Set RETROARCH_OVERLAYS_SOURCE}
sysroot=${AERA_SYSROOT:-/tmp/aera-webkit-sysroot}
cc=${AERA_CC:?Set AERA_CC to an ARM64 musl compiler wrapper}
cxx=${AERA_CXX:?Set AERA_CXX to an ARM64 musl C++ compiler wrapper}
output=${1:?Pass the staged runtime output directory}
script_dir=$(cd -- "$(dirname -- "$0")" && pwd)

test "$(git -C "$retroarch_source" rev-parse HEAD)" = \
  69a4f0ea1e8aaf442ae4858f2e7f2b31a1776576
test "$(git -C "$core_source" rev-parse HEAD)" = \
  39333f7b13dc4daea7c151d9c38d22b961246343

patch -d "$retroarch_source" -p1 < "$script_dir/retroarch-aera.patch"
mkdir -p "$retroarch_source/aera" "$retroarch_source/gfx/drivers" \
  "$retroarch_source/input/drivers" "$retroarch_source/audio/drivers"
cp "$script_dir/drivers/aera/"* "$retroarch_source/aera/"
cp "$script_dir/drivers/gfx/aera_gfx.c" "$retroarch_source/gfx/drivers/"
cp "$script_dir/drivers/input/aera_input.c" "$retroarch_source/input/drivers/"
cp "$script_dir/drivers/audio/aera_audio.c" "$retroarch_source/audio/drivers/"

cd "$retroarch_source"
make clean || true
CC="$cc" CXX="$cxx" PKG_CONFIG_LIBDIR="$sysroot/usr/lib/pkgconfig" \
  ./configure --host=aarch64-alpine-linux-musl --prefix=/usr \
  --with-assets_dir=/usr/share/retroarch/assets \
  --with-core_info_dir=/usr/share/retroarch/info \
  --enable-materialui --disable-xmb --disable-ozone --disable-rgui \
  --disable-x11 --disable-wayland --disable-sdl --disable-sdl2 \
  --disable-opengl --disable-vulkan --disable-kms --disable-egl \
  --disable-alsa --disable-tinyalsa --disable-oss --disable-pulse \
  --disable-pipewire --disable-jack --disable-ffmpeg --disable-qt \
  --disable-caca --disable-sixel --disable-freetype --disable-networking \
  --disable-cheevos --disable-discord --disable-translate --disable-7zip \
  --disable-zstd --disable-chd --disable-flac --disable-imageviewer \
  --disable-video_filter --disable-dsp_filter --disable-runahead \
  --disable-rewind --disable-bsv_movie --disable-accessibility \
  --disable-shaderpipeline --disable-online_updater --disable-update_cores \
  --disable-update_core_info --disable-update_assets --disable-xdelta \
  --disable-parport --disable-test_drivers --disable-crtswitchres \
  --disable-cdrom --disable-langextra --disable-libretrodb \
  --disable-audiomixer --disable-builtinglslang --enable-builtinzlib
sed -i 's/^HAVE_XKBCOMMON = 1/HAVE_XKBCOMMON = 0/' config.mk
sed -i 's/^HAVE_GLSLANG_SPIRV_TOOLS = 1/HAVE_GLSLANG_SPIRV_TOOLS = 0/' config.mk
sed -i 's/^HAVE_GLSLANG_SPIRV_TOOLS_OPT = 1/HAVE_GLSLANG_SPIRV_TOOLS_OPT = 0/' config.mk
sed -i 's/^#define HAVE_XKBCOMMON 1/\/\* #undef HAVE_XKBCOMMON \*\//' config.h
sed -i 's/^#define HAVE_GLSLANG_SPIRV_TOOLS 1/\/\* #undef HAVE_GLSLANG_SPIRV_TOOLS \*\//' config.h
sed -i 's/^#define HAVE_GLSLANG_SPIRV_TOOLS_OPT 1/\/\* #undef HAVE_GLSLANG_SPIRV_TOOLS_OPT \*\//' config.h
make HAVE_AERA=1 -j"$(nproc)"

make -C "$core_source" -f Makefile.libretro clean || true
make -C "$core_source" -f Makefile.libretro platform=unix \
  CC="$cc" CXX="$cxx" -j"$(nproc)"

rm -rf "$output"
mkdir -p "$output/lib" "$output/etc" "$output/usr/bin" \
  "$output/usr/lib/libretro" "$output/usr/share/retroarch/assets/glui" \
  "$output/usr/share/retroarch/info" \
  "$output/usr/share/retroarch/overlays"
cp "$sysroot/lib/ld-musl-aarch64.so.1" "$output/lib/"
cp retroarch "$output/usr/bin/retroarch"
cp "$core_source/2048_libretro.so" "$output/usr/lib/libretro/"
cp "$assets_source/glui/"* "$output/usr/share/retroarch/assets/glui/"
cp "$assets_source/pkg/fallback-font.ttf" \
  "$assets_source/pkg/osd-font.ttf" "$output/usr/share/retroarch/assets/"
cp "$overlays_source/gamepads/retropad/retropad.cfg" \
  "$output/usr/share/retroarch/overlays/"
cp -R "$overlays_source/gamepads/retropad/img" \
  "$output/usr/share/retroarch/overlays/"
cp "$script_dir/retroarch.cfg" "$output/etc/"
curl -L --fail --silent --show-error \
  https://raw.githubusercontent.com/libretro/libretro-core-info/master/2048_libretro.info \
  -o "$output/usr/share/retroarch/info/2048_libretro.info"
llvm-strip --strip-unneeded "$output/usr/bin/retroarch" \
  "$output/usr/lib/libretro/2048_libretro.so"
