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

typedef struct _Client Client;

struct _Client
{
   Ecore_Con_Client *client;
   unsigned char    *buf;
   unsigned int      buf_size;

   Ecore_Timer      *evlog_fetch_timer;
   int               evlog_on;
   FILE             *evlog_file;

   int               version;
   int               cid;
   pid_t             pid; /* Need to be removed? */
};

static Ecore_Con_Server *svr = NULL;
static Eina_List *clients = NULL;

typedef Eina_Bool (*Opcode_Cb)(Ecore_Con_Client *client, void *buffer, int size);

static Eina_Hash *_string_to_opcode_hash = NULL;

static int free_cid = 1;

typedef struct
{
   uint32_t opcode;
   Eina_Stringshare *opcode_string;
   Opcode_Cb cb;
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

static Eina_Bool
_cb_evlog(void *data)
{
   Client *c = data;
   send_cli(c->client, "EVLG", NULL, 0);
   return EINA_TRUE;
}

static Eina_Bool
_client_add(void *data EINA_UNUSED, int type EINA_UNUSED, Ecore_Con_Event_Client_Add *ev)
{
   Client *c = calloc(1, sizeof(Client));
   if (c)
     {
        c->client = ev->client;
        clients = eina_list_append(clients, c);
        ecore_con_client_data_set(c->client, c);
     }
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_client_del(void *data EINA_UNUSED, int type EINA_UNUSED, Ecore_Con_Event_Client_Del *ev)
{
   Client *c = ecore_con_client_data_get(ev->client);
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
   return ECORE_CALLBACK_RENEW;
}

static void
_client_send(Ecore_Con_Client *dest, void *buffer, int size)
{
   Eina_Debug_Packet_Header *hdr = buffer;
   hdr->size = size - sizeof(uint32_t);
   ecore_con_client_send(dest, buffer, size);
}

static Eina_Bool
_client_data(void *data EINA_UNUSED, int type EINA_UNUSED, Ecore_Con_Event_Client_Data *ev)
{
   Client *c = ecore_con_client_data_get(ev->client);
   if (c)
     {
        unsigned int size = ev->size;
        unsigned char *cur = ev->data;
        Eina_Debug_Packet_Header *hdr = (Eina_Debug_Packet_Header *)cur;
        unsigned int psize = hdr->size;
        if (psize < size)
          {
             /* All the packet is received */
             if (size > c->buf_size)
               {
                  /* Copy the packet into a temp buffer */
                  c->buf = realloc(c->buf, size);
                  c->buf_size = size;
               }
             memcpy(c->buf, cur, size);
             hdr = (Eina_Debug_Packet_Header *)c->buf;
             if (hdr->cid)
               {
                  /* If the client id is given, we forward */
                  Client *dest = _client_pid_find(hdr->cid);
                  if (dest)
                    {
                       hdr->cid = c->cid;
                       ecore_con_client_send(dest->client, c->buf, c->buf_size);
                    }
                  else printf("Client %d not found\n", hdr->cid);
               }
             else
               {
                  hdr->cid = c->cid;
                  Opcode_Information *op = _opcodes[hdr->opcode];
                  if (op && op->cb)
                    {
                       printf("Invoke opcode %d\n", hdr->opcode);
                       op->cb(c->client, c->buf, c->buf_size);
                    }
               }

             size -= psize;
             cur += psize;
          }
        else
          {
             // Need to handle fragmented packets
          }
     }
   return ECORE_CALLBACK_RENEW;
}

static uint32_t
_opcode_register(const char *op_name, uint32_t op_id, Opcode_Cb cb)
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
        op_info->cb = cb;
        eina_hash_add(_string_to_opcode_hash, op_name, op_info);
        _opcodes[op_id] = op_info;
     }
   return op_info->opcode;
}

static Eina_Bool
_hello_cb(Ecore_Con_Client *dest, void *buffer, int size EINA_UNUSED)
{
   int version = -1; // version of protocol we speak
   int pid = -1;
   unsigned char *buf = buffer;
   buf += sizeof(Eina_Debug_Packet_Header);
   memcpy(&version, buf, 4);
   memcpy(&pid, buf + 4, 4);
   Client *c = ecore_con_client_data_get(dest);
   c->version = version;
   c->pid = pid;
   c->cid = free_cid++;
   printf("Connection from pid %d\n", pid);
   return EINA_TRUE;
}

static Eina_Bool
_cid_get_cb(Ecore_Con_Client *dest, void *buffer, int size)
{
   uint32_t pid = *(uint32_t *)((char *)buffer + sizeof(Eina_Debug_Packet_Header));
   Client *c = _client_pid_find(pid);
   *(uint32_t *)buffer = c->cid;
   _client_send(dest, buffer, size);
   return EINA_TRUE;
}

static Eina_Bool
_pids_list_cb(Ecore_Con_Client *dest, void *buffer, int size EINA_UNUSED)
{
   Client *c;
   int n = eina_list_count(clients);
   size = sizeof(Eina_Debug_Packet_Header) + n * sizeof(uint32_t);
   char *buf = alloca(size);
   memcpy(buf, buffer, sizeof(Eina_Debug_Packet_Header));
   uint32_t *pids = (uint32_t *)(buf + sizeof(Eina_Debug_Packet_Header));
   if (pids)
     {
        int i = 0;
        Eina_List *l;
        EINA_LIST_FOREACH(clients, l, c)
          {
             pids[i] = c->pid;
             i++;
          }
        _client_send(dest, buf, size);
     }
   return EINA_TRUE;
}

static Eina_Bool
_opcode_register_cb(Ecore_Con_Client *dest, void *buffer, int size)
{
   char *buf = buffer;
   buf += sizeof(Eina_Debug_Packet_Header) + sizeof(uint64_t);
   size -= (sizeof(Eina_Debug_Packet_Header) + sizeof(uint64_t));
   uint32_t *opcodes = (uint32_t *)buf;

   while (size > 0)
     {
        int len = strlen(buf) + 1;
        *opcodes++ = _opcode_register(buf, EINA_DEBUG_OPCODE_INVALID, NULL);
        buf += len;
        size -= len;
     }

   _client_send(dest, buffer, (char *)opcodes - (char *)buffer);

   return EINA_TRUE;
}

int
main(int argc EINA_UNUSED, char **argv EINA_UNUSED)
{
   eina_init();
   ecore_init();
   ecore_con_init();

   svr = ecore_con_server_add(ECORE_CON_LOCAL_USER, "efl_debug", 0, NULL);
   if (!svr)
     {
        fprintf(stderr, "ERROR: Cannot create debug daemon.\n");
        return -1;
     }

   ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_ADD, (Ecore_Event_Handler_Cb)_client_add, NULL);
   ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_DEL, (Ecore_Event_Handler_Cb)_client_del, NULL);
   ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_DATA, (Ecore_Event_Handler_Cb)_client_data, NULL);

   _string_to_opcode_hash = eina_hash_string_superfast_new(NULL);
   _opcode_register("daemon/opcode_register", EINA_DEBUG_OPCODE_REGISTER, _opcode_register_cb);
   _opcode_register("daemon/opcode_hello", EINA_DEBUG_OPCODE_HELLO, _hello_cb);
   _opcode_register("daemon/pids_list", EINA_DEBUG_OPCODE_INVALID, _pids_list_cb);
   _opcode_register("daemon/cid_from_pid", EINA_DEBUG_OPCODE_INVALID, _cid_get_cb);

   ecore_main_loop_begin();

   ecore_con_server_del(svr);

   ecore_con_shutdown();
   ecore_shutdown();
   eina_shutdown();
}
