/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../audio_driver.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct aera_audio { int fd; bool paused; bool nonblock; };

static void *aera_audio_init(const char *device, unsigned rate,
      unsigned latency, unsigned block_frames, unsigned *new_rate)
{
   static const char socket_name[] = "aera-browser-audio-v1";
   static const uint32_t hello[] = {0x41525041U, 48000U, 2U, 16U};
   struct sockaddr_un address;
   struct aera_audio *audio;
   socklen_t address_size;
   (void)device; (void)rate; (void)latency; (void)block_frames;
   audio = (struct aera_audio *)calloc(1, sizeof(*audio));
   if (!audio) return NULL;
   audio->fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
   if (audio->fd < 0) goto fail;
   memset(&address, 0, sizeof(address));
   address.sun_family = AF_UNIX;
   memcpy(address.sun_path + 1, socket_name, sizeof(socket_name) - 1);
   address_size = (socklen_t)(offsetof(struct sockaddr_un, sun_path) +
         1 + sizeof(socket_name) - 1);
   if (connect(audio->fd, (const struct sockaddr *)&address, address_size) != 0 ||
       send(audio->fd, hello, sizeof(hello), MSG_NOSIGNAL) != (ssize_t)sizeof(hello))
      goto fail;
   *new_rate = 48000;
   return audio;
fail:
   if (audio->fd >= 0) close(audio->fd);
   free(audio);
   return NULL;
}

static ssize_t aera_audio_write(void *data, const void *samples, size_t bytes)
{
   struct aera_audio *audio = (struct aera_audio *)data;
   const uint8_t *cursor = (const uint8_t *)samples;
   size_t done = 0;
   if (!audio || audio->paused) return 0;
   while (done < bytes) {
      ssize_t count = send(audio->fd, cursor + done, bytes - done,
            MSG_NOSIGNAL | (audio->nonblock ? MSG_DONTWAIT : 0));
      if (count > 0) { done += (size_t)count; continue; }
      if (count < 0 && errno == EINTR) continue;
      if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
      return done ? (ssize_t)done : -1;
   }
   return (ssize_t)done;
}

static bool aera_audio_stop(void *data)
{ ((struct aera_audio *)data)->paused = true; return true; }
static bool aera_audio_start(void *data, bool shutdown)
{ (void)shutdown; ((struct aera_audio *)data)->paused = false; return true; }
static bool aera_audio_alive(void *data)
{ return data && !((struct aera_audio *)data)->paused; }
static void aera_audio_set_nonblock(void *data, bool state)
{ ((struct aera_audio *)data)->nonblock = state; }
static void aera_audio_free(void *data)
{
   struct aera_audio *audio = (struct aera_audio *)data;
   if (!audio) return;
   close(audio->fd);
   free(audio);
}
static bool aera_audio_use_float(void *data) { (void)data; return false; }

audio_driver_t audio_aera = {
   aera_audio_init, aera_audio_write, aera_audio_stop, aera_audio_start,
   aera_audio_alive, aera_audio_set_nonblock, aera_audio_free,
   aera_audio_use_float, "aera", NULL, NULL, NULL, NULL
};
