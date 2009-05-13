/*****************************************************************************
* Lua-RPC library, Copyright (C) 2001 Russell L. Smith. All rights reserved. *
*   Email: russ@q12.org   Web: www.q12.org                                   *
* For documentation, see http://www.q12.org/lua. For the license agreement,  *
* see the file LICENSE that comes with this distribution.                    *
*****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>

#ifdef WIN32 /* BEGIN NEEDED INCLUDES FOR WIN32 W/ SOCKETS */

#include <windows.h>

#else /* BEGIN NEEDED INCLUDES FOR UNIX W/ SOCKETS */

#include <string.h>
#include <errno.h>
#include <alloca.h>
#include <signal.h>

/* for sockets */
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>

#endif /* END NEEDED INCLUDES W/ SOCKETS */

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "luarpc_rpc.h"

#include "config.h"


/****************************************************************************/
/* handle the differences between winsock and unix */

#ifdef WIN32  /*  BEGIN WIN32 SOCKET SETUP  */

/* this should be called before any network operations */

static void net_startup()
{
  WORD wVersionRequested;
  WSADATA wsaData;
  int err;

  // startup WinSock version 2
  wVersionRequested = MAKEWORD(2,0);
  err = WSAStartup (wVersionRequested,&wsaData);
  if (err != 0) panic ("could not start winsock");

  // confirm that the WinSock DLL supports 2.0. note that if the DLL
  // supports versions greater than 2.0 in addition to 2.0, it will
  // still return 2.0 in wVersion since that is the version we requested.
  if (LOBYTE (wsaData.wVersion ) != 2 ||
      HIBYTE(wsaData.wVersion) != 0 ) {
    WSACleanup();
    panic ("bad winsock version (< 2)");
  }
}


/* WinSock does not seem to have a strerror() style function, so here it is. */

const char * transport_strerror (int n)
{
  switch (n) {
  case WSAEACCES: return "Permission denied.";
  case WSAEADDRINUSE: return "Address already in use.";
  case WSAEADDRNOTAVAIL: return "Cannot assign requested address.";
  case WSAEAFNOSUPPORT:
    return "Address family not supported by protocol family.";
  case WSAEALREADY: return "Operation already in progress.";
  case WSAECONNABORTED: return "Software caused connection abort.";
  case WSAECONNREFUSED: return "Connection refused.";
  case WSAECONNRESET: return "Connection reset by peer.";
  case WSAEDESTADDRREQ: return "Destination address required.";
  case WSAEFAULT: return "Bad address.";
  case WSAEHOSTDOWN: return "Host is down.";
  case WSAEHOSTUNREACH: return "No route to host.";
  case WSAEINPROGRESS: return "Operation now in progress.";
  case WSAEINTR: return "Interrupted function call.";
  case WSAEINVAL: return "Invalid argument.";
  case WSAEISCONN: return "Socket is already connected.";
  case WSAEMFILE: return "Too many open files.";
  case WSAEMSGSIZE: return "Message too long.";
  case WSAENETDOWN: return "Network is down.";
  case WSAENETRESET: return "Network dropped connection on reset.";
  case WSAENETUNREACH: return "Network is unreachable.";
  case WSAENOBUFS: return "No buffer space available.";
  case WSAENOPROTOOPT: return "Bad protocol option.";
  case WSAENOTCONN: return "Socket is not connected.";
  case WSAENOTSOCK: return "Socket operation on nonsocket.";
  case WSAEOPNOTSUPP: return "Operation not supported.";
  case WSAEPFNOSUPPORT: return "Protocol family not supported.";
  case WSAEPROCLIM: return "Too many processes.";
  case WSAEPROTONOSUPPORT: return "Protocol not supported.";
  case WSAEPROTOTYPE: return "Protocol wrong type for socket.";
  case WSAESHUTDOWN: return "Cannot send after socket shutdown.";
  case WSAESOCKTNOSUPPORT: return "Socket type not supported.";
  case WSAETIMEDOUT: return "Connection timed out.";
  case WSAEWOULDBLOCK: return "Resource temporarily unavailable.";
  case WSAHOST_NOT_FOUND: return "Host not found.";
  case WSANOTINITIALISED: return "Successful WSAStartup not yet performed.";
  case WSANO_DATA: return "Valid name, no data record of requested type.";
  case WSANO_RECOVERY: return "This is a nonrecoverable error.";
  case WSASYSNOTREADY: return "Network subsystem is unavailable.";
  case WSATRY_AGAIN: return "Nonauthoritative host not found.";
  case WSAVERNOTSUPPORTED: return "Winsock.dll version out of range.";
  case WSAEDISCON: return "Graceful shutdown in progress.";
  default: return "Unknown error.";

  /* OS dependent error numbers? */
  /*
  case WSATYPE_NOT_FOUND: return "Class type not found.";
  case WSA_INVALID_HANDLE: return "Specified event object handle is invalid.";
  case WSA_INVALID_PARAMETER: return "One or more parameters are invalid.";
  case WSAINVALIDPROCTABLE:
    return "Invalid procedure table from service provider.";
  case WSAINVALIDPROVIDER: return "Invalid service provider version number.";
  case WSA_IO_INCOMPLETE:
    return "Overlapped I/O event object not in signaled state.";
  case WSA_IO_PENDING: return "Overlapped operations will complete later.";
  case WSA_NOT_ENOUGH_MEMORY: return "Insufficient memory available.";
  case WSAPROVIDERFAILEDINIT:
    return "Unable to initialize a service provider.";
  case WSASYSCALLFAILURE: return "System call failure.";
  case WSA_OPERATION_ABORTED: return "Overlapped operation aborted.";
  */
  }
}

/* check some assumptions */
#if SOCKET_ERROR >= 0
#error need SOCKET_ERROR < 0
#endif

#endif /* END WINDOWS SOCKET STUFF  */




/****************************************************************************/
/* socket reading and writing functions.
 * the socket functions throw exceptions if there are errors, so you must call
 * them from within a TRY block.
 */

/* open a socket */

void transport_open (Transport *tpt)
{
  tpt->fd = socket (PF_INET,SOCK_STREAM,IPPROTO_TCP);
  if (tpt->fd == INVALID_TRANSPORT) THROW (sock_errno);
}

/* close a socket */

void transport_close (Transport *tpt)
{
  if (tpt->fd != INVALID_TRANSPORT) close (tpt->fd);
  tpt->fd = INVALID_TRANSPORT;
}


/* connect the socket to a host */

static void transport_connect (Transport *tpt, u32 ip_address, u16 ip_port)
{
  struct sockaddr_in myname;
  TRANSPORT_VERIFY_OPEN;
  myname.sin_family = AF_INET;
  myname.sin_port = htons (ip_port);
  myname.sin_addr.s_addr = htonl (ip_address);
  if (connect (tpt->fd, (struct sockaddr *) &myname, sizeof (myname)) != 0)
    THROW (sock_errno);
}


/* bind the socket to a given address/port. the address can be INADDR_ANY. */

static void transport_bind (Transport *tpt, u32 ip_address, u16 ip_port)
{
  struct sockaddr_in myname;
  TRANSPORT_VERIFY_OPEN;
  myname.sin_family = AF_INET;
  myname.sin_port = htons (ip_port);
  myname.sin_addr.s_addr = htonl (ip_address);
  if (bind (tpt->fd, (struct sockaddr *) &myname, sizeof (myname)) != 0)
    THROW (sock_errno);
}


/* listen for incoming connections, with up to `maxcon' connections
 * queued up.
 */

static void transport_listen (Transport *tpt, int maxcon)
{
  TRANSPORT_VERIFY_OPEN;
  if (listen (tpt->fd,maxcon) != 0) THROW (sock_errno);
}


/* accept an incoming connection, initializing `asock' with the new connection.
 */

void transport_accept (Transport *tpt, Transport *atpt)
{
  struct sockaddr_in clientname;
  size_t namesize;
  TRANSPORT_VERIFY_OPEN;
  namesize = sizeof (clientname);
  atpt->fd = accept (tpt->fd, (struct sockaddr*) &clientname, &namesize);
  if (atpt->fd == INVALID_TRANSPORT) THROW (sock_errno);
}


/* read from the socket into a buffer */

void transport_read_buffer (Transport *tpt, const u8 *buffer, int length)
{
  TRANSPORT_VERIFY_OPEN;
  while (length > 0) {
    int n = read (tpt->fd,(void*) buffer,length);
    if (n == 0) THROW (ERR_EOF);
    if (n < 0) THROW (sock_errno);
    buffer += n;
    length -= n;
  }
}

/* write a buffer to the socket */

void transport_write_buffer (Transport *tpt, const u8 *buffer, int length)
{
  int n;
  TRANSPORT_VERIFY_OPEN;
  n = write (tpt->fd,buffer,length);
  if (n != length) THROW (sock_errno);
}

int transport_open_connection(lua_State *L, Handle *handle)
{
	int ip_port;
  u32 ip_address;
  struct hostent *host;

  check_num_args (L,2);
  if (!lua_isstring (L,1))
    my_lua_error (L,"first argument must be an ip address string");
  ip_port = get_port_number (L,2);

  host = gethostbyname (lua_tostring (L,1));
  if (!host) {
    deal_with_error (L,0,"could not resolve internet address");
    lua_pushnil (L);
    return 1;
  }

  if (host->h_addrtype != AF_INET || host->h_length != 4) {
    deal_with_error (L,0,"not an internet IPv4 address");
    lua_pushnil (L);
    return 1;
  }
  ip_address = ntohl ( *((u32*)host->h_addr_list[0]) );

  /* make handle */
  handle = handle_create(L);

  /* connect the transport to the target server */
  transport_connect (&handle->tpt,ip_address,(u16) ip_port);

	return 0;
}


void transport_open_listener(Transport *tpt, int port)
{
	transport_open (tpt);
  transport_bind (tpt,INADDR_ANY,(u16) port);
  transport_listen (tpt,MAXCON);
}

/* see if there is any data to read from a socket, without actually reading
 * it. return 1 if data is available, on 0 if not. if this is a listening
 * socket this returns 1 if a connection is available or 0 if not.
 */

int transport_readable (Transport *tpt)
{
  fd_set set;
  struct timeval tv;
  int ret;
  if (tpt->fd == INVALID_TRANSPORT) return 0;
  FD_ZERO (&set);
  FD_SET (tpt->fd,&set);
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  ret = select (tpt->fd + 1,&set,0,0,&tv);
  return (ret > 0);
}
 