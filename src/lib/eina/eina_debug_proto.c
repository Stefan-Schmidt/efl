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

int
_eina_debug_session_send(Eina_Debug_Session *session, unsigned int op,
                                 unsigned char *data, int size)
{
   if (!session) return -1;
   // send protocol packet. all protocol is an int for size of packet then
   // included in that size (so a minimum size of 4) is a 4 byte opcode
   // (all opcodes are 4 bytes as a string of 4 chars), then the real
   // message payload as a data blob after that
   unsigned char *buf = alloca(sizeof(Eina_Debug_Packet_Header) + size);
   Eina_Debug_Packet_Header *hdr = (Eina_Debug_Packet_Header *)buf;
   hdr->size = size + sizeof(Eina_Debug_Packet_Header) - sizeof(uint32_t);
   hdr->opcode = (uint32_t)op;
   if (size > 0) memcpy(buf + sizeof(Eina_Debug_Packet_Header), data, size);
   return write(session->fd, buf, hdr->size + sizeof(uint32_t));
}

void
_eina_debug_monitor_service_greet(Eina_Debug_Session *session)
{
   // say hello to our debug daemon - tell them our PID and protocol
   // version we speak
   unsigned char buf[8];
   int version = 1; // version of protocol we speak
   int pid = getpid();
   memcpy(buf +  0, &version, 4);
   memcpy(buf +  4, &pid, 4);
   _eina_debug_session_send(session, EINA_OPCODE_HELO, buf, sizeof(buf));
}

int
_eina_debug_session_receive(Eina_Debug_Session *session, unsigned char **buffer)
{
   uint32_t size;
   int rret;

   if (!session) return -1;
   // get size of packet
   rret = read(session->fd, (void *)&size, sizeof(uint32_t));
   if (rret == sizeof(uint32_t))
     {
        // allocate a buffer for real payload + header - size variable size
        *buffer = malloc(size + sizeof(uint32_t));
        if (*buffer)
          {
             memcpy(*buffer, &size, sizeof(uint32_t));
             // get payload - blocking!!!!
             rret = read(session->fd, *buffer + sizeof(uint32_t), size);
             if (rret != (int)size)
               {
                  // we didn't get payload as expected - error on
                  // comms
                  fprintf(stderr,
                        "EINA DEBUG ERROR: "
                        "Invalid payload+header read of %i\n", rret);
                  free(*buffer);
                  *buffer = NULL;
                  return -1;
               }
             // return payload size (< 0 is an error)
             return size;
          }
        else
          {
             // we couldn't allocate memory for payloa buffer
             // internal memory limit error
             fprintf(stderr,
                   "EINA DEBUG ERROR: "
                   "Cannot allocate %u bytes for op\n", (unsigned int)size);
             return -1;
          }
     }
   fprintf(stderr,
           "EINA DEBUG ERROR: "
           "Invalid size read %i != %lu\n", rret, sizeof(uint32_t));

   return -1;
}
#endif
