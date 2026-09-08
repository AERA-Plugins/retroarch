/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RETROARCH_AERA_PLATFORM_H
#define RETROARCH_AERA_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#define AERA_FRAME_WIDTH 720U
#define AERA_FRAME_HEIGHT 1584U
#define AERA_FRAME_SLOTS 2U
#define AERA_FRAME_BYTES (AERA_FRAME_WIDTH * AERA_FRAME_HEIGHT * 4U)
#define AERA_SHARED_BYTES (AERA_FRAME_BYTES * AERA_FRAME_SLOTS)

enum aera_message_kind {
   AERA_TOUCH_DOWN = 1, AERA_TOUCH_MOVE, AERA_TOUCH_UP, AERA_KEY,
   AERA_CLOSE, AERA_ACK, AERA_FRAME = 32, AERA_STATUS, AERA_ERROR
};

struct aera_message {
   uint32_t magic;
   uint32_t kind;
   uint32_t sequence;
   int32_t x;
   int32_t y;
   uint32_t value;
   char text[128];
};

bool aera_platform_init(void);
void aera_platform_poll(void);
void aera_platform_shutdown(void);
bool aera_platform_alive(void);
bool aera_platform_frame_ready(void);
uint32_t *aera_platform_next_frame(void);
bool aera_platform_publish_frame(void);
int16_t aera_platform_pointer_x(void);
int16_t aera_platform_pointer_y(void);
bool aera_platform_pointer_pressed(void);

#endif
