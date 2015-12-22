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

#include <Eo.h>
#include <Eina.h>
#include <Ecore.h>

#include <unistd.h>

static Eina_Debug_Session *_shell_session = NULL;

static Eina_Bool
_shell_to_local_forward(Eina_Debug_Session *session EINA_UNUSED, void *buffer)
{
   char *data_buf = ((char *)buffer) + sizeof(Eina_Debug_Packet_Header);
   Eina_Debug_Packet_Header *hdr = buffer;
   int size = hdr->size + sizeof(uint32_t) - sizeof(Eina_Debug_Packet_Header);
   eina_debug_session_send(NULL, hdr->cid, hdr->opcode, data_buf, size);
   return EINA_TRUE;
}

static Eina_Bool
_local_to_shell_forward(Eina_Debug_Session *session EINA_UNUSED, void *buffer)
{
   char *data_buf = ((char *)buffer) + sizeof(Eina_Debug_Packet_Header);
   Eina_Debug_Packet_Header *hdr = buffer;
   int size = hdr->size + sizeof(uint32_t) - sizeof(Eina_Debug_Packet_Header);
   eina_debug_session_send(_shell_session, hdr->cid, hdr->opcode, data_buf, size);
   return EINA_TRUE;
}

int
main(int argc, char **argv)
{
   fprintf(stdout, "OK\n");
   fflush(stdout);
   sleep(1);

   eina_init();
   ecore_init();

   (void)argc;
   (void)argv;

   _shell_session = eina_debug_session_new();
   eina_debug_session_basic_codec_add(_shell_session, EINA_DEBUG_CODEC_BASE_16);
   eina_debug_session_dispatch_override(_shell_session, _shell_to_local_forward);
   eina_debug_session_fd_attach(_shell_session, STDIN_FILENO);
   eina_debug_session_fd_out_set(_shell_session, STDOUT_FILENO);
   eina_debug_session_magic_set_on_send(_shell_session);

   eina_debug_session_dispatch_override(NULL, _local_to_shell_forward);

   ecore_main_loop_begin();

   ecore_shutdown();
   eina_shutdown();
}
