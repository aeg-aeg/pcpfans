/*
 * Copyright (c) 2012-2013 Red Hat.
 * Copyright (c) 2002 Silicon Graphics, Inc.  All Rights Reserved.
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

static int	timeToDie;		/* For SIGINT handling */
static char	*logfile = "pmproxy.log";	/* log file name */
static int	run_daemon = 1;		/* run as a daemon, see -f */
static char	*fatalfile = "/dev/tty";/* fatal messages at startup go here */
static char	*username = "pcp";
static char	*certdb;		/* certificate DB path (NSS) */
static char	*dbpassfile;		/* certificate DB password file */
static char	pmproxy_host[MAXHOSTNAMELEN];
static int	pmproxy_port;

/*
 * For maintaining info about a request port that clients may connect to
 * pmproxy on
 */
typedef struct {
    int		fd;		/* File descriptor */
    char*	ipSpec;		/* String used to specify IP addr (or NULL) */
} ReqPortInfo;

/*
 * A list of the ports that pmproxy is listening for client connections on
 */
static unsigned		nReqPorts = 0;	/* number of ports */
static unsigned		szReqPorts = 0;	/* capacity of ports array */
static ReqPortInfo	*reqPorts = NULL;	/* ports array */
int			maxReqPortFd = -1;	/* highest request port file descriptor */

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

    if (nReqPorts == szReqPorts)
	GrowReqPorts();
    rp = &reqPorts[nReqPorts];

    rp->fd = -1;
    if (ipSpec == NULL)
	ipSpec = "INADDR_ANY";
    rp->ipSpec = strdup(ipSpec);
    nReqPorts++;

#ifdef PCP_DEBUG
    if (pmDebug & DBG_TRACE_APPL0)
	fprintf(stderr, "AddRequestPort: %s\n", rp->ipSpec);
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

    while ((c = getopt(argc, argv, "C:D:fi:l:L:P:U:x:?")) != EOF)
	switch (c) {

	    case 'C':	/* path to NSS certificate database */
		certdb = optarg;
		break;

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
		    fputs ("pmproxy: -L requires a positive value\n", stderr);
		    errflag++;
		} else {
		    __pmSetPDUCeiling (val);
		}
		break;

	    case 'P':	/* password file for certificate database access */
		dbpassfile = optarg;
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
"  -C dirname      path to NSS certificate database\n"
"  -f              run in the foreground\n" 
"  -i ipaddress    accept connections on this IP address\n"
"  -l logfile      redirect diagnostics and trace output\n"
"  -L bytes        maximum size for PDUs from clients [default 65536]\n"
"  -P passfile     password file for certificate database access\n"
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
 * ipSpec is the IP address that the port is advertised for.
 * To allow connections to all this host's IP addresses from clients
 * use ipSpec = "INADDR_ANY".
 */
static int
OpenRequestSocket(const char * ipSpec)
{
    int			fd;
    int			sts;
    __pmSockAddr	*myAddr;
    int			one = 1;

    fd = __pmCreateSocket();
    if (fd < 0) {
	__pmNotifyErr(LOG_ERR, "OpenRequestSocket(%d) socket: %s\n",
			pmproxy_port, netstrerror());
	DontStart();
    }
    if (fd > maxSockFd)
	maxSockFd = fd;
    __pmFD_SET(fd, &sockFds);

#ifndef IS_MINGW
    /* Ignore dead client connections */
    if (__pmSetSockOpt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &one,
			(__pmSockLen)sizeof(one)) < 0) {
	__pmNotifyErr(LOG_ERR,
		"OpenRequestSocket(%d) __pmSetSockopt(SO_REUSEADDR): %s\n",
		pmproxy_port, netstrerror());
	DontStart();
    }
#else
    /* see MSDN tech note: "Using SO_REUSEADDR and SO_EXCLUSIVEADDRUSE" */
    if (__pmSetSockOpt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *) &one,
			(__pmSockLen)sizeof(one)) < 0) {
	__pmNotifyErr(LOG_ERR,
		"OpenRequestSocket(%d) __pmSetSockOpt(SO_EXCLUSIVEADDRUSE): %s\n",
		pmproxy_port, netstrerror());
	DontStart();
    }
#endif

    /* and keep alive please - pv 916354 bad networks eat fds */
    if (__pmSetSockOpt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *)&one,
			(__pmSockLen)sizeof(one)) < 0) {
	__pmNotifyErr(LOG_ERR,
		"OpenRequestSocket(%d, %s) __pmSetSockOpt(SO_KEEPALIVE): %s\n",
		pmproxy_port, ipSpec, netstrerror());
	DontStart();
    }

    /* Initialize the socket address */
    if ((myAddr = __pmStringToSockAddr(ipSpec)) == 0) {
	__pmNotifyErr(LOG_ERR, "OpenRequestSocket(%d, %s) invalid address\n",
		      pmproxy_port, ipSpec);
	DontStart();
    }
    __pmSockAddrSetFamily(myAddr, AF_INET);
    __pmSockAddrSetPort(myAddr, pmproxy_port);

    sts = __pmBind(fd, (void *)myAddr, __pmSockAddrSize());
    __pmSockAddrFree(myAddr);
    if (sts < 0) {
	__pmNotifyErr(LOG_ERR, "OpenRequestSocket(%d) __pmBind: %s\n",
			pmproxy_port, netstrerror());
	if (neterror() == EADDRINUSE)
	    __pmNotifyErr(LOG_ERR, "pmproxy is already running\n");
	DontStart();
    }

    sts = __pmListen(fd, 5);	/* Max. of 5 pending connection requests */
    if (sts == -1) {
	__pmNotifyErr(LOG_ERR, "OpenRequestSocket(%d) __pmListen: %s\n",
			pmproxy_port, netstrerror());
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

static int
VerifyClient(ClientInfo *cp, __pmPDU *pb)
{
    int	i, sts, flags = 0, sender = 0, credcount = 0;
    __pmPDUHdr *header = (__pmPDUHdr *)pb;
    __pmCred *credlist;

    /* first check that this is a credentials PDU */
    if (header->type != PDU_CREDS)
	return PM_ERR_IPC;

    /* now decode it and if secure connection requested, set it up */
    if ((sts = __pmDecodeCreds(pb, &sender, &credcount, &credlist)) < 0)
	return sts;

    for (i = 0; i < credcount; i++) {
	if (credlist[i].c_type == CVERSION) {
	    __pmVersionCred *vcp = (__pmVersionCred *)&credlist[i];
	    flags = vcp->c_flags;
	    break;
	}
    }
    if (credlist != NULL)
	free(credlist);

    /* need to ensure both the pmcd and client channel use flags */

    if (sts >= 0 && flags)
	sts = __pmSecureServerHandshake(cp->fd, flags);
	
    /* send credentials PDU through to pmcd now (order maintained) */
    if (sts >= 0)
	sts = __pmXmitPDU(cp->pmcd_fd, pb);

    /* finally perform any additional handshaking needed with pmcd */
    if (sts >= 0 && flags)
	sts = __pmSecureClientHandshake(cp->pmcd_fd, flags, pmproxy_host);
   
    return sts;
}

/* Determine which clients (if any) have sent data to the server and handle it
 * as required.
 */
void
HandleInput(__pmFdSet *fdsPtr)
{
    int		i, sts;
    __pmPDU	*pb;
    ClientInfo	*cp;

    /* input from clients */
    for (i = 0; i < nClients; i++) {
	if (!client[i].status.connected || !__pmFD_ISSET(client[i].fd, fdsPtr))
	    continue;

	cp = &client[i];

	sts = __pmGetPDU(cp->fd, LIMIT_SIZE, 0, &pb);
	if (sts <= 0) {
	    CleanupClient(cp, sts);
	    continue;
	}

	/* We *must* see a credentials PDU as the first PDU */
	if (!cp->status.allowed) {
	    sts = VerifyClient(cp, pb);
	    __pmUnpinPDUBuf(pb);
	    if (sts < 0) {
		CleanupClient(cp, sts);
		continue;
	    }
	    cp->status.allowed = 1;
	    continue;
	}

	sts = __pmXmitPDU(cp->pmcd_fd, pb);
	__pmUnpinPDUBuf(pb);
	if (sts <= 0) {
	    CleanupClient(cp, sts);
	    continue;
	}
    }

    /* input from pmcds */
    for (i = 0; i < nClients; i++) {
	if (!client[i].status.connected ||
	    !__pmFD_ISSET(client[i].pmcd_fd, fdsPtr))
	    continue;

	cp = &client[i];

	sts = __pmGetPDU(cp->pmcd_fd, ANY_SIZE, 0, &pb);
	if (sts <= 0) {
	    CleanupClient(cp, sts);
	    continue;
	}

	sts = __pmXmitPDU(cp->fd, pb);
	__pmUnpinPDUBuf(pb);
	if (sts <= 0) {
	    CleanupClient(cp, sts);
	    continue;
	}
    }
}

/* Called to shutdown pmproxy in an orderly manner */

void
Shutdown(void)
{
    int	i;
    int	fd;

    for (i = 0; i < nClients; i++)
	if (client[i].status.connected)
	    __pmCloseSocket(client[i].fd);
    for (i = 0; i < nReqPorts; i++)
	if ((fd = reqPorts[i].fd) != -1)
	    __pmCloseSocket(fd);
    __pmSecureServerShutdown();
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
    int		i;

    if (fd >= -1 && fd < 3)
	return stdFds[fd + 1];
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
    int		maxFd;
    __pmFdSet	readableFds;
    ClientInfo	*cp;

    for (;;) {
	/* Figure out which file descriptors to wait for input on.  Keep
	 * track of the highest numbered descriptor for the select call.
	 */
	readableFds = sockFds;
	maxFd = maxSockFd + 1;

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
		int rfd = reqPorts[i].fd;
		if (__pmFD_ISSET(rfd, &readableFds)) {
		    cp = AcceptNewClient(rfd);
		    if (cp == NULL) {
			/* failed to negotiate correctly, already cleaned up */
			continue;
		    }
		    /*
		     * make connection to pmcd
		     */
		    if ((cp->pmcd_fd = __pmAuxConnectPMCDPort(cp->pmcd_hostname, cp->pmcd_port)) < 0) {
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
			if (cp->pmcd_fd > maxSockFd)
			    maxSockFd = cp->pmcd_fd;
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

/*
 * extract runtime details:
 * - any PMPROXY_PORT setting from the environment
 * - hostname extracted and cached for later use during protocol negotiations
 */
static void
ProxyEnvironment(void)
{
    struct hostent	*hep = NULL;
    char		host[MAXHOSTNAMELEN];
    char		*env_str;
    char		*end_ptr;

    if ((env_str = getenv("PMPROXY_PORT")) != NULL) {
	pmproxy_port = (int)strtol(env_str, &end_ptr, 0);
	if (*end_ptr != '\0' || pmproxy_port < 0) {
	    __pmNotifyErr(LOG_WARNING,
			 "main: ignored bad PMPROXY_PORT = '%s'\n", env_str);
	    pmproxy_port = PROXY_PORT;
	}
    }
    else
	pmproxy_port = PROXY_PORT;

    /* nathans TODO - fix this up - use newer APIs */
    if (gethostname(host, MAXHOSTNAMELEN) < 0) {
        __pmNotifyErr(LOG_ERR, "gethostname failure\n");
        DontStart();
    }
    host[MAXHOSTNAMELEN-1] = '\0';
    hep = gethostbyname(host);
    strncpy(pmproxy_host, hep ? hep->h_name : host, MAXHOSTNAMELEN);
    pmproxy_host[MAXHOSTNAMELEN-1] = '\0';
}

int
main(int argc, char *argv[])
{
    int		i;
    int		status;
    unsigned	nReqPortsOK = 0;

    umask(022);
    __pmSetProgname(argv[0]);
    __pmSetInternalState(PM_STATE_PMCS);

    ParseOptions(argc, argv);
    ProxyEnvironment();

    if (run_daemon) {
	fflush(stderr);
	StartDaemon(argc, argv);
    }

    __pmSetSignalHandler(SIGHUP, SIG_IGN);
    __pmSetSignalHandler(SIGINT, SigIntProc);
    __pmSetSignalHandler(SIGTERM, SigIntProc);
    __pmSetSignalHandler(SIGBUS, SigBad);
    __pmSetSignalHandler(SIGSEGV, SigBad);

    /* If no -i IP_ADDR options specified, allow connections on any IP number */
    if (nReqPorts == 0)
	AddRequestPort(NULL);

    /* Open request ports for client connections */
    for (i = 0; i < nReqPorts; i++) {
	int fd = OpenRequestSocket(reqPorts[i].ipSpec);
	if (fd != -1) {
	    reqPorts[i].fd = fd;
	    if (fd > maxReqPortFd)
		maxReqPortFd = fd;
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
	  "  sts fd   IP addr\n"
	  "  === ==== ========\n", stderr);
    for (i = 0; i < nReqPorts; i++) {
	ReqPortInfo *rp = &reqPorts[i];
	fprintf(stderr, "  %s %4d %s\n",
		(rp->fd != -1) ? "ok " : "err",
		rp->fd,
		rp->ipSpec ? rp->ipSpec : "(any address)");
    }
    fflush(stderr);

    /* lose root privileges if we have them */
    __pmSetProcessIdentity(username);

    if (__pmSecureServerSetup(certdb, dbpassfile) < 0)
	DontStart();

    /* all the work is done here */
    ClientLoop();

    Shutdown();
    exit(0);
}
#endif
