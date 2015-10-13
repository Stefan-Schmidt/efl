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

/* Last opcode_name in ops is NULL
 * Sends to daemon: pointer of ops followed by list of opcode names seperated by \n
 * */
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

   memcpy(buf, &ops, sizeof(Eina_Debug_Opcode *));
   int size_curr = sizeof(Eina_Debug_Opcode *) + 1;

   while(ops->opcode_name)
     {
        int len = strlen(ops->opcode_name) + 1;
        memcpy(buf + size_curr, ops->opcode_name, len);
        ops++;
     }

   _eina_debug_session_send(session,
         "REGO"/* register opcode , should be changed to 0 */,
         buf,
         size);
}

/* Expecting pointer of ops followed by list of uint's */
Eina_Bool
eina_debug_register_cb(Eina_Debug_Session *session, void *buffer, int size)
{
   Eina_Debug_Opcode* ops = NULL;
   unsigned char *buff = buffer;

   if(size >= (int)sizeof(Eina_Debug_Opcode *))
      memcpy(&ops, buff, sizeof(Eina_Debug_Opcode *));

   if(!ops)
      return EINA_FALSE;

   buff += sizeof(Eina_Debug_Opcode *);

   uint32_t* os = (uint32_t *)buff;
   int count = (size - sizeof(Eina_Debug_Opcode *)) / sizeof(unsigned int);

   int i;
   for(i = 0; i < count; i++)
     {
        if(ops[i].opcode_id)
          {
             *(ops[i].opcode_id) = os[i];
          }
        session->cbs[os[i]] =  ops[i].cb;
     }

   return EINA_TRUE;
}

Eina_Bool
eina_debug_dispatch(Eina_Debug_Session *session, void *buffer)
{
   Eina_Debug_Packet_Header *hdr =  buffer;

   uint32_t opcode = hdr->opcode;

   if (opcode < OPCODE_MAX)
     {
        Eina_Debug_Cb cb = session->cbs[opcode];
        if (cb) cb((unsigned char *)buffer + sizeof(*hdr), hdr->size - sizeof(*hdr));
        return EINA_TRUE;
     }

   return EINA_FALSE;
}

#endif
