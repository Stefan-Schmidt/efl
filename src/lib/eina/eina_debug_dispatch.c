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

#ifdef EINA_HAVE_DEBUG

static void
_eina_debug_opcodes_register_by_reply_info(Eina_Debug_Session *session,
      _opcode_reply_info *info);

typedef struct
{
   Eina_Debug_Session *session;
   int cid;
} _Eina_Debug_Client;

Eina_Debug_Session *_eina_debug_global_session = NULL;

EAPI Eina_Debug_Client *
eina_debug_client_new(Eina_Debug_Session *session, int id)
{
   _Eina_Debug_Client *cl = calloc(1, sizeof(_Eina_Debug_Client));
   cl->session = session;
   cl->cid = id;
   return (Eina_Debug_Client *)cl;
}

EAPI Eina_Debug_Session *
eina_debug_client_session_get(Eina_Debug_Client *cl)
{
   _Eina_Debug_Client *_cl = (_Eina_Debug_Client *)cl;
   return _cl->session;
}

EAPI int
eina_debug_client_id_get(Eina_Debug_Client *cl)
{
   _Eina_Debug_Client *_cl = (_Eina_Debug_Client *)cl;
   return _cl->cid;
}

EAPI void
eina_debug_client_free(Eina_Debug_Client *cl)
{
   free(cl);
}

void
eina_debug_opcodes_unregister(Eina_Debug_Session *session)
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

void
eina_debug_opcodes_register_all(Eina_Debug_Session *session)
{
   Eina_List *l;
   _opcode_reply_info *info = NULL;

   eina_debug_static_opcode_register(session,
         EINA_DEBUG_OPCODE_REGISTER, _eina_debug_callbacks_register_cb);
   EINA_LIST_FOREACH(session->opcode_reply_infos, l, info)
        _eina_debug_opcodes_register_by_reply_info(session, info);;
}

EAPI void
eina_debug_static_opcode_register(Eina_Debug_Session *session,
      uint32_t op_id, Eina_Debug_Cb cb)
{
   if(_eina_debug_global_session) session = _eina_debug_global_session;

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
   if(!session)
      session = _eina_debug_get_main_session();
   if(!session)
      return;

   _opcode_reply_info *info = malloc(sizeof(*info));
   info->ops = ops;
   info->status_cb = status_cb;

   Eina_Debug_Session *session_opcodes = session;
   if(_eina_debug_global_session)
        session_opcodes = _eina_debug_global_session;

   session_opcodes->opcode_reply_infos = eina_list_append(
         session_opcodes->opcode_reply_infos, info);

   //send only if session's fd connected, if not -  it will be sent when connected
   if(session->fd)
      _eina_debug_opcodes_register_by_reply_info(session, info);
}

static void
_eina_debug_opcodes_register_by_reply_info(Eina_Debug_Session *session,
      _opcode_reply_info *info)
{
    unsigned char *buf;

   int count = 0;
   int size = sizeof(uint64_t);

   if(!session)
      session = _eina_debug_get_main_session();
   if(!session)
      return;

   while(info->ops[count].opcode_name)
     {
        size += strlen(info->ops[count].opcode_name) + 1;
        count++;
     }

   buf = alloca(size);

   memcpy(buf, &info, sizeof(uint64_t));
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

/* Expecting pointer of ops followed by list of uint32's */
Eina_Bool
_eina_debug_callbacks_register_cb(Eina_Debug_Client *cl, void *buffer, int size)
{
   Eina_Debug_Session *session = eina_debug_client_session_get(cl);
   _opcode_reply_info *info = NULL;

   memcpy(&info, buffer, sizeof(uint64_t));

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

Eina_Bool
eina_debug_dispatch(Eina_Debug_Session *session, void *buffer)
{
   Eina_Debug_Packet_Header *hdr =  buffer;
   uint32_t opcode = hdr->opcode;
   Eina_Debug_Cb cb = NULL;
   Eina_Debug_Session *session_opcodes = _eina_debug_global_session ? _eina_debug_global_session : session;

   if(opcode < session_opcodes->cbs_length)
      cb = session_opcodes->cbs[opcode];

   if (cb)
     {
        Eina_Debug_Client *cl = eina_debug_client_new(session, hdr->cid);
        cb(cl, (unsigned char *)buffer + sizeof(*hdr), hdr->size + sizeof(uint32_t) - sizeof(*hdr));
        eina_debug_client_free(cl);
        return EINA_TRUE;
     }
   return EINA_FALSE;
}

#endif
