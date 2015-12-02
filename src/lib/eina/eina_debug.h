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

#ifndef EINA_DEBUG_H_
# define EINA_DEBUG_H_

# include <stdint.h>

# include "eina_config.h"
# include "eina_lock.h"
# include "eina_list.h"

# define EINA_MAX_BT 256

# define EINA_DEBUG_OPCODE_MAX 100

# define EINA_DEBUG_OPCODE_INVALID    0xFFFFFFFF
# define EINA_DEBUG_OPCODE_REGISTER   0x00000000
# define EINA_DEBUG_OPCODE_HELLO      0x00000001

typedef struct _Eina_Debug_Client Eina_Debug_Client;
typedef struct _Eina_Debug_Session Eina_Debug_Session;

typedef Eina_Bool (*Eina_Debug_Cb)(Eina_Debug_Client *src, void *buffer, int size);
typedef void (*Eina_Debug_Opcode_Status_Cb)(Eina_Bool);
typedef Eina_Bool (*Eina_Debug_Dispatch_Cb)(Eina_Debug_Session *, void *buffer);

typedef Eina_Bool (*Eina_Debug_Timer_Cb)(void);

typedef void (*Eina_Debug_Connect_Cb)(Eina_Debug_Session *);
typedef void (*Eina_Debug_Disconnect_Cb)(Eina_Debug_Session *);

typedef void *(*Eina_Debug_Encode_Cb)(const void *buffer, int size, int *ret_size);
typedef void *(*Eina_Debug_Decode_Cb)(const void *buffer, int size, int *ret_size);

typedef struct
{
   uint32_t size;
   /*
    * During sending, it corresponds to the id of the destination. During reception, it is the id of the source
    * The daemon is in charge of swapping the id before forwarding to the destination.
    */
   uint32_t cid;
   uint32_t opcode;
} Eina_Debug_Packet_Header;

typedef struct
{
   char *opcode_name;
   uint32_t *opcode_id;
   Eina_Debug_Cb cb;
} Eina_Debug_Opcode;

typedef enum
{
   EINA_DEBUG_CODEC_BASE_16
} Eina_Debug_Basic_Codec;

EAPI void eina_debug_reconnect_set(Eina_Bool reconnect);
EAPI Eina_Bool eina_debug_local_connect(Eina_Debug_Session *session);
EAPI Eina_Bool eina_debug_server_launch(Eina_Debug_Connect_Cb, Eina_Debug_Disconnect_Cb);
EAPI Eina_Bool eina_debug_shell_remote_connect(Eina_Debug_Session *session, const char *cmd, const char *script);

/* TEMP: should be private to debug thread module */
void _eina_debug_thread_add(void *th);
void _eina_debug_thread_del(void *th);
void _eina_debug_thread_mainloop_set(void *th);

void *_eina_debug_chunk_push(int size);
void *_eina_debug_chunk_realloc(int size);
char *_eina_debug_chunk_strdup(const char *str);
void *_eina_debug_chunk_tmp_push(int size);
void  _eina_debug_chunk_tmp_reset(void);

const char *_eina_debug_file_get(const char *fname);

void _eina_debug_dump_fhandle_bt(FILE *f, void **bt, int btlen);

EAPI Eina_Debug_Session *eina_debug_session_new(void);
EAPI void eina_debug_session_free(Eina_Debug_Session *session);
EAPI void eina_debug_session_fd_attach(Eina_Debug_Session *session, int fd);
EAPI void eina_debug_session_fd_out_set(Eina_Debug_Session *session, int fd);
EAPI void eina_debug_session_global_use(Eina_Debug_Dispatch_Cb disp_cb);
EAPI void eina_debug_session_dispatch_override(Eina_Debug_Session *session, Eina_Debug_Dispatch_Cb disp_cb);

EAPI void eina_debug_session_codec_hooks_add(Eina_Debug_Session *session, Eina_Debug_Encode_Cb enc_cb, Eina_Debug_Decode_Cb dec_cb);
EAPI void eina_debug_session_basic_codec_add(Eina_Debug_Session *session, Eina_Debug_Basic_Codec codec);

EAPI Eina_Bool eina_debug_dispatch(Eina_Debug_Session *session, void *buffer);

EAPI void eina_debug_opcodes_register(Eina_Debug_Session *session,
      const Eina_Debug_Opcode ops[], Eina_Debug_Opcode_Status_Cb status_cb);
EAPI void eina_debug_static_opcode_register(Eina_Debug_Session *session,
      uint32_t op_id, Eina_Debug_Cb);
EAPI int eina_debug_session_send(Eina_Debug_Client *dest, uint32_t op, void *data, int size);

EAPI Eina_Debug_Client *eina_debug_client_new(Eina_Debug_Session *session, int id);
EAPI Eina_Debug_Session *eina_debug_client_session_get(Eina_Debug_Client *cl);
EAPI int eina_debug_client_id_get(Eina_Debug_Client *cl);
EAPI void eina_debug_client_free(Eina_Debug_Client *cl);

EAPI Eina_Bool eina_debug_timer_add(unsigned int timeout_ms, Eina_Debug_Timer_Cb cb);

#  define EINA_BT(file) \
   do { \
      void *bt[EINA_MAX_BT]; \
      int btlen = backtrace((void **)bt, EINA_MAX_BT); \
      _eina_debug_dump_fhandle_bt(file, bt, btlen); \
   } while (0)

#endif
