/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <string.h>

#include <encodings/utf.h>

#ifdef HAVE_CONFIG_H
#include "../../config.h"
#endif
#ifdef HAVE_MENU
#include "../../menu/menu_driver.h"
#endif
#include "../video_driver.h"
#include "../font_driver.h"
#include "../gfx_display.h"
#include "../../input/input_driver.h"
#include "../../aera/aera_platform.h"
#include "../../libretro-common/include/formats/image.h"

#ifdef HAVE_OVERLAY
typedef struct aera_overlay_image {
   uint32_t *pixels;
   unsigned width, height;
   float tex_x, tex_y, tex_w, tex_h;
   float vertex_x, vertex_y, vertex_w, vertex_h;
   float alpha;
} aera_overlay_image_t;
#endif

typedef struct aera_texture {
   uint32_t *pixels;
   unsigned width, height;
} aera_texture_t;

typedef struct aera_font {
   struct aera_video *video;
   const font_renderer_driver_t *driver;
   void *data;
   const struct font_atlas *atlas;
} aera_font_t;

typedef struct aera_video {
   uint8_t *menu;
   unsigned menu_width, menu_height, menu_pitch;
   bool rgb32, menu_rgb32, menu_enabled;
   uint32_t *canvas;
   bool canvas_drawn;
   bool scissor_enabled;
   int scissor_left, scissor_top, scissor_right, scissor_bottom;
#ifdef HAVE_OVERLAY
   aera_overlay_image_t *overlays;
   unsigned overlay_count;
   bool overlays_enabled;
#endif
} aera_video_t;

extern input_driver_t input_aera;

static void *aera_gfx_init(const video_info_t *video,
      input_driver_t **input, void **input_data)
{
   aera_video_t *aera = (aera_video_t *)calloc(1, sizeof(*aera));
   (void)video;
   if (!aera || !aera_platform_init()) { free(aera); return NULL; }
   aera->canvas = (uint32_t *)calloc(AERA_FRAME_WIDTH * AERA_FRAME_HEIGHT,
                                     sizeof(uint32_t));
   if (!aera->canvas) {
      free(aera);
      aera_platform_shutdown();
      return NULL;
   }
   aera->rgb32 = video->rgb32;
   /* Explicitly decline video-coupled input. RetroArch passes its already
    * selected driver in *input; leaving that pointer untouched makes
    * video_driver_init_input() assume initialization is complete and skips
    * input_driver_init_joypads(). Clearing it forces the normal AERA input
    * init path, which reuses this mapped transport and installs the joypad
    * shim required by the menu analog scan. */
   *input = NULL;
   *input_data = NULL;
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

static uint8_t aera_float_byte(float value)
{
   if (value <= 0.0f) return 0;
   if (value >= 1.0f) return 255;
   return (uint8_t)(value * 255.0f + 0.5f);
}

static void aera_blend_pixel(uint32_t *destination, uint32_t source,
      uint8_t red_mod, uint8_t green_mod, uint8_t blue_mod, uint8_t alpha_mod)
{
   const unsigned source_alpha = (source >> 24) & 255U;
   const unsigned alpha = source_alpha * alpha_mod / 255U;
   const unsigned inverse = 255U - alpha;
   const uint32_t current = *destination;
   unsigned r, g, b;
   if (!alpha) return;
   r = (((((source >> 16) & 255U) * red_mod / 255U) * alpha) +
        (((current >> 16) & 255U) * inverse)) / 255U;
   g = (((((source >> 8) & 255U) * green_mod / 255U) * alpha) +
        (((current >> 8) & 255U) * inverse)) / 255U;
   b = ((((source & 255U) * blue_mod / 255U) * alpha) +
        ((current & 255U) * inverse)) / 255U;
   *destination = 0xff000000U | (r << 16) | (g << 8) | b;
}

static void aera_scale(uint32_t *output, const void *frame,
      unsigned width, unsigned height, unsigned pitch, bool rgb32, bool menu,
      bool controls)
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
      if (controls && out_height + 90U < AERA_FRAME_HEIGHT)
         top = 90U;
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

static float aera_clamp_float(float value, float low, float high)
{
   if (value < low) return low;
   if (value > high) return high;
   return value;
}

#ifdef HAVE_OVERLAY
static void aera_free_overlays(aera_video_t *aera)
{
   unsigned i;
   if (!aera) return;
   for (i = 0; i < aera->overlay_count; ++i)
      free(aera->overlays[i].pixels);
   free(aera->overlays);
   aera->overlays = NULL;
   aera->overlay_count = 0;
}

static void aera_draw_overlays(aera_video_t *aera, uint32_t *output)
{
   unsigned i;
   if (!aera || !output || !aera->overlays_enabled) return;
   for (i = 0; i < aera->overlay_count; ++i) {
      aera_overlay_image_t *image = &aera->overlays[i];
      int left, top, right, bottom, x, y;
      if (!image->pixels || !image->width || !image->height ||
          image->alpha <= 0.0f || image->vertex_w <= 0.0f ||
          image->vertex_h <= 0.0f || image->tex_w <= 0.0f ||
          image->tex_h <= 0.0f)
         continue;
      left = (int)(image->vertex_x * AERA_FRAME_WIDTH);
      top = (int)(image->vertex_y * AERA_FRAME_HEIGHT);
      right = (int)((image->vertex_x + image->vertex_w) * AERA_FRAME_WIDTH);
      bottom = (int)((image->vertex_y + image->vertex_h) * AERA_FRAME_HEIGHT);
      if (left < 0) left = 0;
      if (top < 0) top = 0;
      if (right > (int)AERA_FRAME_WIDTH) right = AERA_FRAME_WIDTH;
      if (bottom > (int)AERA_FRAME_HEIGHT) bottom = AERA_FRAME_HEIGHT;
      if (right <= left || bottom <= top) continue;
      for (y = top; y < bottom; ++y) {
         const float v = image->tex_y + image->tex_h *
            ((float)(y - top) / (float)(bottom - top));
         unsigned sy = (unsigned)(aera_clamp_float(v, 0.0f, 0.999999f) *
                                  image->height);
         for (x = left; x < right; ++x) {
            const float u = image->tex_x + image->tex_w *
               ((float)(x - left) / (float)(right - left));
            const unsigned sx = (unsigned)(aera_clamp_float(
               u, 0.0f, 0.999999f) * image->width);
            const uint32_t source = image->pixels[sy * image->width + sx];
            aera_blend_pixel(output + y * AERA_FRAME_WIDTH + x, source,
                             255, 255, 255, aera_float_byte(image->alpha));
         }
      }
   }
}

static void aera_overlay_enable(void *data, bool state)
{ ((aera_video_t *)data)->overlays_enabled = state; }

static bool aera_overlay_load(void *data, const void *image_data,
      unsigned num_images)
{
   aera_video_t *aera = (aera_video_t *)data;
   const struct texture_image *images =
      (const struct texture_image *)image_data;
   unsigned i;
   if (!aera || !images || !num_images || num_images > 64U) return false;
   aera_free_overlays(aera);
   aera->overlays = (aera_overlay_image_t *)calloc(
      num_images, sizeof(*aera->overlays));
   if (!aera->overlays) return false;
   aera->overlay_count = num_images;
   for (i = 0; i < num_images; ++i) {
      aera_overlay_image_t *target = &aera->overlays[i];
      const size_t pixels = (size_t)images[i].width * images[i].height;
      if (!images[i].pixels || !pixels || pixels > AERA_FRAME_WIDTH *
                                               AERA_FRAME_HEIGHT) {
         aera_free_overlays(aera);
         return false;
      }
      target->pixels = (uint32_t *)malloc(pixels * sizeof(uint32_t));
      if (!target->pixels) {
         aera_free_overlays(aera);
         return false;
      }
      memcpy(target->pixels, images[i].pixels, pixels * sizeof(uint32_t));
      target->width = images[i].width;
      target->height = images[i].height;
      target->tex_w = target->tex_h = 1.0f;
      target->vertex_w = target->vertex_h = 1.0f;
      target->alpha = 1.0f;
   }
   return true;
}

static void aera_overlay_tex_geom(void *data, unsigned image,
      float x, float y, float w, float h)
{
   aera_video_t *aera = (aera_video_t *)data;
   if (!aera || image >= aera->overlay_count) return;
   aera->overlays[image].tex_x = x;
   aera->overlays[image].tex_y = y;
   aera->overlays[image].tex_w = w;
   aera->overlays[image].tex_h = h;
}

static void aera_overlay_vertex_geom(void *data, unsigned image,
      float x, float y, float w, float h)
{
   aera_video_t *aera = (aera_video_t *)data;
   if (!aera || image >= aera->overlay_count) return;
   aera->overlays[image].vertex_x = x;
   aera->overlays[image].vertex_y = y;
   aera->overlays[image].vertex_w = w;
   aera->overlays[image].vertex_h = h;
}

static void aera_overlay_full_screen(void *data, bool enable)
{ (void)data; (void)enable; }

static void aera_overlay_set_alpha(void *data, unsigned image, float alpha)
{
   aera_video_t *aera = (aera_video_t *)data;
   if (!aera || image >= aera->overlay_count) return;
   aera->overlays[image].alpha = aera_clamp_float(alpha, 0.0f, 1.0f);
}

static const video_overlay_interface_t aera_overlay = {
   aera_overlay_enable, aera_overlay_load, aera_overlay_tex_geom,
   aera_overlay_vertex_geom, aera_overlay_full_screen,
   aera_overlay_set_alpha
};

static void aera_get_overlay(void *data,
      const video_overlay_interface_t **interface)
{ (void)data; *interface = &aera_overlay; }
#endif

static uintptr_t aera_load_texture(void *video_data, void *data,
      bool threaded, enum texture_filter_type filter_type)
{
   const struct texture_image *image = (const struct texture_image *)data;
   aera_texture_t *texture;
   const size_t count = image ? (size_t)image->width * image->height : 0;
   (void)video_data; (void)threaded; (void)filter_type;
   if (!image || !image->pixels || !count ||
       count > AERA_FRAME_WIDTH * AERA_FRAME_HEIGHT)
      return 0;
   texture = (aera_texture_t *)calloc(1, sizeof(*texture));
   if (!texture) return 0;
   texture->pixels = (uint32_t *)malloc(count * sizeof(uint32_t));
   if (!texture->pixels) { free(texture); return 0; }
   memcpy(texture->pixels, image->pixels, count * sizeof(uint32_t));
   texture->width = image->width;
   texture->height = image->height;
   return (uintptr_t)texture;
}

static void aera_unload_texture(void *data, bool threaded, uintptr_t handle)
{
   aera_texture_t *texture = (aera_texture_t *)handle;
   (void)data; (void)threaded;
   if (!texture) return;
   free(texture->pixels);
   free(texture);
}

static int aera_clip_left(const aera_video_t *aera)
{ return aera->scissor_enabled ? aera->scissor_left : 0; }
static int aera_clip_top(const aera_video_t *aera)
{ return aera->scissor_enabled ? aera->scissor_top : 0; }
static int aera_clip_right(const aera_video_t *aera)
{ return aera->scissor_enabled ? aera->scissor_right : AERA_FRAME_WIDTH; }
static int aera_clip_bottom(const aera_video_t *aera)
{ return aera->scissor_enabled ? aera->scissor_bottom : AERA_FRAME_HEIGHT; }

static void aera_display_draw(gfx_display_ctx_draw_t *draw, void *data,
      unsigned video_width, unsigned video_height)
{
   aera_video_t *aera = (aera_video_t *)data;
   aera_texture_t *texture;
   const float *colors;
   const float *coordinates;
   float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
   uint8_t red = 255, green = 255, blue = 255, alpha = 255;
   int left, top, right, bottom, x, y;
   unsigned i;
   if (!aera || !aera->canvas || !draw || !draw->coords ||
       !draw->texture || !draw->width || !draw->height ||
       !video_width || !video_height)
      return;
   texture = (aera_texture_t *)draw->texture;
   if (!texture->pixels || !texture->width || !texture->height) return;
   colors = draw->coords->color;
   if (colors) {
      red = aera_float_byte(colors[0]);
      green = aera_float_byte(colors[1]);
      blue = aera_float_byte(colors[2]);
      alpha = aera_float_byte(colors[3]);
   }
   coordinates = draw->coords->tex_coord;
   if (coordinates && draw->coords->vertices) {
      u0 = u1 = coordinates[0];
      v0 = v1 = coordinates[1];
      for (i = 1; i < draw->coords->vertices; ++i) {
         const float u = coordinates[i * 2U];
         const float v = coordinates[i * 2U + 1U];
         if (u < u0) u0 = u;
         if (u > u1) u1 = u;
         if (v < v0) v0 = v;
         if (v > v1) v1 = v;
      }
   }
   left = (int)((int64_t)draw->x * AERA_FRAME_WIDTH / video_width);
   right = (int)((int64_t)(draw->x + draw->width) * AERA_FRAME_WIDTH /
                 video_width);
   top = (int)((int64_t)(video_height - draw->y - draw->height) *
               AERA_FRAME_HEIGHT / video_height);
   bottom = (int)((int64_t)(video_height - draw->y) * AERA_FRAME_HEIGHT /
                  video_height);
   if (left < aera_clip_left(aera)) left = aera_clip_left(aera);
   if (top < aera_clip_top(aera)) top = aera_clip_top(aera);
   if (right > aera_clip_right(aera)) right = aera_clip_right(aera);
   if (bottom > aera_clip_bottom(aera)) bottom = aera_clip_bottom(aera);
   if (right <= left || bottom <= top) return;
   for (y = top; y < bottom; ++y) {
      const float v = v0 + (v1 - v0) *
         ((float)(y - top) / (float)(bottom - top));
      const unsigned sy = (unsigned)(aera_clamp_float(
         v, 0.0f, 0.999999f) * texture->height);
      for (x = left; x < right; ++x) {
         const float u = u0 + (u1 - u0) *
            ((float)(x - left) / (float)(right - left));
         const unsigned sx = (unsigned)(aera_clamp_float(
            u, 0.0f, 0.999999f) * texture->width);
         aera_blend_pixel(aera->canvas + y * AERA_FRAME_WIDTH + x,
                          texture->pixels[sy * texture->width + sx],
                          red, green, blue, alpha);
      }
   }
   aera->canvas_drawn = true;
}

static const float *aera_display_vertices(void)
{ static const float values[16] = {0}; return values; }
static const float *aera_display_tex_coords(void)
{ static const float values[8] = {0, 0, 1, 0, 0, 1, 1, 1}; return values; }

static void aera_scissor_begin(void *data, unsigned video_width,
      unsigned video_height, int x, int y, unsigned width, unsigned height)
{
   aera_video_t *aera = (aera_video_t *)data;
   aera->scissor_enabled = true;
   aera->scissor_left = (int)((int64_t)x * AERA_FRAME_WIDTH / video_width);
   aera->scissor_right = (int)((int64_t)(x + width) * AERA_FRAME_WIDTH /
                               video_width);
   aera->scissor_top = (int)((int64_t)y * AERA_FRAME_HEIGHT / video_height);
   aera->scissor_bottom = (int)((int64_t)(y + height) * AERA_FRAME_HEIGHT /
                                video_height);
}

static void aera_scissor_end(void *data, unsigned width, unsigned height)
{ (void)width; (void)height; ((aera_video_t *)data)->scissor_enabled = false; }

gfx_display_ctx_driver_t gfx_display_ctx_aera = {
   aera_display_draw, NULL, NULL, NULL, NULL,
   aera_display_vertices, aera_display_tex_coords,
   FONT_DRIVER_RENDER_AERA, GFX_VIDEO_DRIVER_AERA, "aera", false,
   aera_scissor_begin, aera_scissor_end
};

static int aera_font_width(void *data, const char *message,
      size_t length, float scale)
{
   aera_font_t *font = (aera_font_t *)data;
   const struct font_glyph *fallback;
   int width = 0;
   size_t offset = 0;
   if (!font || !font->driver || !message) return 0;
   fallback = font->driver->get_glyph(font->data, '?');
   while (offset < length) {
      const char *cursor = message + offset;
      const struct font_glyph *glyph = font->driver->get_glyph(
         font->data, utf8_walk(&cursor));
      if (!glyph) glyph = fallback;
      if (glyph) width += glyph->advance_x;
      if (cursor <= message + offset) ++offset;
      else offset = (size_t)(cursor - message);
   }
   return (int)(width * scale);
}

static void aera_font_line(aera_font_t *font, const char *message,
      size_t length, float scale, uint32_t color, int baseline,
      int start_x, enum text_alignment alignment)
{
   const struct font_glyph *fallback;
   size_t offset = 0;
   int pen_x = start_x;
   if (alignment == TEXT_ALIGN_RIGHT)
      pen_x -= aera_font_width(font, message, length, scale);
   else if (alignment == TEXT_ALIGN_CENTER)
      pen_x -= aera_font_width(font, message, length, scale) / 2;
   fallback = font->driver->get_glyph(font->data, '?');
   while (offset < length) {
      const char *cursor = message + offset;
      const struct font_glyph *glyph = font->driver->get_glyph(
         font->data, utf8_walk(&cursor));
      int gx, gy, gw, gh, x, y;
      if (!glyph) glyph = fallback;
      if (cursor <= message + offset) ++offset;
      else offset = (size_t)(cursor - message);
      if (!glyph) continue;
      gx = glyph->atlas_offset_x;
      gy = glyph->atlas_offset_y;
      gw = glyph->width;
      gh = glyph->height;
      for (y = 0; y < (int)(gh * scale); ++y) {
         const unsigned sy = (unsigned)(y / scale);
         const int dy = baseline + (int)(glyph->draw_offset_y * scale) + y;
         if (dy < aera_clip_top(font->video) ||
             dy >= aera_clip_bottom(font->video)) continue;
         for (x = 0; x < (int)(gw * scale); ++x) {
            const unsigned sx = (unsigned)(x / scale);
            const int dx = pen_x + (int)(glyph->draw_offset_x * scale) + x;
            uint32_t source;
            uint8_t coverage;
            if (dx < aera_clip_left(font->video) ||
                dx >= aera_clip_right(font->video)) continue;
            coverage = font->atlas->buffer[(gy + sy) * font->atlas->width +
                                           gx + sx];
            source = (color & 0x00ffffffU) |
               ((((color >> 24) & 255U) * coverage / 255U) << 24);
            aera_blend_pixel(font->video->canvas +
                             dy * AERA_FRAME_WIDTH + dx,
                             source, 255, 255, 255, 255);
         }
      }
      pen_x += (int)(glyph->advance_x * scale);
   }
   font->video->canvas_drawn = true;
}

static void *aera_font_init(void *data, const char *path, float size,
      bool threaded)
{
   aera_font_t *font = (aera_font_t *)calloc(1, sizeof(*font));
   (void)threaded;
   if (!font) return NULL;
   font->video = (aera_video_t *)data;
   if (!font_renderer_create_default(&font->driver, &font->data, path,
                                     (unsigned)size) ||
       !(font->atlas = font->driver->get_atlas(font->data))) {
      if (font->driver && font->data) font->driver->free(font->data);
      free(font);
      return NULL;
   }
   return font;
}

static void aera_font_free(void *data, bool threaded)
{
   aera_font_t *font = (aera_font_t *)data;
   (void)threaded;
   if (!font) return;
   if (font->driver && font->data) font->driver->free(font->data);
   free(font);
}

static void aera_font_render(void *userdata, void *data,
      const char *message, const struct font_params *params)
{
   aera_font_t *font = (aera_font_t *)data;
   struct font_line_metrics *metrics = NULL;
   const char *line;
   float scale;
   int x, baseline, line_height;
   uint32_t color;
   enum text_alignment alignment;
   (void)userdata;
   if (!font || !font->video || !font->video->canvas || !message || !*message)
      return;
   scale = params ? params->scale : 1.0f;
   if (scale <= 0.0f) scale = 1.0f;
   x = (int)((params ? params->x : 0.05f) * AERA_FRAME_WIDTH);
   baseline = (int)((1.0f - (params ? params->y : 0.95f)) *
                    AERA_FRAME_HEIGHT);
   alignment = params ? params->text_align : TEXT_ALIGN_LEFT;
   color = params ? params->color : 0xffffffffU;
   font->driver->get_line_metrics(font->data, &metrics);
   line_height = (int)((metrics ? metrics->height : 16) * scale);
   line = message;
   while (line && *line) {
      const char *end = strchr(line, '\n');
      const size_t length = end ? (size_t)(end - line) : strlen(line);
      aera_font_line(font, line, length, scale, color, baseline, x, alignment);
      if (!end) break;
      baseline += line_height;
      line = end + 1;
   }
}

static const struct font_glyph *aera_font_glyph(void *data, uint32_t code)
{
   aera_font_t *font = (aera_font_t *)data;
   return font && font->driver ? font->driver->get_glyph(font->data, code) : NULL;
}

static bool aera_font_metrics(void *data, struct font_line_metrics **metrics)
{
   aera_font_t *font = (aera_font_t *)data;
   if (!font || !font->driver || !font->data) return false;
   font->driver->get_line_metrics(font->data, metrics);
   return true;
}

font_renderer_t aera_font = {
   aera_font_init, aera_font_free, aera_font_render, "aera",
   aera_font_glyph, NULL, NULL, aera_font_width, aera_font_metrics
};

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
      if (menu_alive && aera->canvas) {
         memset(aera->canvas, 0, AERA_FRAME_BYTES);
         aera->canvas_drawn = false;
      }
      menu_driver_frame(menu_alive, video_info);
      if (menu_alive && aera->canvas_drawn) {
         if (aera_platform_frame_ready()) {
            output = aera_platform_next_frame();
            if (!output) return false;
            memcpy(output, aera->canvas, AERA_FRAME_BYTES);
            aera_platform_publish_frame();
         }
         return aera_platform_alive();
      }
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
   aera_scale(output, selected, width, height, pitch, rgb32, menu,
#ifdef HAVE_OVERLAY
              !menu && aera->overlays_enabled
#else
              false
#endif
   );
#ifdef HAVE_OVERLAY
   if (!menu) aera_draw_overlays(aera, output);
#endif
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
   if (aera) {
      free(aera->menu);
      free(aera->canvas);
#ifdef HAVE_OVERLAY
      aera_free_overlays(aera);
#endif
   }
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
                    aera->menu_pitch, aera->menu_rgb32, true, false);
         aera_platform_publish_frame();
      }
   }
}

static void aera_set_texture_enable(void *data, bool state, bool fullscreen)
{ (void)fullscreen; ((aera_video_t *)data)->menu_enabled = state; }

static const video_poke_interface_t aera_poke = {
   NULL, aera_load_texture, aera_unload_texture,
   NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
   NULL, NULL, aera_set_texture_frame, aera_set_texture_enable,
   font_driver_render_msg, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
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
   aera_get_overlay,
#endif
   aera_get_poke, NULL,
#ifdef HAVE_GFX_WIDGETS
   NULL
#endif
};
