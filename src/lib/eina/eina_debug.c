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
#include "eina_list.h"
#include "eina_mempool.h"

#ifdef EINA_HAVE_DEBUG

// access external thread data store in eina_debug_thread.c
extern pthread_t     _eina_debug_thread_mainloop;
extern pthread_t    *_eina_debug_thread_active;
extern int           _eina_debug_thread_active_num;

// yes - a global debug spinlock. i expect contention to be low for now, and
// when needed we can split this up into mroe locks to reduce contention when
// and if that day comes
Eina_Spinlock _eina_debug_lock;

// only init once
static Eina_Bool _inited = EINA_FALSE;
static Eina_Bool _reconnect = EINA_TRUE;

extern Eina_Bool eina_module_init(void);
extern Eina_Bool eina_mempool_init(void);
extern Eina_Bool eina_list_init(void);

Eina_Bool
eina_debug_init(void)
{
   pthread_t self;

   if(!_reconnect)
      return EINA_TRUE;
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
   // connect to our debug daemon
   int fd = _eina_debug_monitor_service_connect();
   // if we connected - set up the debug monitor properly
   if (fd)
     {
        Eina_Debug_Session *session = eina_debug_session_new();
        session->fd = fd;
        session->cbs[EINA_DEBUG_OPCODE_REGISTER] = _eina_debug_callbacks_register_cb;
        // say hello to the debug daemon
        _eina_debug_monitor_service_greet(session);
        //register opcodes for monitor
        _eina_debug_monitor_register_opcodes();
        // set up our profile signal handler
        _eina_debug_monitor_signal_init();
        // start the monitor thread
        _eina_debug_monitor_thread_start(session);
     }
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

EAPI void
eina_debug_set_reconnect(Eina_Bool reconnect)
{
   _reconnect = reconnect;
}

#else
Eina_Bool
eina_debug_init(void)
{
   return EINA_TRUE;
}

Eina_Bool
eina_debug_shutdown(void)
{
   return EINA_TRUE;
}
#endif
