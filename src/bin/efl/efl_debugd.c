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

#define STORE(_buf, pval, sz) \
{ \
   memcpy(_buf, pval, sz); \
   _buf += sz; \
}

typedef struct _Client Client;

struct _Client
{
   Eina_Debug_Session *session;
   char *app_name;
   unsigned char    *buf;
   unsigned int      buf_size;

   Ecore_Timer      *evlog_fetch_timer;
   int               evlog_on;
   FILE             *evlog_file;

   int               version;
   int               cid;
   pid_t             pid;
};

static Eina_List *clients = NULL;

typedef Eina_Bool (*Opcode_Cb)(Ecore_Con_Client *client, void *buffer, int size);

static Eina_Hash *_string_to_opcode_hash = NULL;

static int free_cid = 1;

static uint32_t _clients_info_opcode, _cid_from_pid_opcode;

typedef struct
{
   uint32_t opcode;
   Eina_Stringshare *opcode_string;
} Opcode_Information;

Opcode_Information *_opcodes[EINA_DEBUG_OPCODE_MAX];

static Client *
_client_pid_find(int pid)
{
   Client *c;
   Eina_List *l;

   if (pid <= 0) return NULL;
   EINA_LIST_FOREACH(clients, l, c)
     {
        if (c->pid == pid) return c;
     }
   return NULL;
}

static Client *
_find_client_by_session(Eina_Debug_Session *session)
{
   Eina_List *itr;
   Client *c;
   EINA_LIST_FOREACH(clients, itr, c)
     {
        if (c->session == session) return c;
     }
   return NULL;
}

static void
_client_add(Eina_Debug_Session *session)
{
   Client *c = calloc(1, sizeof(Client));
   clients = eina_list_append(clients, c);
   c->session = session;
}

static void
_client_del(Eina_Debug_Session *session)
{
   Client *c = _find_client_by_session(session);
   if (c)
     {
        clients = eina_list_remove(clients, c);
        if (c->evlog_fetch_timer)
          {
             ecore_timer_del(c->evlog_fetch_timer);
             c->evlog_fetch_timer = NULL;
          }
        if (c->evlog_file)
          {
             fclose(c->evlog_file);
             c->evlog_file = NULL;
          }
        free(c);
     }
   eina_debug_session_free(session);
}

static Eina_Bool
_client_data(Eina_Debug_Session *session, void *buffer)
{
   Client *c = _find_client_by_session(session);
   if (c)
     {
        Eina_Debug_Packet_Header *hdr = (Eina_Debug_Packet_Header *)buffer;
        if (hdr->cid)
          {
             /* If the client id is given, we forward */
             Client *dest = _client_pid_find(hdr->cid);
             if (dest)
               {
                  char *data_buf = ((char *)buffer) + sizeof(Eina_Debug_Packet_Header);
                  int size = hdr->size + sizeof(uint32_t) - sizeof(Eina_Debug_Packet_Header);
                  Eina_Debug_Client *cl = eina_debug_client_new(dest->session, c->cid);
                  eina_debug_session_send(cl, hdr->opcode, data_buf, size);
                  eina_debug_client_free(cl);
               }
             else printf("Client %d not found\n", hdr->cid);
          }
        else
          {
             printf("Invoke %s\n", _opcodes[hdr->opcode]->opcode_string);
             return eina_debug_dispatch(session, buffer);
          }
     }
   return EINA_TRUE;
}

static uint32_t
_opcode_register(const char *op_name, uint32_t op_id)
{
   static uint32_t free_opcode = 0;
   Opcode_Information *op_info = eina_hash_find(_string_to_opcode_hash, op_name);
   if (!op_info)
     {
        op_info = calloc(1, sizeof(*op_info));
        if (op_id == EINA_DEBUG_OPCODE_INVALID)
          {
             do
               {
                  op_id = free_opcode++;
               }
             while(_opcodes[op_id]);
          }
        op_info->opcode = op_id;
        op_info->opcode_string = eina_stringshare_add(op_name);
        eina_hash_add(_string_to_opcode_hash, op_name, op_info);
        _opcodes[op_id] = op_info;
     }
   return op_info->opcode;
}

static Eina_Bool
_hello_cb(Eina_Debug_Client *src, void *buffer, int size)
{
   Client *c = _find_client_by_session(eina_debug_client_session_get(src));
   char *buf = buffer;
   memcpy(&c->version, buf, 4);
   memcpy(&c->pid, buf + 4, 4);
   size -= 8;
   if (size > 1)
     {
        c->app_name = malloc(size);
        strncpy(c->app_name, buf + 8, size);
        c->app_name[size - 1] = '\0';
     }
   c->cid = free_cid++;
   printf("Connection from pid %d - name %s\n", c->pid, c->app_name);
   return EINA_TRUE;
}

static Eina_Bool
_cid_get_cb(Eina_Debug_Client *src, void *buffer, int size EINA_UNUSED)
{
   uint32_t pid = *(uint32_t *)buffer;
   Client *c = _client_pid_find(pid);
   if (c)
      eina_debug_session_send(src, _cid_from_pid_opcode, &(c->cid), sizeof(uint32_t));
   return EINA_TRUE;
}

static Eina_Bool
_clients_info_cb(Eina_Debug_Client *src, void *buffer EINA_UNUSED, int size)
{
   Eina_List *itr;
   Client *c;
   int n = eina_list_count(clients);
   size = (1 + 2 * n) * sizeof(uint32_t); /* nb + nb*(pid+cid) */
   EINA_LIST_FOREACH(clients, itr, c)
     {
        size += (c->app_name ? strlen(c->app_name) : 0) + 1;
     }
   char *buf = alloca(size), *tmp = buf;
   if (!buf) return EINA_FALSE;
   STORE(tmp, &n, sizeof(uint32_t));
   EINA_LIST_FOREACH(clients, itr, c)
     {
        STORE(tmp, &c->cid, sizeof(uint32_t));
        STORE(tmp, &c->pid, sizeof(uint32_t));
        if (c->app_name)
          {
             STORE(tmp, c->app_name, strlen(c->app_name) + 1);
          }
        else
          {
             char end = '\0';
             STORE(tmp, &end, 1);
          }
     }
   eina_debug_session_send(src, _clients_info_opcode, buf, size);
   return EINA_TRUE;
}

static Eina_Bool
_opcode_register_cb(Eina_Debug_Client *src, void *buffer, int size)
{
   char *buf = buffer;
   buf += sizeof(uint64_t);
   size -= sizeof(uint64_t);
   uint32_t *opcodes = (uint32_t *)buf;

   while (size > 0)
     {
        int len = strlen(buf) + 1;
        *opcodes++ = _opcode_register(buf, EINA_DEBUG_OPCODE_INVALID);
        buf += len;
        size -= len;
     }

   eina_debug_session_send(src, EINA_DEBUG_OPCODE_REGISTER, buffer, (char *)opcodes - (char *)buffer);

   return EINA_TRUE;
}

int
main(int argc EINA_UNUSED, char **argv EINA_UNUSED)
{
   eina_debug_reconnect_set(EINA_FALSE);
   eina_init();
   ecore_init();

   eina_debug_session_global_use(_client_data);
   if (!eina_debug_server_launch(_client_add, _client_del))
     {
        fprintf(stderr, "ERROR: Cannot create debug daemon.\n");
        return -1;
     }

   _string_to_opcode_hash = eina_hash_string_superfast_new(NULL);
   _opcode_register("daemon/opcode_register", EINA_DEBUG_OPCODE_REGISTER);
   _opcode_register("daemon/opcode_hello", EINA_DEBUG_OPCODE_HELLO);
   _clients_info_opcode = _opcode_register("daemon/clients_infos", EINA_DEBUG_OPCODE_INVALID);
   _cid_from_pid_opcode = _opcode_register("daemon/cid_from_pid", EINA_DEBUG_OPCODE_INVALID);

   eina_debug_static_opcode_register(NULL, EINA_DEBUG_OPCODE_REGISTER, _opcode_register_cb);
   eina_debug_static_opcode_register(NULL, EINA_DEBUG_OPCODE_HELLO, _hello_cb);
   eina_debug_static_opcode_register(NULL, _clients_info_opcode, _clients_info_cb);
   eina_debug_static_opcode_register(NULL, _cid_from_pid_opcode, _cid_get_cb);

   ecore_main_loop_begin();

   ecore_shutdown();
   eina_shutdown();
}
