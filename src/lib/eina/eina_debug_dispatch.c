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

#define OPCODE_MAX 100

Eina_Debug_Cb _cbs[OPCODE_MAX] = {};

//void
//eina_debug_opcodes_register
Eina_Bool
eina_debug_dispatch(void *buffer)
{
   Eina_Debug_Packet_Header *hdr = buffer;

   uint32_t opcode = hdr->opcode;

   if (opcode < OPCODE_MAX)
     {
        Eina_Debug_Cb cb = _cbs[opcode];
        if (cb) cb(buffer + sizeof(*hdr), hdr->size - sizeof(*hdr));
        return EINA_TRUE;
     }

   return EINA_FALSE;
}

#endif
