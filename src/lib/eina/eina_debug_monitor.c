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

#include "eina_debug.h"
#include "eina_types.h"
#include "eina_evlog.h"
#include "eina_util.h"

#ifdef EINA_HAVE_DEBUG

#define DEBUG_SERVER ".ecore/efl_debug/0"

extern pthread_t            _eina_debug_thread_mainloop;
extern volatile pthread_t  *_eina_debug_thread_active;
extern volatile int         _eina_debug_thread_active_num;

int                    _eina_debug_monitor_service_fd = -1;
Eina_Semaphore         _eina_debug_monitor_return_sem;

static Eina_Bool       _monitor_thread_runs = EINA_FALSE;
static pthread_t       _monitor_thread;

// _bt_buf[0] is always for mainloop, 1 + is for extra threads
static void             ***_bt_buf;
static int                *_bt_buf_len;
static struct timespec    *_bt_ts;
static int                *_bt_cpu;

static Eina_Debug_Session *main_session = NULL;
static Eina_List *sessions = NULL;
//instead of doing FD_SET each time before select.
//we save in it in global variable and just copy it.
static fd_set global_rfds;
static int global_max_fd = 0;

// some state for debugging
static unsigned int   poll_time = 1000;
static Eina_Bool      poll_on = EINA_FALSE;
static Eina_Bool      poll_trace = EINA_FALSE;
static Eina_Bool      poll_cpu = EINA_FALSE;

EAPI Eina_Debug_Session *
eina_debug_session_new(void)
{
   Eina_Debug_Session *session = calloc(1, sizeof(Eina_Debug_Session));

   int i;
   for(i = 0; i < EINA_DEBUG_OPCODE_MAX; i++)
      session->cbs[i] = NULL;

   return session;
}

EAPI void
eina_debug_session_free(Eina_Debug_Session *session)
{
  free(session);
}

static void
_eina_debug_sessions_free(void)
{
   Eina_List *l;
   Eina_Debug_Session *session;

   EINA_LIST_FOREACH(sessions, l, session)
     {
        close(session->fd);
        eina_debug_session_free(main_session);
     }
   eina_list_free(sessions);
}

Eina_Debug_Session *
_eina_debug_get_main_session()
{
   return main_session;
}

static Eina_Debug_Session *
_eina_debug_session_find_by_fd(int fd)
{
   Eina_List *l;
   Eina_Debug_Session *session;

   EINA_LIST_FOREACH(sessions, l, session)
      if(session->fd == fd)
         return session;

   return NULL;
}

EAPI void
eina_debug_session_fd_attach(Eina_Debug_Session *session, int fd)
{
   eina_spinlock_take(&_eina_debug_thread_lock);

   session->fd = fd;

   //check if already appended to session list
   if(!_eina_debug_session_find_by_fd(fd))
      sessions = eina_list_append(sessions, session);

   FD_SET(session->fd, &global_rfds);

   if(session->fd > global_max_fd)
      global_max_fd = session->fd;

   eina_spinlock_release(&_eina_debug_thread_lock);
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

// profiling on with poll time gap as uint payload
static Eina_Bool
_eina_debug_prof_on_cb(Eina_Debug_Client *src EINA_UNUSED, void *buffer, int size)
{
   if (size >= 4) memcpy(&poll_time, buffer, 4);
   poll_on = EINA_TRUE;
   poll_trace = EINA_TRUE;
   return EINA_TRUE;
}

static Eina_Bool
_eina_debug_prof_off_cb(Eina_Debug_Client *src EINA_UNUSED, void *buffer EINA_UNUSED, int size EINA_UNUSED)
{
   poll_time = 1000;
   poll_on = EINA_FALSE;
   poll_trace = EINA_FALSE;
   return EINA_TRUE;
}

static const Eina_Debug_Opcode _EINA_DEBUG_MONITOR_OPS[] = {
       {"PLON", NULL, &_eina_debug_prof_on_cb},
       {"PLOF", NULL, &_eina_debug_prof_off_cb},
       {NULL, NULL, NULL}
};

void
_eina_debug_monitor_register_opcodes(void)
{
   eina_debug_opcodes_register(NULL, _EINA_DEBUG_MONITOR_OPS, NULL);
}

// this is a DEDICATED debug thread to monitor the application so it works
// even if the mainloop is blocked or the app otherwise deadlocked in some
// way. this is an alternative to using external debuggers so we can get
// users or developers to get useful information about an app at all times
static void *
_eina_debug_monitor(void *_data EINA_UNUSED)
{
   int bts = 0, ret;
   double t0, t;
   fd_set rfds, wfds, exfds;
   struct timeval tv = { 0, 0 };
   //try to connect to main session ( will be set if main session diconnect)
   Eina_Bool main_session_reconnect = EINA_FALSE;
   t0 = get_time();

   FD_ZERO(&global_rfds);
   eina_debug_session_fd_attach(main_session, main_session->fd);
   // sit forever processing commands or timeouts in the debug monitor
   // thread - this is separate to the rest of the app so it shouldn't
   // impact the application specifically
   for (;;)
     {
        int i;

        // set up data for select like read fd's

        FD_ZERO(&wfds);
        FD_ZERO(&exfds);
        rfds = global_rfds;
        // if we are in a polling mode then set up a timeout and wait for it
        if (poll_on)
          {
             tv.tv_sec = 0;
             tv.tv_usec = poll_time;
          }
        else
          {
             //if we are not in polling mode set a timeout so we could update the
             //fd vector and try to reconnect to main_session if needed
             tv.tv_sec = 2;
             tv.tv_usec = 0;
          }
        ret = select(global_max_fd + 1, &rfds, &wfds, &exfds, &tv);
        //try to reconnect to main session if disconnected
        if(main_session_reconnect)
          {
             int fd = _eina_debug_monitor_service_connect();
             if(fd)
               {
                  main_session->fd = fd;
                  main_session_reconnect = EINA_FALSE;
               }
          }
        // if the fd for debug daemon says it's alive, process it
        if (ret)
          {
             //check which fd are set/ready for read
             for (i = 0; i < FD_SETSIZE; ++i)
               {
                  if (FD_ISSET (i, &rfds))
                    {
                       int size;
                       unsigned char *buffer;
                       Eina_Debug_Session *session = _eina_debug_session_find_by_fd(i);
                       if(session)
                         {
                            size = _eina_debug_session_receive(session, &buffer);
                            // if not negative - we have a real message
                            if (size >= 0)
                              {
                                 // something we don't understand
                                 if(!eina_debug_dispatch(main_session, buffer))
                                    fprintf(stderr,
                                          "EINA DEBUG ERROR: "
                                          "Uunknown command \n");
                                 free(buffer);
                              }
                            // major failure on debug daemon control fd - get out of here.
                            //   else goto fail;
                            //if its main session we try to reconnect
                            else
                              {
                                 if(session == main_session)

                                    main_session_reconnect = EINA_TRUE;

                                 //TODO if its not main session we will tell the main_loop
                                 //that it disconneted

                              }

                         }
                    }
               }
          }

        if (poll_on)
          {
             if (poll_trace)
               {
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
                       t = get_time();
                       fprintf(stderr, "%1.5f bt's per sec\n", (double)bts / (t - t0));
                       t0 = t;
                       bts = 0;
                    }
               }
             if (poll_cpu)
               {
                  // XXX: opendir /proc/sefl/task
//                  eina_evlog("*cpustat", NULL, 0.0, cpustat);
               }
          }
     }
   // free sessions and close fd's
   _eina_debug_sessions_free();
   return NULL;
}

// start up the debug monitor if we haven't already
void
_eina_debug_monitor_thread_start(Eina_Debug_Session *session)
{
   int err;

   // if it's already running - we're good.
   if (_monitor_thread_runs) return;
   main_session = session;
   // create debug monitor thread
   err = pthread_create(&_monitor_thread, NULL, _eina_debug_monitor, NULL);
   if (err != 0)
     {
        fprintf(stderr, "EINA DEBUG ERROR: Can't create debug thread!\n");
        abort();
     }
   else _monitor_thread_runs = EINA_TRUE;
}

void
_eina_debug_monitor_signal_init(void)
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

static const char *
_socket_home_get()
{
   // get possible debug daemon socket directory base
   const char *dir = getenv("XDG_RUNTIME_DIR");
   if (!dir) dir = eina_environment_home_get();
   if (!dir) dir = eina_environment_tmp_get();
   return dir;
}

// connect to efl_debugd
int
_eina_debug_monitor_service_connect(void)
{
   char buf[4096];
   int fd, socket_unix_len, curstate = 0;
   struct sockaddr_un socket_unix;

   // try this socket file - it will likely be:
   //   ~/.ecore/efl_debug/0
   // or maybe
   //   /var/run/UID/.ecore/efl_debug/0
   // either way a 4k buffer should be ebough ( if it's not we're on an
   // insane system)
   snprintf(buf, sizeof(buf), "%s/%s", _socket_home_get(), DEBUG_SERVER);
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
#define LENGTH_OF_SOCKADDR_UN(s) \
   (strlen((s)->sun_path) + (size_t)(((struct sockaddr_un *)NULL)->sun_path))
   socket_unix_len = LENGTH_OF_SOCKADDR_UN(&socket_unix);
   // actually conenct to efl_debugd service
   if (connect(fd, (struct sockaddr *)&socket_unix, socket_unix_len) < 0)
     goto err;
   // we succeeded - store fd
   return fd;
err:
   // some kind of connection failure here, so close a valid socket and
   // get out of here
   if (fd >= 0) close(fd);
   return 0;
}

EAPI Eina_Debug_Session *
eina_debug_local_connect(void)
{
   Eina_Debug_Session *session = NULL;
   int fd = _eina_debug_monitor_service_connect();
   if (fd)
     {
        session = eina_debug_session_new();
        eina_debug_session_fd_attach(session, fd);
     }
   return session;
}

#endif
