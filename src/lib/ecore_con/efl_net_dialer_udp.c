#define EFL_NET_DIALER_UDP_PROTECTED 1
#define EFL_NET_DIALER_PROTECTED 1
#define EFL_NET_SOCKET_FD_PROTECTED 1
#define EFL_NET_SOCKET_PROTECTED 1
#define EFL_IO_READER_PROTECTED 1

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "Ecore.h"
#include "Ecore_Con.h"
#include "ecore_con_private.h"

#ifdef HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_UDP_H
# include <netinet/udp.h>
#endif
#ifdef HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif
#ifdef HAVE_EVIL
# include <Evil.h>
#endif

#define MY_CLASS EFL_NET_DIALER_UDP_CLASS

typedef struct _Efl_Net_Dialer_Udp_Data
{
   struct {
      Ecore_Thread *thread;
      Efl_Future *timeout;
   } connect;
   Eina_Stringshare *address_dial;
   Eina_Bool connected;
   Eina_Bool closed;
   double timeout_dial;
} Efl_Net_Dialer_Udp_Data;

EOLIAN static Eo*
_efl_net_dialer_udp_efl_object_constructor(Eo *o, Efl_Net_Dialer_Udp_Data *pd EINA_UNUSED)
{
   o = efl_constructor(efl_super(o, MY_CLASS));
   if (!o) return NULL;

   efl_net_dialer_timeout_dial_set(o, 30.0);
   return o;
}

EOLIAN static void
_efl_net_dialer_udp_efl_object_destructor(Eo *o, Efl_Net_Dialer_Udp_Data *pd)
{
   if (efl_io_closer_close_on_destructor_get(o) &&
       (!efl_io_closer_closed_get(o)))
     efl_io_closer_close(o);

   if (pd->connect.thread)
     {
        ecore_thread_cancel(pd->connect.thread);
        pd->connect.thread = NULL;
     }

   efl_destructor(efl_super(o, MY_CLASS));

   eina_stringshare_replace(&pd->address_dial, NULL);
}

static void
_efl_net_dialer_udp_connect_timeout(void *data, const Efl_Event *ev EINA_UNUSED)
{
   Eo *o = data;
   Efl_Net_Dialer_Udp_Data *pd = efl_data_scope_get(o, MY_CLASS);
   Eina_Error err = ETIMEDOUT;

   if (pd->connect.thread)
     {
        ecore_thread_cancel(pd->connect.thread);
        pd->connect.thread = NULL;
     }

   efl_ref(o);
   efl_io_reader_eos_set(o, EINA_TRUE);
   efl_event_callback_call(o, EFL_NET_DIALER_EVENT_ERROR, &err);
   efl_unref(o);
}

static void
_efl_net_dialer_udp_connected(void *data, const struct sockaddr *addr, socklen_t addrlen EINA_UNUSED, int sockfd, Eina_Error err)
{
   Eo *o = data;
   Efl_Net_Dialer_Udp_Data *pd = efl_data_scope_get(o, MY_CLASS);
   char buf[INET6_ADDRSTRLEN + sizeof("[]:65536")];

   pd->connect.thread = NULL;

   efl_ref(o); /* we're emitting callbacks then continuing the workflow */

   if (err)
     {
        efl_io_reader_eos_set(o, EINA_TRUE);
        efl_event_callback_call(o, EFL_NET_DIALER_EVENT_ERROR, &err);
     }
   else
     {
        efl_net_socket_fd_family_set(o, addr->sa_family);
        efl_loop_fd_set(o, sockfd);
        if (efl_net_ip_port_fmt(buf, sizeof(buf), addr))
          {
             efl_net_socket_address_remote_set(o, buf);
             efl_event_callback_call(o, EFL_NET_DIALER_EVENT_RESOLVED, NULL);
          }
        efl_net_dialer_connected_set(o, EINA_TRUE);
     }

   efl_unref(o);
}

EOLIAN static Eina_Error
_efl_net_dialer_udp_efl_net_dialer_dial(Eo *o, Efl_Net_Dialer_Udp_Data *pd EINA_UNUSED, const char *address)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(address, EINVAL);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(efl_net_dialer_connected_get(o), EISCONN);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(efl_io_closer_closed_get(o), EBADF);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(efl_loop_fd_get(o) >= 0, EALREADY);

   if (pd->connect.thread)
     {
        ecore_thread_cancel(pd->connect.thread);
        pd->connect.thread = NULL;
     }

   if (pd->connect.thread)
     ecore_thread_cancel(pd->connect.thread);

   pd->connect.thread = efl_net_ip_connect_async_new(address,
                                                     "",
                                                     NULL,
                                                     NULL,
                                                     SOCK_DGRAM,
                                                     IPPROTO_UDP,
                                                     efl_io_closer_close_on_exec_get(o),
                                                     _efl_net_dialer_udp_connected, o);
   EINA_SAFETY_ON_NULL_RETURN_VAL(pd->connect.thread, EINVAL);

   efl_net_dialer_address_dial_set(o, address);

   if (pd->connect.timeout)
     efl_future_cancel(pd->connect.timeout);
   if (pd->timeout_dial > 0.0)
     {
        efl_future_use(&pd->connect.timeout, efl_loop_timeout(efl_loop_get(o), pd->timeout_dial, o));
        efl_future_then(pd->connect.timeout, _efl_net_dialer_udp_connect_timeout, NULL, NULL, o);
        efl_future_link(o, pd->connect.timeout);
     }

   return 0;
}

EOLIAN static void
_efl_net_dialer_udp_efl_net_dialer_address_dial_set(Eo *o EINA_UNUSED, Efl_Net_Dialer_Udp_Data *pd, const char *address)
{
   eina_stringshare_replace(&pd->address_dial, address);
}

EOLIAN static const char *
_efl_net_dialer_udp_efl_net_dialer_address_dial_get(Eo *o EINA_UNUSED, Efl_Net_Dialer_Udp_Data *pd)
{
   return pd->address_dial;
}

EOLIAN static void
_efl_net_dialer_udp_efl_net_dialer_timeout_dial_set(Eo *o EINA_UNUSED, Efl_Net_Dialer_Udp_Data *pd, double seconds)
{
   pd->timeout_dial = seconds;
   if (pd->connect.timeout)
     efl_future_cancel(pd->connect.timeout);

   if ((pd->timeout_dial > 0.0) && (pd->connect.thread))
     {
        efl_future_use(&pd->connect.timeout, efl_loop_timeout(efl_loop_get(o), pd->timeout_dial, o));
        efl_future_then(pd->connect.timeout, _efl_net_dialer_udp_connect_timeout, NULL, NULL, o);
     }
}

EOLIAN static double
_efl_net_dialer_udp_efl_net_dialer_timeout_dial_get(Eo *o EINA_UNUSED, Efl_Net_Dialer_Udp_Data *pd)
{
   return pd->timeout_dial;
}

EOLIAN static void
_efl_net_dialer_udp_efl_net_dialer_connected_set(Eo *o, Efl_Net_Dialer_Udp_Data *pd, Eina_Bool connected)
{
   if (pd->connect.timeout)
     efl_future_cancel(pd->connect.timeout);
   if (pd->connected == connected) return;
   pd->connected = connected;
   if (connected) efl_event_callback_call(o, EFL_NET_DIALER_EVENT_CONNECTED, NULL);
}

EOLIAN static Eina_Bool
_efl_net_dialer_udp_efl_net_dialer_connected_get(Eo *o EINA_UNUSED, Efl_Net_Dialer_Udp_Data *pd)
{
   return pd->connected;
}

EOLIAN static Eina_Error
_efl_net_dialer_udp_efl_io_closer_close(Eo *o, Efl_Net_Dialer_Udp_Data *pd)
{
   pd->closed = EINA_TRUE;
   efl_net_dialer_connected_set(o, EINA_FALSE);
   return efl_io_closer_close(efl_super(o, MY_CLASS));
}

#include "efl_net_dialer_udp.eo.c"