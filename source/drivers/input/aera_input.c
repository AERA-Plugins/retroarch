/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <libretro.h>
#include "../input_driver.h"
#include "../../aera/aera_platform.h"

static void *aera_input_init(const char *joypad_driver)
{ (void)joypad_driver; return aera_platform_init() ? (void *)-1 : NULL; }
static void aera_input_poll(void *data)
{ (void)data; aera_platform_poll(); }
static int16_t aera_input_state(void *data,
      const input_device_driver_t *joypad,
      const input_device_driver_t *sec_joypad,
      rarch_joypad_info_t *joypad_info,
      const retro_keybind_set *binds,
      bool keyboard_mapping_blocked,
      unsigned port, unsigned device, unsigned idx, unsigned id)
{
   (void)data; (void)joypad; (void)sec_joypad; (void)joypad_info;
   (void)binds; (void)keyboard_mapping_blocked; (void)port;
   if (idx != 0) return 0;
   if (device == RETRO_DEVICE_POINTER || device == RARCH_DEVICE_POINTER_SCREEN) {
      switch (id) {
         case RETRO_DEVICE_ID_POINTER_X: return aera_platform_pointer_x();
         case RETRO_DEVICE_ID_POINTER_Y: return aera_platform_pointer_y();
         case RETRO_DEVICE_ID_POINTER_PRESSED:
            return aera_platform_pointer_pressed();
         case RETRO_DEVICE_ID_POINTER_COUNT:
            return aera_platform_pointer_pressed() ? 1 : 0;
         case RETRO_DEVICE_ID_POINTER_IS_OFFSCREEN: return 0;
         default: return 0;
      }
   }
   return 0;
}
static void aera_input_free(void *data) { (void)data; }
static bool aera_input_set_sensor_state(void *data, unsigned port,
      enum retro_sensor_action action, unsigned rate)
{ (void)data; (void)port; (void)action; (void)rate; return false; }
static float aera_input_get_sensor_input(void *data, unsigned port, unsigned id)
{ (void)data; (void)port; (void)id; return 0.0f; }
static uint64_t aera_input_get_capabilities(void *data)
{ (void)data; return 1ULL << RETRO_DEVICE_POINTER; }
static void aera_input_grab_mouse(void *data, bool state)
{ (void)data; (void)state; }
static bool aera_input_grab_stdin(void *data)
{ (void)data; return false; }
static void aera_input_keypress_vibrate(void) {}

input_driver_t input_aera = {
   aera_input_init, aera_input_poll, aera_input_state, aera_input_free,
   aera_input_set_sensor_state, aera_input_get_sensor_input,
   aera_input_get_capabilities, "aera", aera_input_grab_mouse,
   aera_input_grab_stdin, aera_input_keypress_vibrate
};
