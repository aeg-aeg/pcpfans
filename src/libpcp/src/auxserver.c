/*
 * Copyright (c) 2013 Red Hat.
 * 
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 */

#include "pmapi.h"
#include "impl.h"
#define SOCKET_INTERNAL
#include "internal.h"

/*
 * Info about a request port that clients may connect to a server on
 */
enum {
    INET_FD     = 0,
    IPV6_FD,
    FAMILIES
};
typedef struct {
    int			fds[FAMILIES];	/* Inet and IPv6 File descriptors */
    int			port;		/* Listening port */
    const char		*address;	/* Network address string (or NULL) */
} ReqPortInfo;

static unsigned         nReqPorts;		/* number of ports */
static unsigned         szReqPorts;		/* capacity of ports array */
static ReqPortInfo      *reqPorts;      	/* ports array */

/*
 * Interfaces we're willing to listen for clients on, from -i
 */
static int	nintf;
static char	**intflist;

/*
 * Ports we're willing to listen for clients on, from -p (or env)
 */
static int	nport;
static int	*portlist;

int
__pmServerAddInterface(const char *address)
{
    size_t size = (nintf+1) * sizeof(char *);

    /* one (of possibly several) IP addresses for client requests */
    intflist = (char **)realloc(intflist, nintf * sizeof(char *));
    if (intflist == NULL)
	__pmNoMem("AddInterface: cannot grow interface list", size, PM_FATAL_ERR);
    intflist[nintf++] = strdup(address);
    return nintf;
}

int
__pmServerAddPorts(const char *ports)
{
    char	*endptr, *p = (char *)ports;

    /*
     * one (of possibly several) ports for client requests
     * ... accept a comma separated list of ports here
     */
    for ( ; ; ) {
	size_t	size = (nport + 1) * sizeof(int);
	int	port = (int)strtol(p, &endptr, 0);

	if ((*endptr != '\0' && *endptr != ',') || port < 0)
	    return -EINVAL;
	if ((portlist = (int *)realloc(portlist, size)) == NULL)
	    __pmNoMem("AddPorts: cannot grow port list", size, PM_FATAL_ERR);
	portlist[nport++] = port;
	if (*endptr == '\0')
	    break;
	p = &endptr[1];
    }
    return nport;
}

/* Increase the capacity of the reqPorts array (maintain the contents) */
static void
GrowRequestPorts(void)
{
    size_t	need;

    szReqPorts += 4;
    need = szReqPorts * sizeof(ReqPortInfo);
    reqPorts = (ReqPortInfo*)realloc(reqPorts, need);
    if (reqPorts == NULL) {
	__pmNoMem("GrowRequestPorts: can't grow request port array",
		need, PM_FATAL_ERR);
    }
}

/* Add a request port to the reqPorts array */
static int
AddRequestPort(const char *address, int port)
{
    ReqPortInfo	*rp;

    if (address == NULL)
	address = "INADDR_ANY";

    if (nReqPorts == szReqPorts)
	GrowRequestPorts();
    rp = &reqPorts[nReqPorts];
    rp->fds[INET_FD] = -1;
    rp->fds[IPV6_FD] = -1;
    rp->address = address;
    rp->port = port;
    nReqPorts++;

#ifdef PCP_DEBUG
    if (pmDebug & DBG_TRACE_APPL0)
        fprintf(stderr, "AddRequestPort: %s port %d\n", rp->address, rp->port);
#endif

    return nReqPorts;   /* success */
}

static int
SetupRequestPorts(void)
{
    int	i, n;

    /* check for duplicate ports, remove duplicates */
    for (i = 0; i < nport; i++) {
	for (n = i + 1; n < nport; n++) {
	    if (portlist[i] == portlist[n])
		break;
	}
	if (n < nport) {
	    __pmNotifyErr(LOG_WARNING,
		"%s: duplicate client request port (%d) will be ignored\n",
                     pmProgname, portlist[n]);
	    portlist[n] = -1;
	}
    }

    if (nintf == 0) {
	/* no interface options specified, allow connections on any address */
	for (n = 0; n < nport; n++) {
	    if (portlist[n] != -1)
		AddRequestPort(NULL, portlist[n]);
	}
    }
    else {
	for (i = 0; i < nintf; i++) {
	    for (n = 0; n < nport; n++) {
		if (portlist[n] == -1 || intflist[i] == NULL)
		    continue;
		AddRequestPort(intflist[i], portlist[n]);
	    }
	}
    }
    return 0;
}

static const char *
AddressFamily(int family)
{
    if (family == AF_INET)
	return "inet";
    if (family == AF_INET6)
	return "ipv6";
    return "unknown";
}

/*
 * Create socket for incoming connections and bind to it an address for
 * clients to use.  Returns -1 on failure.
 *
 * address is a string representing the Inet/IPv6 address that the port
 * is advertised for.  To allow connections to all this host's internet
 * addresses from clients use address = "INADDR_ANY".
 * On input, 'family' is a pointer to the address family to use (AF_INET
 * or AF_INET6) if the address specified is empty.  If the spec is not
 * empty then family is ignored and is set to the actual address family
 * used.
 */
static int
OpenRequestSocket(int port, const char *address, int *family,
		  int backlog, __pmFdSet *fdset, int *maximum)
{
    int			fd = -1;
    int			one, sts;
    __pmSockAddr	*myAddr;

    if ((myAddr = __pmStringToSockAddr(address)) == NULL) {
	__pmNotifyErr(LOG_ERR, "OpenRequestSocket(%d, %s) invalid address\n",
		      port, address);
	goto fail;
    }

    /*
     * If the address is unspecified, then use the address family we
     * have been given, otherwise the family will be determined by
     * __pmStringToSockAddr.
     */
    if (address == NULL || strcmp(address, "INADDR_ANY") == 0)
        __pmSockAddrSetFamily(myAddr, *family);
    else
        *family = __pmSockAddrGetFamily(myAddr);
    __pmSockAddrSetPort(myAddr, port);

    /* Create the socket. */
    if (*family == AF_INET)
        fd = __pmCreateSocket();
    else if (*family == AF_INET6)
        fd = __pmCreateIPv6Socket();
    else {
	__pmNotifyErr(LOG_ERR, "OpenRequestSocket(%d, %s) invalid address family: %d\n",
		      port, address, *family);
	goto fail;
    }

    if (fd < 0) {
	__pmNotifyErr(LOG_ERR, "OpenRequestSocket(%d, %s, %s) __pmCreateSocket: %s\n",
		port, address, AddressFamily(*family), netstrerror());
	goto fail;
    }

    /* Ignore dead client connections */
    one = 1;
#ifndef IS_MINGW
    if (__pmSetSockOpt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&one,
		(__pmSockLen)sizeof(one)) < 0) {
	__pmNotifyErr(LOG_ERR,
		"OpenRequestSocket(%d, %s, %s) __pmSetSockOpt(SO_REUSEADDR): %s\n",
		port, address, AddressFamily(*family), netstrerror());
	goto fail;
    }
#else
    if (__pmSetSockOpt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *)&one,
		(__pmSockLen)sizeof(one)) < 0) {
	__pmNotifyErr(LOG_ERR,
		"OpenRequestSocket(%d, %s, %s) __pmSetSockOpt(EXCLUSIVEADDRUSE): %s\n",
		port, address, AddressFamily(*family), netstrerror());
	goto fail;
    }
#endif

    /* and keep alive please - bad networks eat fds */
    if (__pmSetSockOpt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *)&one,
		(__pmSockLen)sizeof(one)) < 0) {
	__pmNotifyErr(LOG_ERR,
		"OpenRequestSocket(%d, %s, %s) __pmSetSockOpt(SO_KEEPALIVE): %s\n",
		port, address, AddressFamily(*family), netstrerror());
	goto fail;
    }

    sts = __pmBind(fd, (void *)myAddr, __pmSockAddrSize());
    __pmSockAddrFree(myAddr);
    myAddr = NULL;
    if (sts < 0) {
	sts = neterror();
	__pmNotifyErr(LOG_ERR, "OpenRequestSocket(%d, %s, %s) __pmBind: %s\n",
		port, address, AddressFamily(*family), netstrerror());
	if (sts == EADDRINUSE)
	    __pmNotifyErr(LOG_ERR, "%s may already be running\n", pmProgname);
	goto fail;
    }

    sts = __pmListen(fd, backlog);	/* Max. pending connection requests */
    if (sts < 0) {
	__pmNotifyErr(LOG_ERR, "OpenRequestSocket(%d, %s, %s) __pmListen: %s\n",
		port, address, AddressFamily(*family), netstrerror());
	goto fail;
    }

    if (fd > *maximum)
	*maximum = fd;
    __pmFD_SET(fd, fdset);
    return fd;

fail:
    if (fd != -1)
        __pmCloseSocket(fd);
    if (myAddr)
        __pmSockAddrFree(myAddr);
    return -1;
}

/*
 * Open request ports for client connections.
 * Open an inet and an IPv6 socket for clients, if appropriate.
 */
static int
OpenRequestPorts(__pmFdSet *fdset, int backlog)
{
    int i, fd, family, success = 0, maximum = -1;
    int with_ipv6 = strcmp(__pmGetAPIConfig("ipv6"), "true") == 0;

    for (i = 0; i < nReqPorts; i++) {
	ReqPortInfo	*rp = &reqPorts[i];

	/*
	 * If the spec is NULL or "INADDR_ANY", then we open one socket
	 * for each address family (inet, IPv6).  Otherwise, the address
	 * family will be determined by OpenRequestSocket.  Reporting of
	 * all errors is left to OpenRequestSocket to avoid doubling up.
	 */
	if (rp->address == NULL || strcmp(rp->address, "INADDR_ANY") == 0) {
	    family = AF_INET;
	    if ((fd = OpenRequestSocket(rp->port, rp->address, &family,
					backlog, fdset, &maximum)) >= 0) {
	        rp->fds[INET_FD] = fd;
		success = 1;
	    }
	    if (with_ipv6) {
		family = AF_INET6;
		if ((fd = OpenRequestSocket(rp->port, rp->address, &family,
					    backlog, fdset, &maximum)) >= 0) {
		    rp->fds[IPV6_FD] = fd;
		    success = 1;
		}
	    }
	    else
		rp->fds[IPV6_FD] = -EPROTO;
	}
	else {
	    if ((fd = OpenRequestSocket(rp->port, rp->address, &family,
					backlog, fdset, &maximum)) >= 0) {
	        if (family == AF_INET) {
		    rp->fds[INET_FD] = fd;
		    success = 1;
		}
		else if (family == AF_INET6) {
		    rp->fds[IPV6_FD] = fd;
		    success = 1;
		}
	    }
	}
    }
    if (success)
	return maximum;

    __pmNotifyErr(LOG_ERR, "%s: can't open any request ports, exiting\n",
		pmProgname);
    return -1;
}

int
__pmServerOpenRequestPorts(__pmFdSet *fdset, int backlog)
{
    int sts;

    if ((sts = SetupRequestPorts()) < 0)
	return sts;
    return OpenRequestPorts(fdset, backlog);
}

void
__pmServerCloseRequestPorts(void)
{
    int i, fd;

    for (i = 0; i < nReqPorts; i++) {
	if ((fd = reqPorts[i].fds[INET_FD]) >= 0)
	    __pmCloseSocket(fd);
	if ((fd = reqPorts[i].fds[IPV6_FD]) >= 0)
	    __pmCloseSocket(fd);
    }
}

/*
 * Accept any new client connections
 */
void
__pmServerAddNewClients(__pmFdSet *fdset, __pmServerCallback NewClient)
{
    int i, fd;

    for (i = 0; i < nReqPorts; i++) {
	/* Check both the inet and ipv6 fds. */
	if ((fd = reqPorts[i].fds[INET_FD]) >= 0)
	    NewClient(fdset, fd);
	if ((fd = reqPorts[i].fds[IPV6_FD]) >= 0)
	    NewClient(fdset, fd);
    }
}

static const char *
RequestFamilyString(int index)
{
    if (index == INET_FD)
	return "inet";
    if (index == IPV6_FD)
	return "ipv6";
    return "unknown";
}

void
__pmServerDumpRequestPorts(FILE *stream)
{
    int	i, j;

    fprintf(stream, "%s request port(s):\n"
	  "  sts fd   port  family address\n"
	  "  === ==== ===== ====== =======\n", pmProgname);

    for (i = 0; i < nReqPorts; i++) {
	ReqPortInfo *rp = &reqPorts[i];
	for (j = 0; j < FAMILIES; j++) {
	    if (rp->fds[j] != -EPROTO)
		fprintf(stderr, "  %-3s %4d %5d %-6s %s\n",
		    (rp->fds[j] != -1) ? "ok" : "err",
		    rp->fds[j], rp->port, RequestFamilyString(j),
		    rp->address ? rp->address : "(any address)");
	}
    }
}

char *
__pmServerRequestPortString(int fd, char *buffer, size_t sz)
{
    int i, j;

    for (i = 0; i < nReqPorts; i++) {
	ReqPortInfo *rp = &reqPorts[i];
	for (j = 0; j < FAMILIES; j++) {
	    if (fd == rp->fds[j]) {
		snprintf(buffer, sz, "%s %s request socket %s",
			pmProgname, RequestFamilyString(j), rp->address);
		return buffer;
	    }
	}
    }
    return NULL;
}

#if !defined(HAVE_SECURE_SOCKETS)

int
__pmSecureServerSetup(const char *db, const char *passwd)
{
    (void)db;
    (void)passwd;
    return 0;
}

int
__pmSecureServerIPCFlags(int fd, int flags)
{
    (void)fd;
    (void)flags;
    return -EOPNOTSUPP;
}

void
__pmSecureServerShutdown(void)
{
    /* nothing to do here */
}

int
__pmSecureServerHandshake(int fd, int flags)
{
    (void)fd;
    (void)flags;
    return -EOPNOTSUPP;
}

int
__pmServerHasFeature(__pmServerFeature query)
{
    if (query == PM_SERVER_FEATURE_IPV6)
	return 1;
    return 0;
}

#endif /* !HAVE_SECURE_SOCKETS */
