/* EINA - EFL data type library
 * Copyright (C) 2015 Carsten Haitzler
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library;
 * if not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "Eina.h"
#include "eina_evlog.h"
#include "eina_debug.h"

#ifdef HAVE_EVIL
# include <Evil.h>
#endif

#if defined(__APPLE__) && defined(__MACH__)
# include <mach/mach_time.h>
#endif

#include <time.h>
#include <unistd.h>

# ifdef HAVE_MMAP
#  include <sys/mman.h>
# endif

# define EVLOG_BUF_SIZE (4 * (1024 * 1024))

static Eina_Spinlock   _evlog_lock;
static int             _evlog_go = 0;

static Eina_Evlog_Buf *buf; // current event log we are writing events to
static Eina_Evlog_Buf  buffers[2]; // double-buffer our event log buffers

static FILE *_evlog_file = NULL;

#if defined (HAVE_CLOCK_GETTIME) || defined (EXOTIC_PROVIDE_CLOCK_GETTIME)
static clockid_t _eina_evlog_time_clock_id = -1;
#elif defined(__APPLE__) && defined(__MACH__)
static double _eina_evlog_time_clock_conversion = 1e-9;
#endif

static inline double
get_time(void)
{
#if defined (HAVE_CLOCK_GETTIME) || defined (EXOTIC_PROVIDE_CLOCK_GETTIME)
   struct timespec t;

   if (EINA_UNLIKELY(_eina_evlog_time_clock_id < 0))
     {
        if (!clock_gettime(CLOCK_MONOTONIC, &t))
          _eina_evlog_time_clock_id = CLOCK_MONOTONIC;
        else
          _eina_evlog_time_clock_id = CLOCK_REALTIME;
     }

   if (EINA_UNLIKELY(clock_gettime(_eina_evlog_time_clock_id, &t)))
     {
        struct timeval timev;
        gettimeofday(&timev, NULL);
        return (double)timev.tv_sec + (((double)timev.tv_usec) / 1000000.0);
     }
   return (double)t.tv_sec + (((double)t.tv_nsec) / 1000000000.0);
#elif defined(HAVE_EVIL)
   return evil_time_get();
#elif defined(__APPLE__) && defined(__MACH__)
   return _eina_evlog_time_clock_conversion * (double)mach_absolute_time();
#else
   struct timeval timev;
   gettimeofday(&timev, NULL);
   return (double)timev.tv_sec + (((double)timev.tv_usec) / 1000000.0);
#endif
}

static void
alloc_buf(Eina_Evlog_Buf *b, unsigned int size)
{
   if (b->buf) return;
   b->size = size;
   b->top = 0;
# ifdef HAVE_MMAP
   b->buf = mmap(NULL, size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANON, -1, 0);
   if (b->buf == MAP_FAILED) b->buf = NULL;
# else
   b->buf = malloc(size);
# endif
   b->overflow = 0;
}

static void
free_buf(Eina_Evlog_Buf *b)
{
   if (!b->buf) return;
   b->size = 0;
   b->top = 0;
# ifdef HAVE_MMAP
   munmap(b->buf, b->size);
# else
   free(b->buf);
# endif
   b->buf = NULL;
}

static inline void *
push_buf(Eina_Evlog_Buf *b, unsigned int size)
{
   void *ptr;

   if (b->size < size) abort();
   if ((b->top + size) > (b->size))
     {
        b->overflow++;
        b->top = 0;
     }
   ptr = (b->buf + b->top);
   b->top += size;
   return ptr;
}

EAPI void
eina_evlog(const char *event, void *obj, double srctime, const char *detail)
{
   Eina_Evlog_Item *item;
   int size;
   char *strings;
   double now = get_time();
   unsigned short detail_offset = 0;
   unsigned short event_size;

   eina_spinlock_take(&_evlog_lock);
   if (!_evlog_go)
     {
        eina_spinlock_release(&_evlog_lock);
        return;
     }
   size = sizeof(Eina_Evlog_Item);
   event_size = strlen(event) + 1;
   size += event_size;
   if (detail)
     {
        detail_offset = size;
        size += strlen(detail) + 1;
     }
   size = sizeof(double) * ((size + sizeof(double) - 1) / sizeof(double));

   strings = push_buf(buf, size);
   item = (Eina_Evlog_Item *)strings;
   item->tim = now;
   item->srctim = srctime;
   item->thread = (unsigned long long)pthread_self();
   item->obj = (unsigned long long)obj;
   item->event_offset = sizeof(Eina_Evlog_Item);
   item->detail_offset = detail_offset;
   item->event_next = size;

   strcpy(strings + sizeof(Eina_Evlog_Item), event);
   if (detail_offset > 0) strcpy(strings + detail_offset, detail);

   eina_spinlock_release(&_evlog_lock);
}

EAPI Eina_Evlog_Buf *
eina_evlog_steal(void)
{
   Eina_Evlog_Buf *stolen = NULL;

   eina_spinlock_take(&_evlog_lock);
   if (buf == &(buffers[0]))
     {
        buf = &(buffers[1]);
        buf->top = 0;
        buf->overflow = 0;
        stolen = &(buffers[0]);
     }
   else
     {
        buf = &(buffers[0]);
        buf->top = 0;
        buf->overflow = 0;
        stolen = &(buffers[1]);
     }
   eina_spinlock_release(&_evlog_lock);
   return stolen;
}

EAPI void
eina_evlog_start(void)
{
   eina_spinlock_take(&_evlog_lock);
   _evlog_go++;
   if (_evlog_go == 1)
     {
        // alloc 2 buffers for spinning around in
        alloc_buf(&(buffers[0]), EVLOG_BUF_SIZE);
        alloc_buf(&(buffers[1]), EVLOG_BUF_SIZE);
     }
   eina_spinlock_release(&_evlog_lock);
}

EAPI void
eina_evlog_stop(void)
{
   eina_spinlock_take(&_evlog_lock);
   _evlog_go--;
   if (_evlog_go == 0)
     {
        free_buf(&(buffers[0]));
        free_buf(&(buffers[1]));
     }
   eina_spinlock_release(&_evlog_lock);
}

static Eina_Bool
_evlog_timer_cb()
{
   if (!_evlog_go) return EINA_FALSE;
   Eina_Evlog_Buf *evlog = eina_evlog_steal();
   if ((_evlog_file) && (evlog->top > 0))
     {
        unsigned int header[3];

        header[0] = 0xffee211;
        header[1] = evlog->top;
        header[2] = evlog->overflow;
        fwrite(header, 12, 1, _evlog_file);
        fwrite(evlog->buf, evlog->top, 1, _evlog_file);
     }
   return EINA_TRUE;
}

// enable evlog
static Eina_Bool
_start_cb(Eina_Debug_Session *session EINA_UNUSED, int cid EINA_UNUSED, void *buffer EINA_UNUSED, int size EINA_UNUSED)
{
   char path[1024];
   snprintf(path, sizeof(path), "%s/efl_debug_evlog-%ld.log", getenv("HOME"), (long)getpid());
   _evlog_file = fopen(path, "wb");
   eina_evlog_start();
   /* FIXME: should register a timer in the eina debug recv thread */
   eina_debug_timer_add(200, _evlog_timer_cb);
   return EINA_TRUE;
}

// stop evlog
static Eina_Bool
_stop_cb(Eina_Debug_Session *session EINA_UNUSED, int cid EINA_UNUSED, void *buffer EINA_UNUSED, int size EINA_UNUSED)
{
   eina_evlog_stop();
   fclose(_evlog_file);
   _evlog_file = NULL;
   return EINA_TRUE;
}

static const Eina_Debug_Opcode _EINA_DEBUG_EVLOG_OPS[] = {
       {"evlog/on", NULL, &_start_cb},
       {"evlog/off", NULL, &_stop_cb},
       {NULL, NULL, NULL}
};

Eina_Bool
eina_evlog_init(void)
{
   eina_spinlock_new(&_evlog_lock);
   buf = &(buffers[0]);
   eina_evlog("+eina_init", NULL, 0.0, NULL);
   eina_debug_opcodes_register(NULL, _EINA_DEBUG_EVLOG_OPS, NULL);
   return EINA_TRUE;
}

Eina_Bool
eina_evlog_shutdown(void)
{
   // yes - we don't free tyhe evlog buffers. they may be in used by debug th
   eina_spinlock_free(&_evlog_lock);
   return EINA_TRUE;
}
