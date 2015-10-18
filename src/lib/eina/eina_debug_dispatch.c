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

typedef struct
{
   Eina_Debug_Session *session;
   int cid;
} _Eina_Debug_Source;

EAPI Eina_Debug_Session *
eina_debug_source_session_get(Eina_Debug_Source *src)
{
   _Eina_Debug_Source *_src = (_Eina_Debug_Source *)src;
   return _src->session;
}

EAPI int
eina_debug_source_id_get(Eina_Debug_Source *src)
{
   _Eina_Debug_Source *_src = (_Eina_Debug_Source *)src;
   return _src->cid;
}

/*
 * Sends to daemon:
 * - Pointer to ops: returned in the response to determine which opcodes have been added
 * - List of opcode names seperated by \0
 */
EAPI void
eina_debug_opcodes_register(Eina_Debug_Session *session, const Eina_Debug_Opcode ops[])
{
   unsigned char *buf;

   int count = 0;
   int size = sizeof(Eina_Debug_Opcode *);

   while(ops[count].opcode_name)
     {
        size += strlen(ops[count].opcode_name) + 1;
        count++;
     }

   buf = alloca(size);

   uint64_t p = (uint64_t)&ops;
   memcpy(buf, &p, sizeof(uint64_t));
   int size_curr = sizeof(uint64_t);

   while(ops->opcode_name)
     {
        int len = strlen(ops->opcode_name) + 1;
        memcpy(buf + size_curr, ops->opcode_name, len);
        ops++;
     }

   _eina_debug_session_send(session,
         EINA_DEBUG_REGISTER_OPCODE,
         buf,
         size);
}

/* Expecting pointer of ops followed by list of uint32's */
Eina_Bool
_eina_debug_callbacks_register_cb(Eina_Debug_Session *session, void *buffer, int size)
{
   Eina_Debug_Opcode *ops = NULL;

   memcpy(&ops, buffer, sizeof(uint64_t));

   if (!ops) return EINA_FALSE;

   uint32_t* os = (uint32_t *)((unsigned char *)buffer + sizeof(uint64_t));
   int count = (size - sizeof(uint64_t)) / sizeof(uint32_t);

   int i;
   for (i = 0; i < count; i++)
     {
        if (ops[i].opcode_id) *(ops[i].opcode_id) = os[i];
        session->cbs[os[i]] = ops[i].cb;
     }

   return EINA_TRUE;
}

Eina_Bool
eina_debug_dispatch(Eina_Debug_Session *session, void *buffer)
{
   Eina_Debug_Packet_Header *hdr =  buffer;

   uint32_t opcode = hdr->opcode;

   if (opcode < EINA_OPCODE_MAX)
     {
        Eina_Debug_Cb cb = session->cbs[opcode];
        if (cb) cb(NULL, (unsigned char *)buffer + sizeof(*hdr), hdr->size - sizeof(*hdr));
        return EINA_TRUE;
     }
   return EINA_FALSE;
}

#endif
