/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#include "../../config.h"
#endif
#ifdef HAVE_MENU
#include "../../menu/menu_driver.h"
#endif
#include "../video_driver.h"
#include "../../input/input_driver.h"
#include "../../aera/aera_platform.h"

typedef struct aera_video {
   uint8_t *menu;
   unsigned menu_width, menu_height, menu_pitch;
   bool rgb32, menu_rgb32, menu_enabled;
} aera_video_t;

extern input_driver_t input_aera;

static void *aera_gfx_init(const video_info_t *video,
      input_driver_t **input, void **input_data)
{
   aera_video_t *aera = (aera_video_t *)calloc(1, sizeof(*aera));
   (void)video;
   if (!aera || !aera_platform_init()) { free(aera); return NULL; }
   aera->rgb32 = video->rgb32;
   *input = &input_aera;
   *input_data = (void *)-1;
   video_driver_set_size(AERA_FRAME_WIDTH, AERA_FRAME_HEIGHT);
   return aera;
}

static uint32_t aera_pixel_32(const uint8_t *row, unsigned x)
{
   uint32_t pixel;
   memcpy(&pixel, row + x * 4U, sizeof(pixel));
   return pixel | 0xff000000U;
}

static uint32_t aera_pixel_565(const uint8_t *row, unsigned x)
{
   uint16_t pixel;
   uint32_t r, g, b;
   memcpy(&pixel, row + x * 2U, sizeof(pixel));
   r = (pixel >> 11) & 31U; g = (pixel >> 5) & 63U; b = pixel & 31U;
   return 0xff000000U | ((r * 255U / 31U) << 16) |
      ((g * 255U / 63U) << 8) | (b * 255U / 31U);
}

static uint32_t aera_pixel_4444(const uint8_t *row, unsigned x)
{
   uint16_t pixel;
   uint32_t r, g, b;
   memcpy(&pixel, row + x * 2U, sizeof(pixel));
   r = (pixel >> 12) & 15U; g = (pixel >> 8) & 15U; b = (pixel >> 4) & 15U;
   return 0xff000000U | ((r * 17U) << 16) |
      ((g * 17U) << 8) | (b * 17U);
}

static void aera_scale(uint32_t *output, const void *frame,
      unsigned width, unsigned height, unsigned pitch, bool rgb32, bool menu)
{
   const uint8_t *source = (const uint8_t *)frame;
   unsigned out_width = AERA_FRAME_WIDTH, out_height = AERA_FRAME_HEIGHT;
   unsigned left = 0, top = 0, x, y;
   if (!menu) {
      uint64_t fit_width = (uint64_t)AERA_FRAME_HEIGHT * width / height;
      if (fit_width <= AERA_FRAME_WIDTH) out_width = (unsigned)fit_width;
      else out_height = (unsigned)((uint64_t)AERA_FRAME_WIDTH * height / width);
      left = (AERA_FRAME_WIDTH - out_width) / 2U;
      top = (AERA_FRAME_HEIGHT - out_height) / 2U;
   }
   memset(output, 0, AERA_FRAME_BYTES);
   for (y = 0; y < out_height; ++y) {
      const unsigned sy = (unsigned)((uint64_t)y * height / out_height);
      const uint8_t *row = source + sy * pitch;
      uint32_t *dest = output + (top + y) * AERA_FRAME_WIDTH + left;
      for (x = 0; x < out_width; ++x) {
         const unsigned sx = (unsigned)((uint64_t)x * width / out_width);
         dest[x] = rgb32 ? aera_pixel_32(row, sx) :
            (menu ? aera_pixel_4444(row, sx) : aera_pixel_565(row, sx));
      }
   }
}

static bool aera_gfx_frame(void *data, const void *frame,
      unsigned width, unsigned height, uint64_t frame_count,
      unsigned pitch, const char *msg, video_frame_info_t *video_info)
{
   aera_video_t *aera = (aera_video_t *)data;
   const void *selected = frame;
   bool rgb32 = aera->rgb32;
   bool menu = false;
   uint32_t *output;
   (void)frame_count; (void)msg;
#ifdef HAVE_MENU
   {
      const bool menu_alive =
         (video_info->menu_st_flags & MENU_ST_FLAG_ALIVE) != 0;
      menu_driver_frame(menu_alive, video_info);
      /* RGUI submits its first texture after the dummy 4x4 cached frame.
       * Publishing that dummy frame makes RetroArch wait for another core
       * frame that never arrives while the menu is idle. Let the texture
       * callback publish the initial menu instead. */
      if (menu_alive && aera->menu_enabled && !aera->menu)
         return aera_platform_alive();
      if (menu_alive && aera->menu_enabled && aera->menu) {
         selected = aera->menu;
         width = aera->menu_width; height = aera->menu_height;
         pitch = aera->menu_pitch; rgb32 = aera->menu_rgb32; menu = true;
      }
   }
#endif
   if (!selected || !width || !height || !pitch ||
       !aera_platform_frame_ready())
      return aera_platform_alive();
   output = aera_platform_next_frame();
   if (!output) return false;
   aera_scale(output, selected, width, height, pitch, rgb32, menu);
   aera_platform_publish_frame();
   return aera_platform_alive();
}

static void aera_gfx_nonblock(void *data, bool toggle,
      bool adaptive, unsigned interval)
{ (void)data; (void)toggle; (void)adaptive; (void)interval; }
static bool aera_gfx_alive(void *data)
{ (void)data; return aera_platform_alive(); }
static bool aera_gfx_focus(void *data) { (void)data; return true; }
static bool aera_gfx_screensaver(void *data, bool enable)
{ (void)data; (void)enable; return false; }
static bool aera_gfx_windowed(void *data) { (void)data; return false; }
static bool aera_gfx_shader(void *data, enum rarch_shader_type type,
      const char *path)
{ (void)data; (void)type; (void)path; return false; }
static void aera_gfx_free(void *data)
{
   aera_video_t *aera = (aera_video_t *)data;
   if (aera) free(aera->menu);
   free(aera);
   aera_platform_shutdown();
}
static void aera_gfx_viewport(void *data, unsigned width, unsigned height,
      bool force_full, bool rotate)
{ (void)data; (void)width; (void)height; (void)force_full; (void)rotate; }
static void aera_gfx_rotation(void *data, unsigned rotation)
{ (void)data; (void)rotation; }

static void aera_set_texture_frame(void *data, const void *frame, bool rgb32,
      unsigned width, unsigned height, float alpha)
{
   aera_video_t *aera = (aera_video_t *)data;
   const unsigned pitch = width * (rgb32 ? 4U : 2U);
   const size_t bytes = (size_t)pitch * height;
   uint8_t *replacement;
   (void)alpha;
   if (!frame || !width || !height || !pitch || bytes > AERA_FRAME_BYTES)
      return;
   replacement = (uint8_t *)realloc(aera->menu, bytes);
   if (!replacement) return;
   aera->menu = replacement;
   memcpy(aera->menu, frame, bytes);
   aera->menu_width = width; aera->menu_height = height;
   aera->menu_pitch = pitch; aera->menu_rgb32 = rgb32;
   /* RGUI can update its software texture without scheduling a second video
    * frame. Submit it here when the channel is free so the initial menu and
    * touch-driven redraws are never stranded behind the cached dummy frame. */
   if (aera->menu_enabled && aera_platform_frame_ready()) {
      uint32_t *output = aera_platform_next_frame();
      if (output) {
         aera_scale(output, aera->menu, aera->menu_width, aera->menu_height,
                    aera->menu_pitch, aera->menu_rgb32, true);
         aera_platform_publish_frame();
      }
   }
}

static void aera_set_texture_enable(void *data, bool state, bool fullscreen)
{ (void)fullscreen; ((aera_video_t *)data)->menu_enabled = state; }

static const video_poke_interface_t aera_poke = {
   NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
   NULL, NULL, aera_set_texture_frame, aera_set_texture_enable,
   NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
static void aera_get_poke(void *data,
      const video_poke_interface_t **interface)
{ (void)data; *interface = &aera_poke; }

video_driver_t video_aera = {
   aera_gfx_init, aera_gfx_frame, aera_gfx_nonblock, aera_gfx_alive,
   aera_gfx_focus, aera_gfx_screensaver, aera_gfx_windowed,
   aera_gfx_shader, aera_gfx_free, "aera", aera_gfx_viewport,
   aera_gfx_rotation, NULL, NULL, NULL,
#ifdef HAVE_OVERLAY
   NULL,
#endif
   aera_get_poke, NULL,
#ifdef HAVE_GFX_WIDGETS
   NULL
#endif
};
