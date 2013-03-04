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

#define MAXPENDING	5	/* maximum number of pending connections */
#define FDNAMELEN	40	/* maximum length of a fd description */
#define STRINGIFY(s)    #s
#define TO_STRING(s)    STRINGIFY(s)

#ifdef PCP_DEBUG
static char	*FdToString(int);
#endif

static int	timeToDie;		/* For SIGINT handling */
static char	*logfile = "pmproxy.log";	/* log file name */
static int	run_daemon = 1;		/* run as a daemon, see -f */
static char	*fatalfile = "/dev/tty";/* fatal messages at startup go here */
static char	*username;
static char	*certdb;		/* certificate DB path (NSS) */
static char	*dbpassfile;		/* certificate DB password file */
static char	hostname[MAXHOSTNAMELEN];

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

static void
ParseOptions(int argc, char *argv[], int *nports)
{
    int		c;
    int		sts;
    int		errflag = 0;
    int		usage = 0;
    int		val;

    while ((c = getopt(argc, argv, "C:D:fi:l:L:p:P:U:x:?")) != EOF)
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
		/* one (of possibly several) interfaces for client requests */
		__pmServerAddInterface(optarg);
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

	    case 'p':
		if (__pmServerAddPorts(optarg) < 0) {
		    fprintf(stderr,
			"pmproxy: -p requires a positive numeric argument (%s)\n",
			optarg);
		    errflag++;
		} else {
		    *nports += 1;
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

    if (usage || errflag || optind < argc) {
	fprintf(stderr,
"Usage: %s [options]\n\n"
"Options:\n"
"  -C dirname      path to NSS certificate database\n"
"  -f              run in the foreground\n" 
"  -i ipaddress    accept connections on this IP address\n"
"  -l logfile      redirect diagnostics and trace output\n"
"  -L bytes        maximum size for PDUs from clients [default 65536]\n"
"  -p port         accept connections on this port\n"
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
	sts = __pmSecureClientHandshake(cp->pmcd_fd, flags, hostname);
   
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

    for (i = 0; i < nClients; i++)
	if (client[i].status.connected)
	    __pmCloseSocket(client[i].fd);
    __pmServerCloseRequestPorts();
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

static void
CheckNewClient(__pmFdSet * fdset, int rfd)
{
    ClientInfo	*cp;

    if (__pmFD_ISSET(rfd, fdset)) {
	if ((cp = AcceptNewClient(rfd)) == NULL)
	    /* failed to negotiate, already cleaned up */
	    return;

	/* establish a new connection to pmcd */
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

/* Loop, synchronously processing requests from clients. */
static void
ClientLoop(void)
{
    int		i, sts;
    int		maxFd;
    __pmFdSet	readableFds;

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
	    __pmServerAddNewClients(&readableFds, CheckNewClient);
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
 * Hostname extracted and cached for later use during protocol negotiations
 */
static void
GetProxyHostname(void)
{
    struct hostent	*hep = NULL;
    char		host[MAXHOSTNAMELEN];

    /* nathans TODO - fix this up - use newer APIs */
    if (gethostname(host, MAXHOSTNAMELEN) < 0) {
        __pmNotifyErr(LOG_ERR, "gethostname failure\n");
        DontStart();
    }
    host[MAXHOSTNAMELEN-1] = '\0';
    hep = gethostbyname(host);
    strncpy(hostname, hep ? hep->h_name : host, MAXHOSTNAMELEN);
    hostname[MAXHOSTNAMELEN-1] = '\0';
}

int
main(int argc, char *argv[])
{
    int		sts;
    int		nport = 0;
    char	*envstr;

    umask(022);
    __pmSetProgname(argv[0]);
    __pmGetUsername(&username);
    __pmSetInternalState(PM_STATE_PMCS);

    if ((envstr = getenv("PMPROXY_PORT")) != NULL)
	nport = __pmServerAddPorts(envstr);
    ParseOptions(argc, argv, &nport);
    if (nport == 0)
        __pmServerAddPorts(TO_STRING(PROXY_PORT));
    GetProxyHostname();

    if (run_daemon) {
	fflush(stderr);
	StartDaemon(argc, argv);
    }

    __pmSetSignalHandler(SIGHUP, SIG_IGN);
    __pmSetSignalHandler(SIGINT, SigIntProc);
    __pmSetSignalHandler(SIGTERM, SigIntProc);
    __pmSetSignalHandler(SIGBUS, SigBad);
    __pmSetSignalHandler(SIGSEGV, SigBad);

    /* Open request ports for client connections */
    if ((sts = __pmServerOpenRequestPorts(&sockFds, MAXPENDING)) < 0)
	DontStart();
    maxReqPortFd = maxSockFd = sts;

    __pmOpenLog(pmProgname, logfile, stderr, &sts);
    /* close old stdout, and force stdout into same stream as stderr */
    fflush(stdout);
    close(fileno(stdout));
    if (dup(fileno(stderr)) == -1) {
	fprintf(stderr, "Warning: dup() failed: %s\n", pmErrStr(-oserror()));
    }

    fprintf(stderr, "pmproxy: PID = %" FMT_PID, getpid());
    fprintf(stderr, ", PDU version = %u\n", PDU_VERSION);
    __pmServerDumpRequestPorts(stderr);
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

#ifdef PCP_DEBUG
/* Convert a file descriptor to a string describing what it is for. */
static char *
FdToString(int fd)
{
    static char fdStr[FDNAMELEN];
    static char *stdFds[4] = {"*UNKNOWN FD*", "stdin", "stdout", "stderr"};
    int		i;

    if (fd >= -1 && fd < 3)
	return stdFds[fd + 1];
    if (__pmServerRequestPortString(fd, fdStr, FDNAMELEN) != NULL)
	return fdStr;
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
#endif
