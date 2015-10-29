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

#include "efl_debug_common.h"

# ifdef HAVE_CONFIG_H
#  include "config.h"
# endif

#define EXTRACT(_buf, pval, sz) \
{ \
   memcpy(pval, _buf, sz); \
   _buf += sz; \
}

static uint32_t _cl_stat_reg_opcode = EINA_DEBUG_OPCODE_INVALID;
static uint32_t _cid_from_pid_opcode = EINA_DEBUG_OPCODE_INVALID;
static uint32_t _poll_on_opcode = EINA_DEBUG_OPCODE_INVALID;
static uint32_t _poll_off_opcode = EINA_DEBUG_OPCODE_INVALID;
static uint32_t _evlog_on_opcode = EINA_DEBUG_OPCODE_INVALID;
static uint32_t _evlog_off_opcode = EINA_DEBUG_OPCODE_INVALID;

typedef struct
{
   uint32_t *opcode; /* address to the opcode */
   void *buffer;
   int size;
} _pending_request;

static Eina_List *_pending = NULL;
static Eina_Debug_Session *_session = NULL;

static uint32_t _cid = 0;

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

   Eina_Debug_Client *cl = eina_debug_client_new(_session, _cid);
   eina_debug_session_send(cl, *(req->opcode), req->buffer, req->size);
   eina_debug_client_free(cl);

   free(req->buffer);
   free(req);
}

static void
_pending_add(uint32_t *opcode, void *buffer, int size)
{
   _pending_request *req = malloc(sizeof(*req));
   req->opcode = opcode;
   req->buffer = buffer;
   req->size = size;
   _pending = eina_list_append(_pending, req);
}

static Eina_Bool
_cid_get_cb(Eina_Debug_Client *src EINA_UNUSED, void *buffer, int size EINA_UNUSED)
{
   _cid = *(uint32_t *)buffer;
   _consume();
   return EINA_TRUE;
}

static Eina_Bool
_clients_info_cb(Eina_Debug_Client *src EINA_UNUSED, void *buffer, int size)
{
   char *buf = buffer;
   while(size)
     {
        int cid, pid, len;
        EXTRACT(buf, &cid, sizeof(uint32_t));
        EXTRACT(buf, &pid, sizeof(uint32_t));
        printf("CID: %d - PID: %d - Name: %s\n", cid, pid, buf);
        len = strlen(buf) + 1;
        buf += len;
        size -= (2 * sizeof(uint32_t) + len);
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
        Eina_Debug_Client *cl = eina_debug_client_new(_session, 0);
        if (!strcmp(my_argv[i], "list"))
          {
             eina_debug_session_send(cl, _cl_stat_reg_opcode, NULL, 0);
             i++;
          }
        else if (i < my_argc - 1)
          {
             const char *op_str = my_argv[i++];
             uint32_t pid = atoi(my_argv[i++]);
             char *buf = NULL;
             eina_debug_session_send(cl, _cid_from_pid_opcode, &pid, sizeof(uint32_t));
             if ((!strcmp(op_str, "pon")) && (i < (my_argc - 2)))
               {
                  uint32_t freq = atoi(my_argv[i++]);
                  buf = malloc(sizeof(uint32_t));
                  memcpy(buf, &freq, sizeof(uint32_t));
                  _pending_add(&_poll_on_opcode, buf, sizeof(uint32_t));
               }
             else if (!strcmp(op_str, "poff"))
                _pending_add(&_poll_off_opcode, NULL, 0);
             else if (!strcmp(op_str, "evlogon"))
                _pending_add(&_evlog_on_opcode, NULL, 0);
             else if (!strcmp(op_str, "evlogoff"))
                _pending_add(&_evlog_off_opcode, NULL, 0);
          }
        eina_debug_client_free(cl);
     }
}

static const Eina_Debug_Opcode ops[] =
{
     {"daemon/client_status_register", &_cl_stat_reg_opcode,  NULL},
     {"daemon/client_added", NULL, &_clients_info_cb},
     {"daemon/cid_from_pid",  &_cid_from_pid_opcode,  &_cid_get_cb},
     {"poll/on",              &_poll_on_opcode,       NULL},
     {"poll/off",             &_poll_off_opcode,      NULL},
     {"evlog/on",             &_evlog_on_opcode,      NULL},
     {"evlog/off",            &_evlog_off_opcode,     NULL},
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
