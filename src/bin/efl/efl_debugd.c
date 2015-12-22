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

#define EXTRACT(_buf, pval, sz) \
{ \
   memcpy(pval, _buf, sz); \
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

   Eina_Bool         cl_stat_obs : 1;
};

static Eina_List *clients = NULL;

typedef Eina_Bool (*Opcode_Cb)(Ecore_Con_Client *client, void *buffer, int size);

static Eina_Hash *_string_to_opcode_hash = NULL;

static int free_cid = 1;

static uint32_t _client_added_opcode, _client_deleted_opcode, _clients_stat_register_opcode;
static uint32_t _cid_from_pid_opcode;

typedef struct
{
   uint32_t opcode;
   Eina_Stringshare *opcode_string;
} Opcode_Information;

Opcode_Information *_opcodes[EINA_DEBUG_OPCODE_MAX];

static Client *
_client_find_by_cid(int cid)
{
   Client *c;
   Eina_List *l;
   EINA_LIST_FOREACH(clients, l, c)
      if (c->cid == cid) return c;
   return NULL;
}

static Client *
_client_find_by_pid(int pid)
{
   Client *c;
   Eina_List *l;
   EINA_LIST_FOREACH(clients, l, c)
      if (c->pid == pid) return c;
   return NULL;
}

static Client *
_client_find_by_session(Eina_Debug_Session *session)
{
   Eina_List *itr;
   Client *c;
   EINA_LIST_FOREACH(clients, itr, c)
      if (c->session == session) return c;
   return NULL;
}

static void
_client_del(Eina_Debug_Session *session)
{
   Client *c = _client_find_by_session(session);
   if (!c) return;
   int cid = c->cid;
   Eina_List *itr;
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
   /* Update the observers */
   EINA_LIST_FOREACH(clients, itr, c)
     {
        if (c->cl_stat_obs) eina_debug_session_send(c->session, c->cid, _client_deleted_opcode, &cid, sizeof(uint32_t));
     }
}

static Eina_Bool
_client_data(Eina_Debug_Session *session, void *buffer)
{
   Eina_Debug_Packet_Header *hdr = (Eina_Debug_Packet_Header *)buffer;
   if (hdr->cid)
     {
        /* If the client id is given, we forward */
        Client *dest = _client_find_by_cid(hdr->cid);
        if (dest)
          {
             Client *src = _client_find_by_session(session);
             if (src)
               {
                  char *data_buf = ((char *)buffer) + sizeof(Eina_Debug_Packet_Header);
                  int size = hdr->size + sizeof(uint32_t) - sizeof(Eina_Debug_Packet_Header);
                  eina_debug_session_send(dest->session, src->cid, hdr->opcode, data_buf, size);
               }
             else printf("Client %d not found\n", hdr->cid);
          }
     }
   else
     {
        printf("Invoke %s\n", _opcodes[hdr->opcode]->opcode_string);
        return eina_debug_dispatch(session, buffer);
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
   printf("Register %s -> opcode %d\n", op_name, op_info->opcode);
   return op_info->opcode;
}

static Eina_Bool
_hello_cb(Eina_Debug_Session *session, uint32_t src EINA_UNUSED, void *buffer, int size)
{
   Eina_List *itr;
   if (_client_find_by_session(session)) return EINA_FALSE;
   Client *c = calloc(1, sizeof(Client));
   clients = eina_list_append(clients, c);
   char *buf = buffer;
   if (!c) return EINA_FALSE;
   EXTRACT(buf, &c->version, 4);
   EXTRACT(buf, &c->pid, 4);
   size -= 8;
   c->session = session;
   c->cid = free_cid++;
   if (size > 1)
     {
        c->app_name = malloc(size);
        strncpy(c->app_name, buf, size);
        c->app_name[size - 1] = '\0';
     }
   printf("Connection from pid %d - name %s\n", c->pid, c->app_name);
   /* Update the observers */
   size = 2 * sizeof(uint32_t) + (c->app_name ? strlen(c->app_name) : 0) + 1; /* cid + pid + name + \0 */
   buf = alloca(size);
   char *tmp = buf;
   if (!buf) return EINA_FALSE;
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
   EINA_LIST_FOREACH(clients, itr, c)
     {
        if (c->cl_stat_obs) eina_debug_session_send(c->session, c->cid, _client_added_opcode, buf, size);
     }
   return EINA_TRUE;
}

static Eina_Bool
_cid_get_cb(Eina_Debug_Session *session, uint32_t cid, void *buffer, int size EINA_UNUSED)
{
   uint32_t pid = *(uint32_t *)buffer;
   Client *c = _client_find_by_pid(pid);
   if (c) eina_debug_session_send(session, cid, _cid_from_pid_opcode, &(c->cid), sizeof(uint32_t));
   return EINA_TRUE;
}

static Eina_Bool
_cl_stat_obs_register_cb(Eina_Debug_Session *session, uint32_t cid, void *buffer EINA_UNUSED, int size EINA_UNUSED)
{
   Client *c = _client_find_by_session(session);
   if (!c) return EINA_FALSE;
   if (!c->cl_stat_obs)
     {
        Eina_List *itr;
        int n = eina_list_count(clients);
        c->cl_stat_obs = EINA_TRUE;
        size = (2 * n) * sizeof(uint32_t); /* nb*(cid+pid) */
        EINA_LIST_FOREACH(clients, itr, c)
          {
             size += (c->app_name ? strlen(c->app_name) : 0) + 1;
          }
        char *buf = alloca(size), *tmp = buf;
        if (!buf) return EINA_FALSE;
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
        eina_debug_session_send(session, cid, _client_added_opcode, buf, size);
     }
   return EINA_TRUE;
}

static Eina_Bool
_opcode_register_cb(Eina_Debug_Session *session, uint32_t cid, void *buffer, int size)
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

   eina_debug_session_send(session, cid, EINA_DEBUG_OPCODE_REGISTER, buffer, (char *)opcodes - (char *)buffer);

   return EINA_TRUE;
}

int
main(int argc EINA_UNUSED, char **argv EINA_UNUSED)
{
   eina_debug_reconnect_set(EINA_FALSE);
   eina_init();
   ecore_init();

   eina_debug_session_global_use(_client_data);
   if (!eina_debug_server_launch(NULL, _client_del))
     {
        fprintf(stderr, "ERROR: Cannot create debug daemon.\n");
        return -1;
     }

   _string_to_opcode_hash = eina_hash_string_superfast_new(NULL);
   _opcode_register("daemon/opcode_register", EINA_DEBUG_OPCODE_REGISTER);
   _opcode_register("daemon/opcode_hello", EINA_DEBUG_OPCODE_HELLO);
   _clients_stat_register_opcode = _opcode_register("daemon/client_status_register", EINA_DEBUG_OPCODE_INVALID);
   _client_added_opcode = _opcode_register("daemon/client_added", EINA_DEBUG_OPCODE_INVALID);
   _client_deleted_opcode = _opcode_register("daemon/client_deleted", EINA_DEBUG_OPCODE_INVALID);
   _cid_from_pid_opcode = _opcode_register("daemon/cid_from_pid", EINA_DEBUG_OPCODE_INVALID);

   eina_debug_static_opcode_register(NULL, EINA_DEBUG_OPCODE_REGISTER, _opcode_register_cb);
   eina_debug_static_opcode_register(NULL, EINA_DEBUG_OPCODE_HELLO, _hello_cb);
   eina_debug_static_opcode_register(NULL, _clients_stat_register_opcode, _cl_stat_obs_register_cb);
   eina_debug_static_opcode_register(NULL, _cid_from_pid_opcode, _cid_get_cb);

   ecore_main_loop_begin();

   ecore_shutdown();
   eina_shutdown();
}
