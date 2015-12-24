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

#include <Eina.h>
#include <Ecore.h>

# ifdef HAVE_CONFIG_H
#  include "config.h"
# endif

#define EXTRACT(_buf, pval, sz) \
{ \
   memcpy(pval, _buf, sz); \
   _buf += sz; \
}

static int _cl_stat_reg_opcode = EINA_DEBUG_OPCODE_INVALID;
static int _cid_from_pid_opcode = EINA_DEBUG_OPCODE_INVALID;
static int _prof_on_opcode = EINA_DEBUG_OPCODE_INVALID;
static int _prof_off_opcode = EINA_DEBUG_OPCODE_INVALID;
static int _evlog_on_opcode = EINA_DEBUG_OPCODE_INVALID;
static int _evlog_off_opcode = EINA_DEBUG_OPCODE_INVALID;

typedef struct
{
   int *opcode; /* address to the opcode */
   void *buffer;
   int size;
} _pending_request;

static Eina_List *_pending = NULL;
static Eina_Debug_Session *_session = NULL;

static int _cid = 0;

static int my_argc = 0;
static char **my_argv = NULL;

static void
_consume()
{
   if (!_pending)
     {
        ecore_main_loop_quit();
        return;
     }
   _pending_request *req = eina_list_data_get(_pending);
   _pending = eina_list_remove(_pending, req);

   eina_debug_session_send(_session, _cid, *(req->opcode), req->buffer, req->size);

   free(req->buffer);
   free(req);
}

static void
_pending_add(int *opcode, void *buffer, int size)
{
   _pending_request *req = malloc(sizeof(*req));
   req->opcode = opcode;
   req->buffer = buffer;
   req->size = size;
   _pending = eina_list_append(_pending, req);
}

static Eina_Bool
_cid_get_cb(Eina_Debug_Session *session EINA_UNUSED, int cid EINA_UNUSED, void *buffer, int size EINA_UNUSED)
{
   _cid = *(int *)buffer;
   _consume();
   return EINA_TRUE;
}

static Eina_Bool
_clients_info_added_cb(Eina_Debug_Session *session EINA_UNUSED, int src EINA_UNUSED, void *buffer, int size)
{
   char *buf = buffer;
   while(size)
     {
        int cid, pid, len;
        EXTRACT(buf, &cid, sizeof(int));
        EXTRACT(buf, &pid, sizeof(int));
        printf("Added: CID: %d - PID: %d - Name: %s\n", cid, pid, buf);
        len = strlen(buf) + 1;
        buf += len;
        size -= (2 * sizeof(int) + len);
     }
   _consume();
   return EINA_TRUE;
}

static Eina_Bool
_clients_info_deleted_cb(Eina_Debug_Session *session EINA_UNUSED, int src EINA_UNUSED, void *buffer, int size)
{
   char *buf = buffer;
   while(size)
     {
        int cid;
        EXTRACT(buf, &cid, sizeof(int));
        printf("Deleted: CID: %d\n", cid);
        size -= sizeof(int);
     }
   _consume();
   return EINA_TRUE;
}

static void
_args_handle(Eina_Bool flag)
{
   int i;
   if (!flag) exit(0);
   for (i = 1; i < my_argc;)
     {
        const char *op_str = my_argv[i++];
        if (!strcmp(op_str, "list"))
          {
             eina_debug_session_send(_session, 0, _cl_stat_reg_opcode, NULL, 0);
          }
        else if (i <= my_argc - 1)
          {
             int pid = atoi(my_argv[i++]);
             char *buf = NULL;
             eina_debug_session_send(_session, 0, _cid_from_pid_opcode, &pid, sizeof(int));
             printf("got %s %d\n", op_str, pid);
             if ((!strcmp(op_str, "pon")) && (i <= (my_argc - 1)))
               {
                  int freq = atoi(my_argv[i++]);
                  buf = malloc(sizeof(int));
                  memcpy(buf, &freq, sizeof(int));
                  _pending_add(&_prof_on_opcode, buf, sizeof(int));
               }
             else if (!strcmp(op_str, "poff"))
                _pending_add(&_prof_off_opcode, NULL, 0);
             else if (!strcmp(op_str, "evlogon"))
                _pending_add(&_evlog_on_opcode, NULL, 0);
             else if (!strcmp(op_str, "evlogoff"))
                _pending_add(&_evlog_off_opcode, NULL, 0);
          }
     }
}

static const Eina_Debug_Opcode ops[] =
{
     {"daemon/observer/client/register", &_cl_stat_reg_opcode,   NULL},
     {"daemon/observer/client_added",   NULL,                  &_clients_info_added_cb},
     {"daemon/observer/client_deleted", NULL,                  &_clients_info_deleted_cb},
     {"daemon/info/cid_from_pid",      &_cid_from_pid_opcode,  &_cid_get_cb},
     {"profiler/on",                   &_prof_on_opcode,       NULL},
     {"profiler/off",                  &_prof_off_opcode,      NULL},
     {"evlog/on",                      &_evlog_on_opcode,      NULL},
     {"evlog/off",                     &_evlog_off_opcode,     NULL},
     {NULL, NULL, NULL}
};

int
main(int argc, char **argv)
{
   eina_init();
   ecore_init();

   my_argc = argc;
   my_argv = argv;

   _session = eina_debug_session_new();
   if (!eina_debug_local_connect(_session))
     {
        fprintf(stderr, "ERROR: Cannot connect to debug daemon.\n");
        return -1;
     }
   eina_debug_opcodes_register(_session, ops, _args_handle);

   ecore_main_loop_begin();

   ecore_shutdown();
   eina_shutdown();
}
