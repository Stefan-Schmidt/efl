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

# ifndef _GNU_SOURCE
#  define _GNU_SOURCE 1
# endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <libunwind.h>

#include "eina_debug.h"
#include "eina_types.h"
#include "eina_list.h"
#include "eina_mempool.h"
#include "eina_util.h"

// yes - a global debug spinlock. i expect contention to be low for now, and
// when needed we can split this up into mroe locks to reduce contention when
// and if that day comes
Eina_Spinlock _eina_debug_lock;

// only init once
static Eina_Bool _inited = EINA_FALSE;
static char *_my_app_name = NULL;

extern Eina_Bool eina_module_init(void);
extern Eina_Bool eina_mempool_init(void);
extern Eina_Bool eina_list_init(void);

#define MAX_EVENTS   16

extern Eina_Spinlock      _eina_debug_thread_lock;
extern pthread_t          _eina_debug_thread_mainloop;
extern volatile pthread_t *_eina_debug_thread_active;
extern volatile int       _eina_debug_thread_active_num;

Eina_Semaphore _eina_debug_monitor_return_sem;

static Eina_Bool _monitor_thread_runs = EINA_FALSE;
static pthread_t _monitor_thread;

// _bt_buf[0] is always for mainloop, 1 + is for extra threads
static void             ***_bt_buf;
static int                *_bt_buf_len;
static struct timespec    *_bt_ts;
static int                *_bt_cpu;

static Eina_Bool _local_reconnect_enabled = EINA_TRUE;
static Eina_Debug_Session *main_session = NULL;
static Eina_List *sessions = NULL;
static int _epfd = 0, _listening_fd = 0;
static Eina_Debug_Connect_Cb _server_conn_cb = NULL;
static Eina_Debug_Disconnect_Cb _server_disc_cb = NULL;

static uint32_t _module_init_opcode = EINA_DEBUG_OPCODE_INVALID;

/* Used by trace timer */
static double _trace_t0 = 0.0;

// some state for debugging
static unsigned int poll_time = 0;
static Eina_Debug_Timer_Cb poll_timer_cb = NULL;

typedef struct
{
   const Eina_Debug_Opcode *ops;
   Eina_Debug_Opcode_Status_Cb status_cb;
} _opcode_reply_info;

struct _Eina_Debug_Session
{
   Eina_Debug_Cb *cbs;
   Eina_List *opcode_reply_infos;
   Eina_Debug_Dispatch_Cb dispatch_cb;
   Eina_Debug_Encode_Cb encode_cb;
   Eina_Debug_Decode_Cb decode_cb;
   unsigned int cbs_length;
   int fd;
};

struct _Eina_Debug_Client
{
   Eina_Debug_Session *session;
   int cid;
};

Eina_Debug_Session *_global_session = NULL;

EAPI int
eina_debug_session_send(Eina_Debug_Client *dest, uint32_t op, void *data, int size)
{
   if (!dest) return -1;
   Eina_Debug_Session *session = eina_debug_client_session_get(dest);

   if(!session) session = main_session;

   if (!session) return -1;
   // send protocol packet. all protocol is an int for size of packet then
   // included in that size (so a minimum size of 4) is a 4 byte opcode
   // (all opcodes are 4 bytes as a string of 4 chars), then the real
   // message payload as a data blob after that
   unsigned char *buf = alloca(sizeof(Eina_Debug_Packet_Header) + size);
   Eina_Debug_Packet_Header *hdr = (Eina_Debug_Packet_Header *)buf;
   hdr->size = size + sizeof(Eina_Debug_Packet_Header) - sizeof(uint32_t);
   hdr->opcode = op;
   hdr->cid = eina_debug_client_id_get(dest);
   if (size > 0) memcpy(buf + sizeof(Eina_Debug_Packet_Header), data, size);
   return send(session->fd, buf, hdr->size + sizeof(uint32_t), MSG_NOSIGNAL);
}

static void
_eina_debug_monitor_service_greet(Eina_Debug_Session *session)
{
   // say hello to our debug daemon - tell them our PID and protocol
   // version we speak
   int size = 8 + (_my_app_name ? strlen(_my_app_name) : 0) + 1;
   unsigned char *buf = alloca(size);
   int version = 1; // version of protocol we speak
   int pid = getpid();
   memcpy(buf + 0, &version, 4);
   memcpy(buf + 4, &pid, 4);
   if (_my_app_name)
      memcpy(buf + 8, _my_app_name, strlen(_my_app_name) + 1);
   else
      buf[8] = '\0';
   Eina_Debug_Client *cl = eina_debug_client_new(session, 0);
   eina_debug_session_send(cl, EINA_DEBUG_OPCODE_HELLO, buf, size);
   eina_debug_client_free(cl);
}

static int
_eina_debug_session_receive(Eina_Debug_Session *session, unsigned char **buffer)
{
   uint32_t size;
   int rret;

   if (!session) return -1;
   // get size of packet
   rret = recv(session->fd, (void *)&size, sizeof(uint32_t), 0);
   if (rret == sizeof(uint32_t))
     {
        // allocate a buffer for real payload + header - size variable size
        *buffer = malloc(size + sizeof(uint32_t));
        if (*buffer)
          {
             memcpy(*buffer, &size, sizeof(uint32_t));
             // get payload - blocking!!!!
             rret = read(session->fd, *buffer + sizeof(uint32_t), size);
             if (rret != (int)size)
               {
                  // we didn't get payload as expected - error on
                  // comms
                  fprintf(stderr,
                        "EINA DEBUG ERROR: "
                        "Invalid payload+header read of %i\n", rret);
                  free(*buffer);
                  *buffer = NULL;
                  return -1;
               }
             // return payload size (< 0 is an error)
             return size;
          }
        else
          {
             // we couldn't allocate memory for payloa buffer
             // internal memory limit error
             fprintf(stderr,
                   "EINA DEBUG ERROR: "
                   "Cannot allocate %u bytes for op\n", (unsigned int)size);
             return -1;
          }
     }
   if (rret)
      fprintf(stderr,
            "EINA DEBUG ERROR: "
            "Invalid size read %i != %lu\n", rret, sizeof(uint32_t));

   return -1;
}

EAPI void
eina_debug_reconnect_set(Eina_Bool reconnect)
{
   _local_reconnect_enabled = reconnect;
}

EAPI Eina_Debug_Session *
eina_debug_session_new()
{
   Eina_Debug_Session *session = calloc(1, sizeof(*session));
   session->dispatch_cb = eina_debug_dispatch;
   eina_spinlock_take(&_eina_debug_lock);
   //check if already appended to session list
   sessions = eina_list_append(sessions, session);
   eina_spinlock_release(&_eina_debug_lock);
   return session;
}

/*
 * Used only in the daemon. when set each session will use the global
 * session opcodes.
 */
EAPI void
eina_debug_session_global_use(Eina_Debug_Dispatch_Cb disp_cb)
{
   if(!_global_session)
      _global_session = eina_debug_session_new();
   _global_session->dispatch_cb = disp_cb ? disp_cb : eina_debug_dispatch;
}

EAPI void
eina_debug_session_free(Eina_Debug_Session *session)
{
   if (!session) return;
   eina_spinlock_take(&_eina_debug_lock);
   sessions = eina_list_remove(sessions, session);
   eina_spinlock_release(&_eina_debug_lock);

   _opcode_reply_info *info = NULL;

   EINA_LIST_FREE(session->opcode_reply_infos, info)
      free(info);

   free(session->cbs);
   free(session);
}

EAPI void
eina_debug_session_dispatch_override(Eina_Debug_Session *session, Eina_Debug_Dispatch_Cb disp_cb)
{
   if (!session) session = main_session;
   if (!session) return;
   if (!disp_cb) disp_cb = eina_debug_dispatch;
   session->dispatch_cb = disp_cb;
}

static void
_sessions_free(void)
{
   while (sessions)
     {
        Eina_Debug_Session *session = eina_list_data_get(sessions);
        close(session->fd);
        eina_debug_session_free(session);
     }
}

static Eina_Debug_Session *
_session_find_by_fd(int fd)
{
   Eina_List *l;
   Eina_Debug_Session *session;

   EINA_LIST_FOREACH(sessions, l, session)
      if(session->fd == fd)
         return session;

   return NULL;
}

void
eina_debug_session_fd_attach(Eina_Debug_Session *session, int fd)
{
   session->fd = fd;

   struct epoll_event event;
   event.data.fd = fd;
   event.events = EPOLLIN ;
   int ret = epoll_ctl (_epfd, EPOLL_CTL_ADD, fd, &event);
   if (ret) perror ("epoll_ctl");
}

static void
_session_fd_unattach(Eina_Debug_Session *session)
{
   int ret = epoll_ctl (_epfd, EPOLL_CTL_DEL, session->fd, NULL);
   if (ret) perror ("epoll_ctl");
   close(session->fd);
}

// a backtracer that uses libunwind to do the job
static inline int
_eina_debug_unwind_bt(void **bt, int max)
{
   unw_cursor_t cursor;
   unw_context_t uc;
   unw_word_t p;
   int total;

   // create a context for unwinding
   unw_getcontext(&uc);
   // begin our work
   unw_init_local(&cursor, &uc);
   // walk up each stack frame until there is no more, storing it
   for (total = 0; (unw_step(&cursor) > 0) && (total < max); total++)
     {
        unw_get_reg(&cursor, UNW_REG_IP, &p);
        bt[total] = (void *)p;
     }
   // return our total backtrace stack size
   return total;
}

// this signal handler is called inside each and every thread when the
// thread gets a signal via pthread_kill(). this causes the thread to
// stop here inside this handler and "do something" then when this returns
// resume whatever it was doing like any signal handler
static void
_eina_debug_signal(int sig EINA_UNUSED,
                   siginfo_t *si EINA_UNUSED,
                   void *foo EINA_UNUSED)
{
   int i, slot = 0;
   pthread_t self = pthread_self();
   clockid_t cid;

   // find which slot in the array of threads we have so we store info
   // in the correct slot for us
   if (self != _eina_debug_thread_mainloop)
     {
        for (i = 0; i < _eina_debug_thread_active_num; i++)
          {
             if (self == _eina_debug_thread_active[i])
               {
                  slot = i + 1;
                  goto found;
               }
          }
        // we couldn't find out thread reference! help!
        fprintf(stderr, "EINA DEBUG ERROR: can't find thread slot!\n");
        eina_semaphore_release(&_eina_debug_monitor_return_sem, 1);
        return;
     }
found:
   // store thread info like what cpu core we are on now (not reliable
   // but hey - better than nothing), the amount of cpu time total
   // we have consumed (it's cumulative so subtracing deltas can give
   // you an average amount of cpu time consumed between now and the
   // previous time we looked) and also a full backtrace
   _bt_cpu[slot] = sched_getcpu();
   pthread_getcpuclockid(self, &cid);
   clock_gettime(cid, &(_bt_ts[slot]));
   _bt_buf_len[slot] = _eina_debug_unwind_bt(_bt_buf[slot], EINA_MAX_BT);
   // now wake up the monitor to let them know we are done collecting our
   // backtrace info
   eina_semaphore_release(&_eina_debug_monitor_return_sem, 1);
}

// we shall sue SIGPROF as out signal for pausing threads and having them
// dump a backtrace for polling based profiling
#define SIG SIGPROF

// a quick and dirty local time point getter func - not portable
static inline double
get_time(void)
{
   struct timeval timev;
   gettimeofday(&timev, NULL);
   return (double)timev.tv_sec + (((double)timev.tv_usec) / 1000000.0);
}

static void
_eina_debug_collect_bt(pthread_t pth)
{
   // this async signals the thread to switch to the deebug signal handler
   // and collect a backtrace and other info from inside the thread
   pthread_kill(pth, SIG);
}

static Eina_Bool
_trace_cb()
{
   static int bts = 0;
   int i;
   if (!_trace_t0) _trace_t0 = get_time();
   // take a lock on grabbing thread debug info like backtraces
   eina_spinlock_take(&_eina_debug_thread_lock);
   // reset our "stack" of memory se use to dump thread info into
   _eina_debug_chunk_tmp_reset();
   // get an array of pointers for the backtrace array for main + th
   _bt_buf = _eina_debug_chunk_tmp_push
      ((1 + _eina_debug_thread_active_num) * sizeof(void *));
   if (!_bt_buf) goto err;
   // get an array of pointers for the timespec array for mainloop + th
   _bt_ts = _eina_debug_chunk_tmp_push
      ((1 + _eina_debug_thread_active_num) * sizeof(struct timespec));
   if (!_bt_ts) goto err;
   // get an array of pointers for the cpuid array for mainloop + th
   _bt_cpu = _eina_debug_chunk_tmp_push
      ((1 + _eina_debug_thread_active_num) * sizeof(int));
   if (!_bt_cpu) goto err;
   // now get an array of void pts for mainloop bt
   _bt_buf[0] = _eina_debug_chunk_tmp_push(EINA_MAX_BT * sizeof(void *));
   if (!_bt_buf[0]) goto err;
   // get an array of void ptrs for each thread we know about for bt
   for (i = 0; i < _eina_debug_thread_active_num; i++)
     {
        _bt_buf[i + 1] = _eina_debug_chunk_tmp_push(EINA_MAX_BT * sizeof(void *));
        if (!_bt_buf[i + 1]) goto err;
     }
   // get an array of ints to stor the bt len for mainloop + threads
   _bt_buf_len = _eina_debug_chunk_tmp_push
      ((1 + _eina_debug_thread_active_num) * sizeof(int));
   // collect bt from the mainloop - always there
   _eina_debug_collect_bt(_eina_debug_thread_mainloop);
   // now collect per thread
   for (i = 0; i < _eina_debug_thread_active_num; i++)
      _eina_debug_collect_bt(_eina_debug_thread_active[i]);
   // we're done probing. now collec all the "i'm done" msgs on the
   // semaphore for every thread + mainloop
   for (i = 0; i < (_eina_debug_thread_active_num + 1); i++)
      eina_semaphore_lock(&_eina_debug_monitor_return_sem);
   // we now have gotten all the data from all threadd + mainloop.
   // we can process it now as we see fit, so release thread lock
   //// XXX: some debug so we can see the bt's we collect - will go
   //                  for (i = 0; i < (_eina_debug_thread_active_num + 1); i++)
   //                    {
   //                       _eina_debug_dump_fhandle_bt(stderr, _bt_buf[i], _bt_buf_len[i]);
   //                    }
err:
   eina_spinlock_release(&_eina_debug_thread_lock);
   //// XXX: some debug just to see how well we perform - will go
   bts++;
   if (bts >= 10000)
     {
        double t;
        t = get_time();
        fprintf(stderr, "%1.5f bt's per sec\n", (double)bts / (t - _trace_t0));
        _trace_t0 = t;
        bts = 0;
     }
   return EINA_TRUE;
}

// profiling on with poll time gap as uint payload
static Eina_Bool
_eina_debug_prof_on_cb(Eina_Debug_Client *src EINA_UNUSED, void *buffer, int size)
{
   unsigned int time;
   if (size >= 4)
     {
        memcpy(&time, buffer, 4);
        _trace_t0 = 0.0;
        eina_debug_timer_add(time, _trace_cb);
     }
   return EINA_TRUE;
}

static Eina_Bool
_eina_debug_prof_off_cb(Eina_Debug_Client *src EINA_UNUSED, void *buffer EINA_UNUSED, int size EINA_UNUSED)
{
   eina_debug_timer_add(0, NULL);
   return EINA_TRUE;
}

typedef struct {
     Eina_Module *handle;
     int (*init)(void);
     int (*shutdown)(void);
} _module_info;

#define _LOAD_SYMBOL(cls_struct, pkg, sym) \
   do \
     { \
        char func_name[1024]; \
        sprintf(func_name, "%s_debug_" #sym, pkg); \
        (cls_struct).sym = eina_module_symbol_get((cls_struct).handle, func_name); \
        if (!(cls_struct).sym) \
          { \
             printf("Failed loading symbol '%s' from the library.\n", func_name); \
             eina_module_free((cls_struct).handle); \
             (cls_struct).handle = NULL; \
             return EINA_FALSE; \
          } \
     } \
   while (0)

static Eina_Bool
_module_init_cb(Eina_Debug_Client *src, void *buffer, int size)
{
   _module_info minfo;
   if (size <= 0) return EINA_FALSE;
   const char *module_name = buffer;
   char module_path[1024];
   printf("Init module %s\n", module_name);
   sprintf(module_path, PACKAGE_LIB_DIR "/lib%s_debug.so", module_name);
   minfo.handle = eina_module_new(module_path);
   if (!minfo.handle || !eina_module_load(minfo.handle))
     {
        printf("Failed loading debug module %s.\n", module_name);
        if (minfo.handle) eina_module_free(minfo.handle);
        minfo.handle = NULL;
        return EINA_TRUE;
     }

   _LOAD_SYMBOL(minfo, module_name, init);
   _LOAD_SYMBOL(minfo, module_name, shutdown);

   minfo.init();

   eina_debug_session_send(src, _module_init_opcode, buffer, size);
   return EINA_TRUE;
}

static Eina_Bool
_module_shutdown_cb(Eina_Debug_Client *src EINA_UNUSED, void *buffer EINA_UNUSED, int size EINA_UNUSED)
{
   return EINA_TRUE;
}

static const Eina_Debug_Opcode _EINA_DEBUG_MONITOR_OPS[] = {
       {"PLON", NULL, &_eina_debug_prof_on_cb},
       {"PLOF", NULL, &_eina_debug_prof_off_cb},
       {"Module/Init", &_module_init_opcode, &_module_init_cb},
       {"Module/Shutdown", NULL, &_module_shutdown_cb},
       {NULL, NULL, NULL}
};

static void
_opcodes_register(void)
{
   eina_debug_opcodes_register(NULL, _EINA_DEBUG_MONITOR_OPS, NULL);
}

/* Expecting pointer of ops followed by list of uint32's */
static Eina_Bool
_callbacks_register_cb(Eina_Debug_Client *cl, void *buffer, int size)
{
   Eina_Debug_Session *session = eina_debug_client_session_get(cl);
   _opcode_reply_info *info = NULL;

   uint64_t info_64;
   memcpy(&info_64, buffer, sizeof(uint64_t));
   info = (_opcode_reply_info *)info_64;

   if (!info) return EINA_FALSE;

   uint32_t *os = (uint32_t *)((unsigned char *)buffer + sizeof(uint64_t));
   unsigned int count = (size - sizeof(uint64_t)) / sizeof(uint32_t);

   unsigned int i;

   for (i = 0; i < count; i++)
     {
        if (info->ops[i].opcode_id) *(info->ops[i].opcode_id) = os[i];
        eina_debug_static_opcode_register(session, os[i], info->ops[i].cb);
     }
   if (info->status_cb) info->status_cb(EINA_TRUE);

   return EINA_TRUE;
}

static void
_opcodes_registration_send(Eina_Debug_Session *session,
      _opcode_reply_info *info)
{
   unsigned char *buf;

   int count = 0;
   int size = sizeof(uint64_t);

   if(!session) session = main_session;
   if(!session) return;

   while(info->ops[count].opcode_name)
     {
        size += strlen(info->ops[count].opcode_name) + 1;
        count++;
     }

   buf = alloca(size);

   uint64_t info_64 = (uint64_t)info;
   memcpy(buf, &info_64, sizeof(uint64_t));
   int size_curr = sizeof(uint64_t);

   count = 0;
   while(info->ops[count].opcode_name)
     {
        int len = strlen(info->ops[count].opcode_name) + 1;
        memcpy(buf + size_curr, info->ops[count].opcode_name, len);
        size_curr += len;
        count++;
     }

   Eina_Debug_Client *cl = eina_debug_client_new(session, 0);
   eina_debug_session_send(cl,
         EINA_DEBUG_OPCODE_REGISTER,
         buf,
         size);
   eina_debug_client_free(cl);
}

static void
_opcodes_register_all(Eina_Debug_Session *session)
{
   Eina_List *l;
   _opcode_reply_info *info = NULL;

   eina_debug_static_opcode_register(session,
         EINA_DEBUG_OPCODE_REGISTER, _callbacks_register_cb);
   EINA_LIST_FOREACH(session->opcode_reply_infos, l, info)
        _opcodes_registration_send(session, info);;
}

static void
_opcodes_unregister_all(Eina_Debug_Session *session)
{
   free(session->cbs);
   session->cbs_length = 0;
   session->cbs = NULL;

   Eina_List *l;
   _opcode_reply_info *info = NULL;

   EINA_LIST_FOREACH(session->opcode_reply_infos, l, info)
     {
        const Eina_Debug_Opcode *op = info->ops;
        while(!op->opcode_name)
          {
             if (op->opcode_id) *(op->opcode_id) = EINA_DEBUG_OPCODE_INVALID;
             op++;
          }
        if (info->status_cb) info->status_cb(EINA_FALSE);
     }
}

static const char *
_socket_home_get()
{
   // get possible debug daemon socket directory base
   const char *dir = getenv("XDG_RUNTIME_DIR");
   if (!dir) dir = eina_environment_home_get();
   if (!dir) dir = eina_environment_tmp_get();
   return dir;
}

#define SERVER_PATH ".ecore"
#define SERVER_NAME "efl_debug"
#define SERVER_PORT 0

#define LENGTH_OF_SOCKADDR_UN(s) \
   (strlen((s)->sun_path) + (size_t)(((struct sockaddr_un *)NULL)->sun_path))

EAPI Eina_Bool
eina_debug_local_connect(Eina_Debug_Session *session)
{
   char buf[4096];
   int fd, socket_unix_len, curstate = 0;
   struct sockaddr_un socket_unix;

   if (!session) return EINA_FALSE;
   // try this socket file - it will likely be:
   //   ~/.ecore/efl_debug/0
   // or maybe
   //   /var/run/UID/.ecore/efl_debug/0
   // either way a 4k buffer should be ebough ( if it's not we're on an
   // insane system)
   snprintf(buf, sizeof(buf), "%s/%s/%s/%i", _socket_home_get(), SERVER_PATH, SERVER_NAME, SERVER_PORT);
   // create the socket
   fd = socket(AF_UNIX, SOCK_STREAM, 0);
   if (fd < 0) goto err;
   // set the socket to close when we exec things so they don't inherit it
   if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) goto err;
   // set up some socket options on addr re-use
   if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&curstate,
                  sizeof(curstate)) < 0)
     goto err;
   // sa that it's a unix socket and where the path is
   socket_unix.sun_family = AF_UNIX;
   strncpy(socket_unix.sun_path, buf, sizeof(socket_unix.sun_path));
   socket_unix_len = LENGTH_OF_SOCKADDR_UN(&socket_unix);
   // actually conenct to efl_debugd service
   if (connect(fd, (struct sockaddr *)&socket_unix, socket_unix_len) < 0)
      goto err;
   // we succeeded
   eina_debug_session_fd_attach(session, fd);
   _eina_debug_monitor_service_greet(session);
   _opcodes_register_all(session);
   return EINA_TRUE;
err:
   // some kind of connection failure here, so close a valid socket and
   // get out of here
   if (fd >= 0) close(fd);
   return EINA_FALSE;
}

EAPI Eina_Bool
eina_debug_server_launch(Eina_Debug_Connect_Cb conn_cb, Eina_Debug_Disconnect_Cb disc_cb)
{
   char buf[4096];
   int fd, socket_unix_len, curstate = 0;
   struct sockaddr_un socket_unix;
   struct epoll_event event = {0};
   mode_t mask = 0;

   sprintf(buf, "%s/%s", _socket_home_get(), SERVER_PATH);
   if (mkdir(buf, S_IRWXU) < 0 && errno != EEXIST)
     {
        perror("mkdir SERVER_PATH");
        goto err;
     }
   sprintf(buf, "%s/%s/%s", _socket_home_get(), SERVER_PATH, SERVER_NAME);
   if (mkdir(buf, S_IRWXU) < 0 && errno != EEXIST)
     {
        perror("mkdir SERVER_NAME");
        goto err;
     }
   sprintf(buf, "%s/%s/%s/%i", _socket_home_get(), SERVER_PATH, SERVER_NAME, SERVER_PORT);
   mask = umask(S_IRWXG | S_IRWXO);
   // create the socket
   fd = socket(AF_UNIX, SOCK_STREAM, 0);
   if (fd < 0) goto err;
   // set the socket to close when we exec things so they don't inherit it
   if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) goto err;
   // set up some socket options on addr re-use
   if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&curstate,
                  sizeof(curstate)) < 0)
     goto err;
   // sa that it's a unix socket and where the path is
   socket_unix.sun_family = AF_UNIX;
   strncpy(socket_unix.sun_path, buf, sizeof(socket_unix.sun_path));
   socket_unix_len = LENGTH_OF_SOCKADDR_UN(&socket_unix);
   unlink(socket_unix.sun_path);
   if (bind(fd, (struct sockaddr *)&socket_unix, socket_unix_len) < 0)
     {
        perror("ERROR on binding");
        goto err;
     }
   listen(fd, 5);
   _listening_fd = fd;
   _server_conn_cb = conn_cb;
   _server_disc_cb = disc_cb;
   event.data.fd = _listening_fd;
   event.events = EPOLLIN;
   epoll_ctl (_epfd, EPOLL_CTL_ADD, _listening_fd, &event);
   umask(mask);
   return EINA_TRUE;
err:
   if (mask) umask(mask);
   if (fd >= 0) close(fd);
   return EINA_FALSE;
}

EAPI Eina_Bool
eina_debug_timer_add(unsigned int timeout_ms, Eina_Debug_Timer_Cb cb)
{
   poll_time = timeout_ms;
   poll_timer_cb = cb;
   return EINA_TRUE;
}

static void
_signal_init(void)
{
   struct sigaction sa;

   // set up signal handler for our profiling signal - eevery thread should
   // obey this (this is the case on linux - other OSs may vary)
   sa.sa_sigaction = _eina_debug_signal;
   sa.sa_flags = SA_RESTART | SA_SIGINFO;
   sigemptyset(&sa.sa_mask);
   if (sigaction(SIG, &sa, NULL) != 0)
     fprintf(stderr, "EINA DEBUG ERROR: Can't set up sig %i handler!\n", SIG);
}

// this is a DEDICATED debug thread to monitor the application so it works
// even if the mainloop is blocked or the app otherwise deadlocked in some
// way. this is an alternative to using external debuggers so we can get
// users or developers to get useful information about an app at all times
static void *
_monitor(void *_data EINA_UNUSED)
{
   int ret;
   //try to connect to main session ( will be set if main session diconnect)
   Eina_Bool main_session_reconnect = EINA_TRUE;

   struct epoll_event events[MAX_EVENTS];

   //register opcodes for monitor - should be only once
   _opcodes_register();
   // set up our profile signal handler
   _signal_init();

   // sit forever processing commands or timeouts in the debug monitor
   // thread - this is separate to the rest of the app so it shouldn't
   // impact the application specifically
   for (;;)
     {
        int timeout = -1; //in milliseconds
        //try to reconnect to main session if disconnected
        if(_local_reconnect_enabled && main_session_reconnect)
          {
             if (eina_debug_local_connect(main_session))
                main_session_reconnect = EINA_FALSE;
          }

        // if we are in a polling mode then set up a timeout and wait for it
        if (poll_time)
          {
             timeout = poll_time;
          }
        else
          {
             //if we are not in polling mode set a timeout
             //so we could reconnect to main_session
             if(main_session_reconnect)
                timeout = 2000;
          }
        ret = epoll_wait (_epfd, events, MAX_EVENTS, timeout);

        // if the fd for debug daemon says it's alive, process it
        if (ret)
          {
             int i;
             //check which fd are set/ready for read
             for (i = 0; i < ret; i++)
               {
                  if (events[i].events & EPOLLIN)
                    {
                       int size;
                       unsigned char *buffer;

                       //someone wants to connect
                       if(events[i].data.fd == _listening_fd)
                         {
                            int new_fd = accept(_listening_fd, NULL, NULL);
                            if (new_fd <= 0) perror("Accept");
                            else
                              {
                                 Eina_Debug_Session *new_fd_session = eina_debug_session_new();
                                 eina_debug_session_fd_attach(new_fd_session, new_fd);
                                 if (_server_conn_cb) _server_conn_cb(new_fd_session);
                              }
                            continue;
                         }

                       Eina_Debug_Session *session =
                          _session_find_by_fd(events[i].data.fd);
                       if(session)
                          {
                             size = _eina_debug_session_receive(session, &buffer);
                             // if not negative - we have a real message
                             if (size >= 0)
                               {
                                  Eina_Debug_Session *_disp_session = _global_session ? _global_session : session;
                                  if(!_disp_session->dispatch_cb(session, buffer))
                                    {
                                       // something we don't understand
                                       fprintf(stderr,
                                             "EINA DEBUG ERROR: "
                                             "Uunknown command \n");
                                    }
                               }
                             // major failure on debug daemon control fd - get out of here.
                             //   else goto fail;
                             //if its main session we try to reconnect
                             else
                               {
                                  if(session == main_session)
                                     main_session_reconnect = EINA_TRUE;

                                  _session_fd_unattach(session);
                                  _opcodes_unregister_all(session);
                                  session->fd = -1;
                                  if (_server_disc_cb) _server_disc_cb(session);
                                  //TODO if its not main session we will tell the main_loop
                                  //that it disconneted
                               }
                          }
                    }
               }
          }
        else
           if (poll_time && poll_timer_cb)
             {
                if (!poll_timer_cb()) poll_time = 0;
             }
     }
   // free sessions and close fd's
   _sessions_free();
   return NULL;
}

// start up the debug monitor if we haven't already
static void
_thread_start()
{
   int err;

   // if it's already running - we're good.
   if (_monitor_thread_runs) return;
   main_session = eina_debug_session_new();
   _epfd = epoll_create (MAX_EVENTS);
   // create debug monitor thread
   err = pthread_create(&_monitor_thread, NULL, _monitor, NULL);
   if (err != 0)
     {
        fprintf(stderr, "EINA DEBUG ERROR: Can't create debug thread!\n");
        abort();
     }
   else _monitor_thread_runs = EINA_TRUE;
}

EAPI Eina_Debug_Client *
eina_debug_client_new(Eina_Debug_Session *session, int id)
{
   Eina_Debug_Client *cl = calloc(1, sizeof(*cl));
   cl->session = session;
   cl->cid = id;
   return cl;
}

EAPI Eina_Debug_Session *
eina_debug_client_session_get(Eina_Debug_Client *cl)
{
   return cl->session;
}

EAPI int
eina_debug_client_id_get(Eina_Debug_Client *cl)
{
   return cl->cid;
}

EAPI void
eina_debug_client_free(Eina_Debug_Client *cl)
{
   free(cl);
}

EAPI void
eina_debug_static_opcode_register(Eina_Debug_Session *session,
      uint32_t op_id, Eina_Debug_Cb cb)
{
   if(_global_session) session = _global_session;

   if(session->cbs_length < op_id + 1)
     {
        unsigned int i = session->cbs_length;
        session->cbs_length = op_id + 16;
        session->cbs = realloc(session->cbs, session->cbs_length * sizeof(Eina_Debug_Cb));
        for(; i < session->cbs_length; i++)
           session->cbs[i] = NULL;
     }
   session->cbs[op_id] = cb;
}

/*
 * Sends to daemon:
 * - Pointer to ops: returned in the response to determine which opcodes have been added
 * - List of opcode names seperated by \0
 */
EAPI void
eina_debug_opcodes_register(Eina_Debug_Session *session, const Eina_Debug_Opcode ops[],
      Eina_Debug_Opcode_Status_Cb status_cb)
{
   Eina_Debug_Session *opcodes_session = session;
   if(!opcodes_session) opcodes_session = main_session;
   if(_global_session) opcodes_session = _global_session;
   if(!opcodes_session) return;

   _opcode_reply_info *info = malloc(sizeof(*info));
   info->ops = ops;
   info->status_cb = status_cb;

   opcodes_session->opcode_reply_infos = eina_list_append(
         opcodes_session->opcode_reply_infos, info);

   //send only if session's fd connected, if not -  it will be sent when connected
   if(opcodes_session && opcodes_session->fd)
      _opcodes_registration_send(opcodes_session, info);
}

Eina_Bool
eina_debug_dispatch(Eina_Debug_Session *session, void *buffer)
{
   Eina_Debug_Packet_Header *hdr =  buffer;
   uint32_t opcode = hdr->opcode;
   Eina_Debug_Cb cb = NULL;
   Eina_Debug_Session *opcodes_session = _global_session ? _global_session : session;

   if(opcode < opcodes_session->cbs_length) cb = opcodes_session->cbs[opcode];

   if (cb)
     {
        Eina_Debug_Client *cl = eina_debug_client_new(session, hdr->cid);
        cb(cl, (unsigned char *)buffer + sizeof(*hdr), hdr->size + sizeof(uint32_t) - sizeof(*hdr));
        eina_debug_client_free(cl);
        free(buffer);
        return EINA_TRUE;
     }
   free(buffer);
   return EINA_FALSE;
}

static void *
_base_16_encode_cb(const void *data, int src_size, int *dest_size)
{
   const char *src = data;
   *dest_size = src_size * 2;
   char *dest = malloc(*dest_size);
   int i;
   for (i = 0; i < src_size; i++)
     {
        dest[(i << 1) + 0] = ((src[i] & 0xF0) >> 4) + 0x40;
        dest[(i << 1) + 1] = ((src[i] & 0x0F) >> 0) + 0x40;
     }
   return dest;
}

static void *
_base_16_decode_cb(const void *data, int src_size, int *dest_size)
{
   const char *src = data;
   *dest_size = src_size / 2;
   char *dest = malloc(*dest_size);
   int i, j;
   for (i = 0, j = 0; j < src_size; j += 2)
     {
        if ((src[j] & 0xF0) == 0x40 && (src[j + 1] & 0xF0) == 0x40)
           dest[i++] = ((src[j] - 0x40) << 4) | ((src[j + 1] - 0x40));
        else goto error;
     }
   goto end;
error:
   free(dest);
   dest = NULL;
   *dest_size = 0;
end:
   return dest;
}

EAPI void eina_debug_session_codec_hooks_add(Eina_Debug_Session *session, Eina_Debug_Encode_Cb enc_cb, Eina_Debug_Decode_Cb dec_cb)
{
   if (!session) return;
   session->encode_cb = enc_cb;
   session->decode_cb = dec_cb;
}

EAPI void eina_debug_session_basic_codec_add(Eina_Debug_Session *session, Eina_Debug_Basic_Codec codec)
{
   switch(codec)
     {
      case EINA_DEBUG_CODEC_BASE_16:
         eina_debug_session_codec_hooks_add(session, _base_16_encode_cb, _base_16_decode_cb);
         break;
      default:
         fprintf(stderr, "EINA DEBUG ERROR: Bad basic encoding\n");
     }
}

Eina_Bool
eina_debug_init(void)
{
   pthread_t self;

   // if already inbitted simply release our lock that we may have locked on
   // shutdown if we are re-initted again in the same process
   if (_inited)
     {
        eina_spinlock_release(&_eina_debug_thread_lock);
        return EINA_TRUE;
     }
   // mark as initted
   _inited = EINA_TRUE;
   eina_module_init();
   eina_mempool_init();
   eina_list_init();
#ifdef __linux__
   extern char *__progname;
   _my_app_name = __progname;
#endif
   // For Windows support GetModuleFileName can be used
   // set up thread things
   eina_spinlock_new(&_eina_debug_lock);
   eina_spinlock_new(&_eina_debug_thread_lock);
   eina_semaphore_new(&_eina_debug_monitor_return_sem, 0);
   self = pthread_self();
   _eina_debug_thread_mainloop_set(&self);
#if defined(HAVE_GETUID) && defined(HAVE_GETEUID)
   // if we are setuid - don't debug!
   if (getuid() != geteuid()) return EINA_TRUE;
#endif
   // if someone uses the EFL_NODEBUG env var - do not do debugging. handy
   // for when this debug code is buggy itself
   if (getenv("EFL_NODEBUG")) return EINA_TRUE;
   // start the monitor thread
   _thread_start();
   return EINA_TRUE;
}

Eina_Bool
eina_debug_shutdown(void)
{
   eina_spinlock_take(&_eina_debug_thread_lock);
   // yes - we never free on shutdown - this is because the monitor thread
   // never exits. this is not a leak - we intend to never free up any
   // resources here because they are allocated once only ever.
   return EINA_TRUE;
}
