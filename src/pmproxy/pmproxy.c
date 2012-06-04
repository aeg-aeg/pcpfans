/*
 * Copyright (c) 2002 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (c) 2012 Red Hat.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include "pmproxy.h"
#include <sys/stat.h>
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

int 		proxy_hi_openfds = -1;   /* Highest known file descriptor for pmproxy */

static int	timeToDie;		/* For SIGINT handling */
static char	*logfile = "pmproxy.log";	/* log file name */
static int	run_daemon = 1;		/* run as a daemon, see -f */
static char	*fatalfile = "/dev/tty";/* fatal messages at startup go here */
static char	*username;

/*
 * For maintaining info about a request port that clients may connect to
 * pmproxy on
 */
typedef struct {
    __pmFD	fd;		/* File descriptor */
    char*	ipSpec;		/* String used to specify IP addr (or NULL) */
    __uint32_t	ipAddr;		/* IP address (network byte order) */
} ReqPortInfo;

/*
 * A list of the ports that pmproxy is listening for client connections on
 */
static unsigned		nReqPorts = 0;	/* number of ports */
static unsigned		szReqPorts = 0;	/* capacity of ports array */
static ReqPortInfo	*reqPorts = NULL;	/* ports array */
__pmFD			maxReqPortFd = PM_ERROR_FD; /* highest request port file descriptor */

static void
DontStart(void)
{
    FILE	*tty;
    FILE	*log;
    __pmNotifyErr(LOG_ERR, "pmproxy not started due to errors!\n");

    if ((tty = fopen(fatalfile, "w")) != NULL) {
	fflush(stderr);
	fprintf(tty, "NOTE: pmproxy not started due to errors!  ");
	if ((log = fopen(logfile, "r")) != NULL) {
	    int		c;
	    fprintf(tty, "Log file \"%s\" contains ...\n", logfile);
	    while ((c = fgetc(log)) != EOF)
		fputc(c, tty);
	    fclose(log);
	}
	else
	    fprintf(tty, "Log file \"%s\" has vanished!\n", logfile);
	fclose(tty);
    }
    exit(1);
}

/* Increase the capacity of the reqPorts array (maintain the contents) */

static void
GrowReqPorts(void)
{
    size_t need;
    szReqPorts += 4;
    need = szReqPorts * sizeof(ReqPortInfo);
    reqPorts = (ReqPortInfo*)realloc(reqPorts, need);
    if (reqPorts == NULL) {
	__pmNoMem("pmproxy: can't grow request port array", need, PM_FATAL_ERR);
    }
}

/* Add a request port to the reqPorts array */

static int
AddRequestPort(char *ipSpec)
{
    ReqPortInfo		*rp;
    char		*sp = ipSpec;
    char		*endp;
    u_long		addr = 0;
    int			i;

    if (nReqPorts == szReqPorts)
	GrowReqPorts();
    rp = &reqPorts[nReqPorts];

    rp->fd = PM_ERROR_FD;
    if (ipSpec) {
	for (i = 0; i < 4; i++) {
	    unsigned long part = strtoul(sp, &endp, 10);

	    if (*endp != ((i < 3) ? '.' : '\0'))
		return 0;
	    if (part > 255)
		return 0;
	    addr |= part << (8 * (3 - i));
	    if (i < 3)
		sp = endp + 1;
	}
    }
    else {
	ipSpec = "INADDR_ANY";
	addr = INADDR_ANY;
    }
    rp->ipSpec = strdup(ipSpec);
    rp->ipAddr = (__uint32_t)htonl(addr);
    nReqPorts++;

#ifdef PCP_DEBUG
    if (pmDebug & DBG_TRACE_APPL0)
	fprintf(stderr, "AddRequestPort: %s -> %08lx -> %08x\n",
		rp->ipSpec, addr, rp->ipAddr);
#endif
    return 1;	/* success */

}

static void
ParseOptions(int argc, char *argv[])
{
    int		c;
    int		sts;
    int		errflag = 0;
    int		usage = 0;
    int		val;

    while ((c = getopt(argc, argv, "D:fi:l:L:U:x:?")) != EOF)
	switch (c) {

	    case 'D':	/* debug flag */
		sts = __pmParseDebug(optarg);
		if (sts < 0) {
		    fprintf(stderr, "%s: unrecognized debug flag specification (%s)\n",
			pmProgname, optarg);
		    errflag++;
		}
		pmDebug |= sts;
		break;

	    case 'f':
		/* foreground, i.e. do _not_ run as a daemon */
		run_daemon = 0;
		break;

	    case 'i':
		/* one (of possibly several) IP addresses for client requests */
		if (!AddRequestPort(optarg)) {
		    fprintf(stderr, "pmproxy: bad IP spec: -i %s\n", optarg);
		    errflag++;
		}
		break;

	    case 'l':
		/* log file name */
		logfile = optarg;
		break;

	    case 'L': /* Maximum size for PDUs from clients */
		val = (int)strtol (optarg, NULL, 0);
		if ( val <= 0 ) {
		    fputs ("pmproxy: -L require a posivite value\n", stderr);
		    errflag++;
		} else {
		    __pmSetPDUCeiling (val);
		}
		break;

	    case 'U':
		/* run as user username */
		username = optarg;
		break;

	    case 'x':
		fatalfile = optarg;
		break;

	    case '?':
		usage = 1;
		break;

	    default:
		errflag++;
		break;
	}

    if (usage ||errflag || optind < argc) {
	fprintf(stderr,
"Usage: %s [options]\n\n"
"Options:\n"
"  -f              run in the foreground\n" 
"  -i ipaddress    accept connections on this IP address\n"
"  -l logfile      redirect diagnostics and trace output\n"
"  -L bytes        maximum size for PDUs from clients [default 65536]\n"
"  -U username     assume identity of username (only when run as root)\n"
"  -x file         fatal messages at startup sent to file [default /dev/tty]\n",
			pmProgname);
	if (usage)
	    exit(0);
	else
	    DontStart();
    }
}

/* Create socket for incoming connections and bind to it an address for
 * clients to use.  Only returns if it succeeds (exits on failure).
 * ipAddr is the IP address that the port is advertised for (in network byte
 * order, see htonl(3N)).  To allow connections to all this host's IP addresses
 * from clients use ipAddr = htonl(INADDR_ANY).
 */
static int
OpenRequestSocket(int port, __uint32_t ipAddr)
{
    __pmFD		fd;
    int			sts;
    __pmSockAddrIn	myAddr;
    int			one = 1;

    fd = __pmCreateSocket();
    if (fd == PM_ERROR_FD) {
	__pmNotifyErr(LOG_ERR, "OpenRequestSocket(%d) socket: %s\n",
			port, netstrerror());
	DontStart();
    }
    maxSockFd = __pmUpdateMaxFD(fd, maxSockFd);
    __pmFD_SET(fd, &sockFds);

#ifndef IS_MINGW
    /* Ignore dead client connections */
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &one,
			(mysocklen_t)sizeof(one)) < 0) {
	__pmNotifyErr(LOG_ERR,
		"OpenRequestSocket(%d) setsockopt(SO_REUSEADDR): %s\n",
		port, netstrerror());
	DontStart();
    }
#else
    /* see MSDN tech note: "Using SO_REUSEADDR and SO_EXCLUSIVEADDRUSE" */
    if (setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *) &one,
			(mysocklen_t)sizeof(one)) < 0) {
	__pmNotifyErr(LOG_ERR,
		"OpenRequestSocket(%d) setsockopt(SO_EXCLUSIVEADDRUSE): %s\n",
		port, netstrerror());
	DontStart();
    }
#endif

    /* and keep alive please - pv 916354 bad networks eat fds */
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *)&one,
			(mysocklen_t)sizeof(one)) < 0) {
	__pmNotifyErr(LOG_ERR,
		"OpenRequestSocket(%d, 0x%x) setsockopt(SO_KEEPALIVE): %s\n",
		port, ipAddr, netstrerror());
	DontStart();
    }

    memset(&myAddr, 0, sizeof(myAddr));
    myAddr.sin_family = AF_INET;
    myAddr.sin_addr.s_addr = ipAddr;
    myAddr.sin_port = htons(port);
    sts = __pmBind(fd, (__pmSockAddr*)&myAddr, sizeof(myAddr));
    if (sts < 0){
	__pmNotifyErr(LOG_ERR, "OpenRequestSocket(%d) bind: %s\n",
			port, netstrerror());
	__pmNotifyErr(LOG_ERR, "pmproxy is already running\n");
	DontStart();
    }

    sts = __pmListen(fd, 5);	/* Max. of 5 pending connection requests */
    if (sts == -1) {
	__pmNotifyErr(LOG_ERR, "OpenRequestSocket(%d) listen: %s\n",
			port, netstrerror());
	DontStart();
    }
    return fd;
}

static void
CleanupClient(ClientInfo *cp, int sts)
{
#ifdef PCP_DEBUG
    if (pmDebug & DBG_TRACE_APPL0) {
	int		i;
	for (i = 0; i < nClients; i++) {
	    if (cp == &client[i])
		break;
	}
	fprintf(stderr, "CleanupClient: client[%d] fd=%d %s (%d)\n",
	    i, cp->fd, pmErrStr(sts), sts);
    }
#endif

    DeleteClient(cp);
}

/* Determine which clients (if any) have sent data to the server and handle it
 * as required.
 */
void
HandleInput(__pmFdSet *fdsPtr)
{
    int		ists;
    int		osts;
    int		i;
    __pmPDU	*pb;
    ClientInfo	*cp;

    /* input from client */
    for (i = 0; i < nClients; i++) {
	if (!client[i].status.connected || !__pmFD_ISSET(client[i].fd, fdsPtr))
	    continue;

	cp = &client[i];

	/*
	 * TODO new read logic
	 *	- read len
	 *	- read pdu body
	 *	- forward
	 * need pmcd fds in select mask
	 * need to map in-fd to out-fd
	 * handle any input here, not just clients ... or treat pmcd
	 * connections as clients also?
	 */
	ists = __pmGetPDU(cp->fd, LIMIT_SIZE, 0, &pb);
	if (ists <= 0) {
	    CleanupClient(cp, ists);
	    continue;
	}

	osts = __pmXmitPDU(cp->pmcd_fd, pb);
	__pmUnpinPDUBuf(pb);
	if (osts <= 0) {
	    CleanupClient(cp, osts);
	    continue;
	}
    }


    /* input from pmcd */
    for (i = 0; i < nClients; i++) {
	if (!client[i].status.connected || !__pmFD_ISSET(client[i].pmcd_fd, fdsPtr))
	    continue;

	cp = &client[i];

	/*
	 * TODO new read logic
	 *	- read len
	 *	- read pdu body
	 *	- forward
	 * need pmcd fds in select mask
	 * need to map in-fd to out-fd
	 * handle any input here, not just clients ... or treat pmcd
	 * connections as clients also?
	 */
	ists = __pmGetPDU(cp->pmcd_fd, ANY_SIZE, 0, &pb);
	if (ists <= 0) {
	    CleanupClient(cp, ists);
	    continue;
	}

	osts = __pmXmitPDU(cp->fd, pb);
	__pmUnpinPDUBuf(pb);
	if (osts <= 0) {
	    CleanupClient(cp, osts);
	    continue;
	}
    }
}

/* Called to shutdown pmproxy in an orderly manner */

void
Shutdown(void)
{
    int	i;
    __pmFD	fd;

    for (i = 0; i < nClients; i++)
	if (client[i].status.connected)
	    __pmCloseSocket(client[i].fd);
    for (i = 0; i < nReqPorts; i++)
	if ((fd = reqPorts[i].fd) != PM_ERROR_FD)
	    __pmCloseSocket(fd);
    __pmNotifyErr(LOG_INFO, "pmproxy Shutdown\n");
    fflush(stderr);
}

void
SignalShutdown(void)
{
    __pmNotifyErr(LOG_INFO, "pmproxy caught SIGINT or SIGTERM\n");
    Shutdown();
    exit(0);
}

#ifdef PCP_DEBUG
/* Convert a file descriptor to a string describing what it is for. */
char*
FdToString(int fd)
{
#define FDNAMELEN 40
    static char fdStr[FDNAMELEN];
    static char *stdFds[4] = {"*UNKNOWN FD*", "stdin", "stdout", "stderr"};
    int		streamIx;
    int		i;

    streamIx = __pmStandardStreamIx(fd);
    if (streamIx < 3)
	return stdFds[streamIx + 1];
    for (i = 0; i < nReqPorts; i++) {
	if (fd == reqPorts[i].fd) {
	    sprintf(fdStr, "pmproxy request socket %s", reqPorts[i].ipSpec);
	    return fdStr;
	}
    }
    for (i = 0; i < nClients; i++) {
	if (client[i].status.connected && fd == client[i].fd) {
	    sprintf(fdStr, "client[%d] client socket", i);
	    return fdStr;
	}
	if (client[i].status.connected && fd == client[i].pmcd_fd) {
	    sprintf(fdStr, "client[%d] pmcd socket", i);
	    return fdStr;
	}
    }
    return stdFds[0];
}

/* Loop, synchronously processing requests from clients. */
static void
ClientLoop(void)
{
    int		i, sts;
    __pmFD	maxFd;
    __pmFdSet	readableFds;
    int		CheckClientAccess(ClientInfo *);
    ClientInfo	*cp;

    for (;;) {
	/* Figure out which file descriptors to wait for input on.  Keep
	 * track of the highest numbered descriptor for the select call.
	 */
	readableFds = sockFds;
	maxFd = __pmIncrFD(maxSockFd);

	sts = __pmSelectRead(maxFd, &readableFds, NULL);

	if (sts > 0) {
#ifdef PCP_DEBUG
	    if (pmDebug & DBG_TRACE_APPL0)
		for (i = 0; i <= maxSockFd; i++)
		    if (__pmFD_ISSET(i, &readableFds))
			fprintf(stderr, "__pmSelectRead(): from %s fd=%d\n", FdToString(i), i);
#endif
	    /* Accept any new client connections */
	    for (i = 0; i < nReqPorts; i++) {
	        __pmFD rfd = reqPorts[i].fd;
		if (__pmFD_ISSET(rfd, &readableFds)) {
		    cp = AcceptNewClient(rfd);
		    if (cp == NULL) {
			/* failed to negotiate correctly, already cleaned up */
			continue;
		    }
		    /*
		     * make connection to pmcd
		     */
		    if ((cp->pmcd_fd = __pmAuxConnectPMCDPort(cp->pmcd_hostname, cp->pmcd_port)) == PM_ERROR_FD) {
#ifdef PCP_DEBUG
			if (pmDebug & DBG_TRACE_CONTEXT)
			    /* append to message started in AcceptNewClient() */
			    fprintf(stderr, " oops!\n"
				"__pmAuxConnectPMCDPort(%s,%d) failed: %s\n",
				cp->pmcd_hostname, cp->pmcd_port,
				pmErrStr(-oserror()));
#endif
			CleanupClient(cp, -oserror());
		    }
		    else {
			maxSockFd = __pmUpdateMaxFD(cp->pmcd_fd, maxSockFd);
			__pmFD_SET(cp->pmcd_fd, &sockFds);
#ifdef PCP_DEBUG
			if (pmDebug & DBG_TRACE_CONTEXT)
			    /* append to message started in AcceptNewClient() */
			    fprintf(stderr, " fd=%d\n", cp->pmcd_fd);
#endif
		    }
		}
	    }

	    HandleInput(&readableFds);
	}
	else if (sts == -1 && neterror() != EINTR) {
	    __pmNotifyErr(LOG_ERR, "ClientLoop select: %s\n", netstrerror());
	    break;
	}
	if (timeToDie) {
	    SignalShutdown();
	    break;
	}
    }
}

static void
SigIntProc(int s)
{
#ifdef IS_MINGW
    SignalShutdown();
#else
    signal(SIGINT, SigIntProc);
    signal(SIGTERM, SigIntProc);
    timeToDie = 1;
#endif
}

static void
SigBad(int sig)
{
    __pmNotifyErr(LOG_ERR, "Unexpected signal %d ...\n", sig);
    fprintf(stderr, "\nDumping to core ...\n");
    fflush(stderr);
    abort();
}

int
main(int argc, char *argv[])
{
    int		i;
    int		status;
    char	*env_str;
    int		port;
    unsigned	nReqPortsOK = 0;

    umask(022);
    __pmSetProgname(argv[0]);
    __pmSetInternalState(PM_STATE_PMCS);

    ParseOptions(argc, argv);

    if (run_daemon) {
	fflush(stderr);
	StartDaemon(argc, argv);
    }

    __pmSetSignalHandler(SIGHUP, SIG_IGN);
    __pmSetSignalHandler(SIGINT, SigIntProc);
    __pmSetSignalHandler(SIGTERM, SigIntProc);
    __pmSetSignalHandler(SIGBUS, SigBad);
    __pmSetSignalHandler(SIGSEGV, SigBad);

    /*
     * get optional stuff from environment ...
     *	PMPROXY_PORT
     * ... and create sockets
     */
    if ((env_str = getenv("PMPROXY_PORT")) != NULL) {
	char	*end_ptr;

	port = (int)strtol(env_str, &end_ptr, 0);
	if (*end_ptr != '\0' || port < 0) {
	    __pmNotifyErr(LOG_WARNING,
			 "main: ignored bad PMPROXY_PORT = '%s'\n", env_str);
	    port = PROXY_PORT;
	}
    }
    else
	port = PROXY_PORT;

    /* If no -i IP_ADDR options specified, allow connections on any IP number */
    if (nReqPorts == 0)
	AddRequestPort(NULL);

    /* Open request ports for client connections */
    for (i = 0; i < nReqPorts; i++) {
	int fd = OpenRequestSocket(port, reqPorts[i].ipAddr);
	if (fd != PM_ERROR_FD) {
	    reqPorts[i].fd = fd;
	    maxReqPortFd = __pmUpdateMaxFD(fd, maxReqPortFd);
	    nReqPortsOK++;
	}
    }
    if (nReqPortsOK == 0) {
	__pmNotifyErr(LOG_ERR, "pmproxy: can't open any request ports, exiting\n");
	DontStart();
    }	

    __pmOpenLog("pmproxy", logfile, stderr, &status);
    /* close old stdout, and force stdout into same stream as stderr */
    fflush(stdout);
    close(fileno(stdout));
    if (dup(fileno(stderr)) == -1) {
	fprintf(stderr, "Warning: dup() failed: %s\n", pmErrStr(-oserror()));
    }

    fprintf(stderr, "pmproxy: PID = %" FMT_PID, getpid());
    fprintf(stderr, ", PDU version = %u\n", PDU_VERSION);
    fputs("pmproxy request port(s):\n"
	  "  sts fd  IP addr\n"
	  "  === === ========\n", stderr);
    for (i = 0; i < nReqPorts; i++) {
	ReqPortInfo *rp = &reqPorts[i];
	fprintf(stderr, "  %s %3d %08x %s\n",
		(rp->fd != PM_ERROR_FD) ? "ok " : "err",
		rp->fd, rp->ipAddr,
		rp->ipSpec ? rp->ipSpec : "(any address)");
    }
    fflush(stderr);

#ifdef HAVE_GETPWNAM
    /* lose root privileges if we have them */
    if (username) {
	struct passwd	*pw;

	if ((pw = getpwnam(username)) == 0) {
	    __pmNotifyErr(LOG_WARNING,
			"cannot find the user %s to switch to\n", username);
	    DontStart();
	}
	if (setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0) {
	    __pmNotifyErr(LOG_WARNING,
			"cannot switch to uid/gid of user %s\n", username);
	    DontStart();
	}
    }
#endif

    /* all the work is done here */
    ClientLoop();

    Shutdown();
    exit(0);
}
#endif
