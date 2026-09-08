/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "aera_platform.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define AERA_MAGIC 0x41525241U
#define AERA_FRAME_FD 3
#define AERA_CONTROL_FD 4

static uint8_t *aera_pixels;
static uint32_t aera_sequence;
static bool aera_outstanding;
static bool aera_running;
static bool aera_pressed;
static int32_t aera_x;
static int32_t aera_y;

static bool aera_valid_host_message(const struct aera_message *message)
{
   if (message->magic != AERA_MAGIC ||
       memchr(message->text, '\0', sizeof(message->text)) == NULL)
      return false;
   switch (message->kind) {
      case AERA_TOUCH_DOWN:
      case AERA_TOUCH_MOVE:
      case AERA_TOUCH_UP:
         return message->x >= 0 && message->y >= 0 &&
            message->x < (int32_t)AERA_FRAME_WIDTH &&
            message->y < (int32_t)AERA_FRAME_HEIGHT;
      case AERA_KEY:
      case AERA_CLOSE:
      case AERA_ACK:
         return true;
      default:
         return false;
   }
}

bool aera_platform_init(void)
{
   struct stat info;
   int type = 0;
   socklen_t type_size = sizeof(type);
   if (aera_pixels)
      return true;
   if (fstat(AERA_FRAME_FD, &info) != 0 || !S_ISREG(info.st_mode) ||
       (uint64_t)info.st_size != (uint64_t)AERA_SHARED_BYTES ||
       getsockopt(AERA_CONTROL_FD, SOL_SOCKET, SO_TYPE,
                  &type, &type_size) != 0 || type != SOCK_SEQPACKET)
      return false;
   aera_pixels = mmap(NULL, AERA_SHARED_BYTES, PROT_READ | PROT_WRITE,
         MAP_SHARED, AERA_FRAME_FD, 0);
   if (aera_pixels == MAP_FAILED) {
      aera_pixels = NULL;
      return false;
   }
   if (fcntl(AERA_CONTROL_FD, F_SETFL,
            fcntl(AERA_CONTROL_FD, F_GETFL) | O_NONBLOCK) != 0) {
      munmap(aera_pixels, AERA_SHARED_BYTES);
      aera_pixels = NULL;
      return false;
   }
   aera_running = true;
   return true;
}

void aera_platform_poll(void)
{
   struct aera_message message;
   ssize_t count;
   if (!aera_running)
      return;
   for (;;) {
      count = recv(AERA_CONTROL_FD, &message, sizeof(message),
                   MSG_DONTWAIT | MSG_TRUNC);
      if (count < 0 && errno == EINTR)
         continue;
      if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
         return;
      if (count != (ssize_t)sizeof(message) ||
          !aera_valid_host_message(&message)) {
         aera_running = false;
         return;
      }
      switch (message.kind) {
         case AERA_TOUCH_DOWN:
         case AERA_TOUCH_MOVE:
            aera_x = message.x;
            aera_y = message.y;
            aera_pressed = true;
            break;
         case AERA_TOUCH_UP:
            aera_x = message.x;
            aera_y = message.y;
            aera_pressed = false;
            break;
         case AERA_ACK:
            if (aera_outstanding && message.sequence == aera_sequence)
               aera_outstanding = false;
            break;
         case AERA_CLOSE:
            aera_running = false;
            return;
         default:
            break;
      }
   }
}

void aera_platform_shutdown(void)
{
   if (aera_pixels)
      munmap(aera_pixels, AERA_SHARED_BYTES);
   aera_pixels = NULL;
   aera_running = false;
   aera_outstanding = false;
}

bool aera_platform_alive(void)
{
   aera_platform_poll();
   return aera_running;
}

bool aera_platform_frame_ready(void)
{
   aera_platform_poll();
   return aera_running && !aera_outstanding && aera_pixels;
}

uint32_t *aera_platform_next_frame(void)
{
   const uint32_t slot = (aera_sequence + 1U) % AERA_FRAME_SLOTS;
   return aera_pixels ?
      (uint32_t *)(aera_pixels + slot * AERA_FRAME_BYTES) : NULL;
}

bool aera_platform_publish_frame(void)
{
   struct aera_message message;
   ssize_t count;
   if (!aera_platform_frame_ready())
      return false;
   memset(&message, 0, sizeof(message));
   message.magic = AERA_MAGIC;
   message.kind = AERA_FRAME;
   message.sequence = ++aera_sequence;
   message.x = AERA_FRAME_WIDTH;
   message.y = AERA_FRAME_HEIGHT;
   message.value = AERA_FRAME_BYTES;
   count = send(AERA_CONTROL_FD, &message, sizeof(message),
                MSG_DONTWAIT | MSG_NOSIGNAL);
   if (count != (ssize_t)sizeof(message)) {
      if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
         return false;
      aera_running = false;
      return false;
   }
   aera_outstanding = true;
   return true;
}

static int16_t aera_normalize(int32_t value, uint32_t extent)
{
   int64_t scaled = ((int64_t)value * 65535) / (extent - 1U) - 32768;
   if (scaled < -32768) scaled = -32768;
   if (scaled > 32767) scaled = 32767;
   return (int16_t)scaled;
}

int16_t aera_platform_pointer_x(void)
{ return aera_normalize(aera_x, AERA_FRAME_WIDTH); }
int16_t aera_platform_pointer_y(void)
{ return aera_normalize(aera_y, AERA_FRAME_HEIGHT); }
bool aera_platform_pointer_pressed(void) { return aera_pressed; }
