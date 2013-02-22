/*
 * Copyright (c) 2012-2013 Red Hat.
 * Copyright (c) 1995-2001,2004 Silicon Graphics, Inc.  All Rights Reserved.
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

#include "pmcd.h"
#include "impl.h"
#include <sys/stat.h>
#include <assert.h>

extern int  ParseInitAgents(char *);
extern void ParseRestartAgents(char *);
extern void PrintAgentInfo(FILE *);
extern void ResetBadHosts(void);
extern void StartDaemon(int, char **);

#define SHUTDOWNWAIT 12 /* < PMDAs wait previously used in rc_pcp */

int		AgentDied;		/* for updating mapdom[] */
static int	timeToDie;		/* For SIGINT handling */
static int	restart;		/* For SIGHUP restart */
static char	configFileName[MAXPATHLEN]; /* path to pmcd.conf */
static char	*logfile = "pmcd.log";	/* log file name */
static int	run_daemon = 1;		/* run as a daemon, see -f */
int		_creds_timeout = 3;	/* Timeout for agents credential PDU */
static char	*fatalfile = "/dev/tty";/* fatal messages at startup go here */
static char	*pmnsfile = PM_NS_DEFAULT;
static char	*username;
static int	dupok;			/* set to 1 for -N pmnsfile */

/*
 * Interfaces we're willing to listen for clients on, from -i
 */
static int		nintf;
static char		**intflist;

/*
 * Ports we're willing to listen for clients on, from -p or $PMCD_PORT
 */
static int		nport;
static int		*portlist;

/*
 * For maintaining info about a request port that clients may connect to pmcd on
 */
typedef struct {
    int			fds[2];		/* inet and ipv6 File descriptors */
    int			port;		/* Listening port */
    char*		ipSpec;		/* String used to specify IP addr (or NULL) */
} ReqPortInfo;
/* These are for indexing, and interating over, the request port's fds. */
#define INET_FD  0
#define IPV6_FD  1
#define FIRST_FD 0
#define LAST_FD  1

/*
 * A list of the ports that pmcd is listening for client connections on
 */
static unsigned		nReqPorts;	/* number of ports */
static unsigned		szReqPorts;	/* capacity of ports array */
static ReqPortInfo	*reqPorts;	/* ports array */
int			maxReqPortFd = -1;	/* highest request port file descriptor */

/*
 * Optional security services information
 */
static char		*certdb;	/* certificate database path (NSS) */
static char		*dbpassfile;	/* certificate database password file */

#ifdef HAVE_SA_SIGINFO
static pid_t	killer_pid;
static uid_t	killer_uid;
#endif
static int	killer_sig;

static void
DontStart(void)
{
    FILE	*tty;
    FILE	*log;
    __pmNotifyErr(LOG_ERR, "pmcd not started due to errors!\n");

    if ((tty = fopen(fatalfile, "w")) != NULL) {
	fflush(stderr);
	fprintf(tty, "NOTE: pmcd not started due to errors!  ");
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

static int
CreatePIDfile(void)
{
    char	pidpath[MAXPATHLEN];
    FILE	*pidfile;

    snprintf(pidpath, sizeof(pidpath), "%s%c" "pmcd.pid",
		pmGetConfig("PCP_RUN_DIR"), __pmPathSeparator());
    if ((pidfile = fopen(pidpath, "w")) == NULL) {
	fprintf(stderr, "Error: Cant open PID file %s\n", pidpath);
	return -1;
    }
    fprintf(pidfile, "%" FMT_PID, getpid());
    fflush(pidfile);
    fclose(pidfile);
    return 0;
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
	__pmNoMem("pmcd: can't grow request port array", need, PM_FATAL_ERR);
    }
}

/* Add a request port to the reqPorts array */

static int
AddRequestPort(char *ipSpec, int port)
{
    ReqPortInfo		*rp;

    if (ipSpec == NULL)
	ipSpec = "INADDR_ANY";

    if (nReqPorts == szReqPorts)
	GrowReqPorts();
    rp = &reqPorts[nReqPorts];
    rp->fds[INET_FD] = -1;
    rp->fds[IPV6_FD] = -1;
    rp->ipSpec = strdup(ipSpec);
    rp->port = port;
    nReqPorts++;

#ifdef PCP_DEBUG
    if (pmDebug & DBG_TRACE_APPL0)
	fprintf(stderr, "AddRequestPort: %s port %d\n", rp->ipSpec, rp->port);
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
    char	*endptr;
    int		val;
    int		port;
    char	*p;

    __pmSetProgname(argv[0]);
    __pmGetUsername(&username);

    strcpy(configFileName, pmGetConfig("PCP_PMCDCONF_PATH"));

#ifdef HAVE_GETOPT_NEEDS_POSIXLY_CORRECT
    /*
     * pmcd does not really need this for its own options because the
     * arguments like "arg -x" are not valid.  But the PMDA's launched
     * by pmcd from pmcd.conf may not be so lucky.
     */
    putenv("POSIXLY_CORRECT=");
#endif

    while ((c = getopt(argc, argv, "C:D:fi:l:L:N:n:p:P:q:t:T:U:x:?")) != EOF)
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
		nintf++;
		if ((intflist = (char **)realloc(intflist, nintf * sizeof(char *))) == NULL) {
		    __pmNoMem("pmcd: can't grow interface list", nintf * sizeof(char *), PM_FATAL_ERR);
		}
		intflist[nintf-1] = optarg;
		break;

	    case 'l':
		/* log file name */
		logfile = optarg;
		break;

	    case 'L': /* Maximum size for PDUs from clients */
		val = (int)strtol (optarg, NULL, 0);
		if ( val <= 0 ) {
		    fputs ("pmcd: -L requires a positive value\n", stderr);
		    errflag++;
		} else {
		    __pmSetPDUCeiling (val);
		}
		break;

	    case 'N':
		dupok = 1;
		/*FALLTHROUGH*/
	    case 'n':
	    	/* name space file name */
		pmnsfile = optarg;
		break;

	    case 'p':
		/*
		 * one (of possibly several) ports for client requests
		 * ... accept a comma separated list of ports here
		 */
		p = optarg;
		for ( ; ; ) {
		    port = (int)strtol(p, &endptr, 0);
		    if ((*endptr != '\0' && *endptr != ',') || port < 0) {
			fprintf(stderr,
				"pmcd: -p requires a positive numeric argument (%s)\n", optarg);
			errflag++;
			break;
		    }
		    else {
			nport++;
			if ((portlist = (int *)realloc(portlist, nport * sizeof(int))) == NULL) {
			    __pmNoMem("pmcd: can't grow port list", nport * sizeof(int), PM_FATAL_ERR);
			}
			portlist[nport-1] = port;
		    }
		    if (*endptr == '\0')
			break;
		    p = &endptr[1];
		}
		break;

	    case 'P':	/* password file for certificate database access */
		dbpassfile = optarg;
		break;

	    case 'q':
		val = (int)strtol(optarg, &endptr, 10);
		if (*endptr != '\0' || val <= 0.0) {
		    fprintf(stderr,
			    "pmcd: -q requires a positive numeric argument\n");
		    errflag++;
		}
		else
		    _creds_timeout = val;
		break;

	    case 't':
		val = (int)strtol(optarg, &endptr, 10);
		if (*endptr != '\0' || val < 0.0) {
		    fprintf(stderr,
			    "pmcd: -t requires a positive numeric argument\n");
		    errflag++;
		}
		else
		    _pmcd_timeout = val;
		break;

	    case 'T':
		val = (int)strtol(optarg, &endptr, 10);
		if (*endptr != '\0' || val < 0) {
		    fprintf(stderr,
			    "pmcd: -T requires a positive numeric argument\n");
		    errflag++;
		}
		else
		    _pmcd_trace_mask = val;
		break;

	    case 'U':
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
"  -n pmnsfile     use an alternative PMNS\n"
"  -N pmnsfile     use an alternative PMNS (duplicate PMIDs are allowed)\n"
"  -p port         accept connections on this port\n"
"  -P passfile     password file for certificate database access\n"
"  -q timeout      PMDA initial negotiation timeout (seconds) [default 3]\n"
"  -T traceflag    Event trace control\n"
"  -t timeout      PMDA response timeout (seconds) [default 5]\n"
"  -U username     in daemon mode, run as named user [default pcp]\n"
"  -x file         fatal messages at startup sent to file [default /dev/tty]\n",
			pmProgname);
	if (usage)
	    exit(0);
	else
	    DontStart();
    }
}

/* Create socket for incoming connections and bind to it an address for
 * clients to use.  Returns -1 on failure.
 * ipSpec is a string representing the IP address that the port is advertised for.
 * To allow connections to all this host's IP addresses from clients use ipSpec = "INADDR_ANY".
 * On input, 'family' is a pointer to the address family to use (AF_INET or AF_INET6) if the spec is
 * empty. If the spec is not empty then *family is ignored and is set to the actual address
 * family used.
 */
static int
OpenRequestSocket(int port, const char * ipSpec, int *family)
{
    int			fd = -1;
    int			one, sts;
    __pmSockAddr	*myAddr;

    if ((myAddr = __pmStringToSockAddr(ipSpec)) == NULL) {
	__pmNotifyErr(LOG_ERR, "OpenRequestSocket(%d, %s) invalid address\n",
		      port, ipSpec);
	goto fail;
    }

    /*
     * If the address is unspecified, then use the address family we
     * have been given, otherwise the family will be determined by
     * __pmStringToSockAddr.
     */
    if (ipSpec == NULL || strcmp(ipSpec, "INADDR_ANY") == 0)
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
		      port, ipSpec, *family);
	goto fail;
    }

    if (fd < 0) {
	__pmNotifyErr(LOG_ERR, "OpenRequestSocket(%d, %s) __pmCreateSocket: %s\n",
		port, ipSpec, netstrerror());
	goto fail;
    }
    if (fd > maxClientFd)
	maxClientFd = fd;
    __pmFD_SET(fd, &clientFds);

    /* Ignore dead client connections */
    one = 1;
#ifndef IS_MINGW
    if (__pmSetSockOpt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&one,
		(__pmSockLen)sizeof(one)) < 0) {
	__pmNotifyErr(LOG_ERR,
		"OpenRequestSocket(%d, %s) __pmSetSockOpt(SO_REUSEADDR): %s\n",
		port, ipSpec, netstrerror());
	goto fail;
    }
#else
    if (__pmSetSockOpt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *)&one,
		(__pmSockLen)sizeof(one)) < 0) {
	__pmNotifyErr(LOG_ERR,
		"OpenRequestSocket(%d,%s) __pmSetSockOpt(EXCLUSIVEADDRUSE): %s\n",
		port, ipSpec, netstrerror());
	goto fail;
    }
#endif

    /* and keep alive please - pv 916354 bad networks eat fds */
    if (__pmSetSockOpt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *)&one,
		(__pmSockLen)sizeof(one)) < 0) {
	__pmNotifyErr(LOG_ERR,
		"OpenRequestSocket(%d, %s) __pmSetSockOpt(SO_KEEPALIVE): %s\n",
		port, ipSpec, netstrerror());
	goto fail;
    }

    sts = __pmBind(fd, (void *)myAddr, __pmSockAddrSize());
    __pmSockAddrFree(myAddr);
    myAddr = NULL;
    if (sts < 0) {
	sts = neterror();
	__pmNotifyErr(LOG_ERR, "OpenRequestSocket(%d, %s) __pmBind: %s\n",
		port, ipSpec, netstrerror());
	if (sts == EADDRINUSE)
	    __pmNotifyErr(LOG_ERR, "pmcd may already be running\n");
	goto fail;
    }

    sts = __pmListen(fd, 5);	/* Max. of 5 pending connection requests */
    if (sts == -1) {
	__pmNotifyErr(LOG_ERR, "OpenRequestSocket(%d, %s) __pmListen: %s\n",
		port, ipSpec, netstrerror());
	goto fail;
    }
    return fd;

fail:
    if (fd != -1)
        __pmCloseSocket(fd);
    if (myAddr)
        __pmSockAddrFree(myAddr);
    return -1;
}

extern int DoFetch(ClientInfo *, __pmPDU *);
extern int DoProfile(ClientInfo *, __pmPDU *);
extern int DoDesc(ClientInfo *, __pmPDU *);
extern int DoInstance(ClientInfo *, __pmPDU *);
extern int DoText(ClientInfo *, __pmPDU *);
extern int DoStore(ClientInfo *, __pmPDU *);
extern int DoCreds(ClientInfo *, __pmPDU *);
extern int DoPMNSIDs(ClientInfo *, __pmPDU *);
extern int DoPMNSNames(ClientInfo *, __pmPDU *);
extern int DoPMNSChild(ClientInfo *, __pmPDU *);
extern int DoPMNSTraverse(ClientInfo *, __pmPDU *);

/* Determine which clients (if any) have sent data to the server and handle it
 * as required.
 */

void
HandleClientInput(__pmFdSet *fdsPtr)
{
    int		sts;
    int		i;
    __pmPDU	*pb;
    __pmPDUHdr	*php;
    ClientInfo	*cp;

    for (i = 0; i < nClients; i++) {
	int		pinpdu;
	if (!client[i].status.connected || !__pmFD_ISSET(client[i].fd, fdsPtr))
	    continue;

	cp = &client[i];
	this_client_id = i;

	pinpdu = sts = __pmGetPDU(cp->fd, LIMIT_SIZE, _pmcd_timeout, &pb);
	if (sts > 0 && _pmcd_trace_mask)
	    pmcd_trace(TR_RECV_PDU, cp->fd, sts, (int)((__psint_t)pb & 0xffffffff));
	if (sts <= 0) {
	    CleanupClient(cp, sts);
	    continue;
	}

	php = (__pmPDUHdr *)pb;
	if (__pmVersionIPC(cp->fd) == UNKNOWN_VERSION && php->type != PDU_CREDS) {
	    /* old V1 client protocol, no longer supported */
	    sts = PM_ERR_IPC;
	    CleanupClient(cp, sts);
	    __pmUnpinPDUBuf(pb);
	    continue;
	}

#ifdef PCP_DEBUG
	if (pmDebug & DBG_TRACE_APPL0)
	    ShowClients(stderr);
#endif

	switch (php->type) {
	    case PDU_PROFILE:
		sts = DoProfile(cp, pb);
		break;

	    case PDU_FETCH:
		sts = (cp->denyOps & PMCD_OP_FETCH) ?
		      PM_ERR_PERMISSION : DoFetch(cp, pb);
		break;

	    case PDU_INSTANCE_REQ:
		sts = (cp->denyOps & PMCD_OP_FETCH) ?
		      PM_ERR_PERMISSION : DoInstance(cp, pb);
		break;

	    case PDU_DESC_REQ:
		sts = (cp->denyOps & PMCD_OP_FETCH) ?
		      PM_ERR_PERMISSION : DoDesc(cp, pb);
		break;

	    case PDU_TEXT_REQ:
		sts = (cp->denyOps & PMCD_OP_FETCH) ?
		      PM_ERR_PERMISSION : DoText(cp, pb);
		break;

	    case PDU_RESULT:
		sts = (cp->denyOps & PMCD_OP_STORE) ?
		      PM_ERR_PERMISSION : DoStore(cp, pb);
		break;

	    case PDU_PMNS_IDS:
		sts = (cp->denyOps & PMCD_OP_FETCH) ?
		      PM_ERR_PERMISSION : DoPMNSIDs(cp, pb);
		break;

	    case PDU_PMNS_NAMES:
		sts = (cp->denyOps & PMCD_OP_FETCH) ?
		      PM_ERR_PERMISSION : DoPMNSNames(cp, pb);
		break;

	    case PDU_PMNS_CHILD:
		sts = (cp->denyOps & PMCD_OP_FETCH) ?
		      PM_ERR_PERMISSION : DoPMNSChild(cp, pb);
		break;

	    case PDU_PMNS_TRAVERSE:
		sts = (cp->denyOps & PMCD_OP_FETCH) ?
		      PM_ERR_PERMISSION : DoPMNSTraverse(cp, pb);
		break;

	    case PDU_CREDS:
		sts = DoCreds(cp, pb);
		break;

	    default:
		sts = PM_ERR_IPC;
	}
	if (sts < 0) {

#ifdef PCP_DEBUG
	    if (pmDebug & DBG_TRACE_APPL0)
		fprintf(stderr, "PDU:  %s client[%d]: %s\n",
		    __pmPDUTypeStr(php->type), i, pmErrStr(sts));
#endif
	    /* Make sure client still alive before sending. */
	    if (cp->status.connected) {
		if (_pmcd_trace_mask)
		    pmcd_trace(TR_XMIT_PDU, cp->fd, PDU_ERROR, sts);
		sts = __pmSendError(cp->fd, FROM_ANON, sts);
		if (sts < 0)
		    __pmNotifyErr(LOG_ERR, "HandleClientInput: "
			"error sending Error PDU to client[%d] %s\n", i, pmErrStr(sts));
	    }
	}
	if (pinpdu > 0)
	    __pmUnpinPDUBuf(pb);
    }
}

/* Called to shutdown pmcd in an orderly manner */

void
Shutdown(void)
{
    int	i;
    int	fd;

    for (i = 0; i < nAgents; i++) {
	AgentInfo *ap = &agent[i];
	if (!ap->status.connected)
	    continue;
	if (ap->inFd != -1) {
	    if (__pmSocketIPC(ap->inFd))
		__pmCloseSocket(ap->inFd);
	    else
		close(ap->inFd);
	}
	if (ap->outFd != -1) {
	    if (__pmSocketIPC(ap->outFd))
		__pmCloseSocket(ap->outFd);
	    else
		close(ap->outFd);
	}
	if (ap->ipcType == AGENT_SOCKET &&
	    ap->ipc.socket.addrDomain == AF_UNIX) {
	    /* remove the Unix domain socket */
	    unlink(ap->ipc.socket.name);
	}
    }
    if (HarvestAgents(SHUTDOWNWAIT) < 0) {
	/* terminate with prejudice any still remaining */
	for (i = 0; i < nAgents; i++) {
	    AgentInfo *ap = &agent[i];
	    if (ap->status.connected) {
		pid_t pid = ap->ipcType == AGENT_SOCKET ?
			    ap->ipc.socket.agentPid : ap->ipc.pipe.agentPid;
		__pmProcessTerminate(pid, 1);
	    }
	}
    }
    for (i = 0; i < nClients; i++)
        if (client[i].status.connected)
	    __pmCloseSocket(client[i].fd);
    for (i = 0; i < nReqPorts; i++) {
	if ((fd = reqPorts[i].fds[INET_FD]) != -1)
	    __pmCloseSocket(reqPorts[i].fds[INET_FD]);
	if ((fd = reqPorts[i].fds[IPV6_FD]) != -1)
	    __pmCloseSocket(reqPorts[i].fds[IPV6_FD]);
    }
    __pmNotifyErr(LOG_INFO, "pmcd Shutdown\n");
    fflush(stderr);
}

static void
SignalShutdown(void)
{
#ifdef HAVE_SA_SIGINFO
#if DESPERATE
    char	buf[256];
#endif
    if (killer_pid != 0) {
	__pmNotifyErr(LOG_INFO, "pmcd caught %s from pid=%" FMT_PID " uid=%d\n",
	    killer_sig == SIGINT ? "SIGINT" : "SIGTERM", killer_pid, killer_uid);
#if DESPERATE
	__pmNotifyErr(LOG_INFO, "Try to find process in ps output ...\n");
	sprintf(buf, "sh -c \". \\$PCP_DIR/etc/pcp.env; ( \\$PCP_PS_PROG \\$PCP_PS_ALL_FLAGS | \\$PCP_AWK_PROG 'NR==1 {print} \\$2==%" FMT_PID " {print}' )\"", killer_pid);
	system(buf);
#endif
    }
    else {
	__pmNotifyErr(LOG_INFO, "pmcd caught %s from unknown process\n",
			killer_sig == SIGINT ? "SIGINT" : "SIGTERM");
    }
#else
    __pmNotifyErr(LOG_INFO, "pmcd caught %s\n",
		    killer_sig == SIGINT ? "SIGINT" : "SIGTERM");
#endif
    Shutdown();
    exit(0);
}

void
SignalRestart(void)
{
    time_t	now;

    time(&now);
    __pmNotifyErr(LOG_INFO, "\n\npmcd RESTARTED at %s", ctime(&now));
    fprintf(stderr, "\nCurrent PMCD clients ...\n");
    ShowClients(stderr);
    ResetBadHosts();
    ParseRestartAgents(configFileName);
}

void
SignalReloadPMNS(void)
{
    int sts;

    /* Reload PMNS if necessary. 
     * Note: this will only stat() the base name i.e. ASCII pmns,
     * typically $PCP_VAR_DIR/pmns/root and not $PCP_VAR_DIR/pmns/root.bin .
     * This is considered a very low risk problem, as the binary
     * PMNS is always compiled from the ASCII version;
     * when one changes so should the other.
     * This caveat was allowed to make the code a lot simpler. 
     */
    if (__pmHasPMNSFileChanged(pmnsfile)) {
	__pmNotifyErr(LOG_INFO, "Reloading PMNS \"%s\"",
	   (pmnsfile==PM_NS_DEFAULT)?"DEFAULT":pmnsfile);
	pmUnloadNameSpace();
	if (dupok)
	    sts = pmLoadASCIINameSpace(pmnsfile, 1);
	else
	    sts = pmLoadNameSpace(pmnsfile);
	if (sts < 0) {
	    __pmNotifyErr(LOG_ERR, "PMNS \"%s\" load failed: %s",
		(pmnsfile == PM_NS_DEFAULT) ? "DEFAULT" : pmnsfile,
		pmErrStr(sts));
	}
    }
    else {
	__pmNotifyErr(LOG_INFO, "PMNS file \"%s\" is unchanged",
		(pmnsfile == PM_NS_DEFAULT) ? "DEFAULT" : pmnsfile);
    }
}

/* Process I/O on file descriptors from agents that were marked as not ready
 * to handle PDUs.
 */
static int
HandleReadyAgents(__pmFdSet *readyFds)
{
    int		i, s, sts;
    int		fd;
    int		reason;
    int		ready = 0;
    AgentInfo	*ap;
    __pmPDU	*pb;

    for (i = 0; i < nAgents; i++) {
	ap = &agent[i];
	if (ap->status.notReady) {
	    fd = ap->outFd;
	    if (__pmFD_ISSET(fd, readyFds)) {
		int		pinpdu;
		/* Expect an error PDU containing PM_ERR_PMDAREADY */
		reason = AT_COMM;	/* most errors are protocol failures */
		pinpdu = sts = __pmGetPDU(ap->outFd, ANY_SIZE, _pmcd_timeout, &pb);
		if (sts > 0 && _pmcd_trace_mask)
		    pmcd_trace(TR_RECV_PDU, ap->outFd, sts, (int)((__psint_t)pb & 0xffffffff));
		if (sts == PDU_ERROR) {
		    s = __pmDecodeError(pb, &sts);
		    if (s < 0) {
			sts = s;
			pmcd_trace(TR_RECV_ERR, ap->outFd, PDU_ERROR, sts);
		    }
		    else {
			/* sts is the status code from the error PDU */
#ifdef PCP_DEBUG
			if (pmDebug && DBG_TRACE_APPL0)
			    __pmNotifyErr(LOG_INFO,
				 "%s agent (not ready) sent %s status(%d)\n",
				 ap->pmDomainLabel,
				 sts == PM_ERR_PMDAREADY ?
					     "ready" : "unknown", sts);
#endif
			if (sts == PM_ERR_PMDAREADY) {
			    ap->status.notReady = 0;
			    sts = 1;
			    ready++;
			}
			else {
			    pmcd_trace(TR_RECV_ERR, ap->outFd, PDU_ERROR, sts);
			    sts = PM_ERR_IPC;
			}
		    }
		}
		else {
		    if (sts < 0)
			pmcd_trace(TR_RECV_ERR, ap->outFd, PDU_RESULT, sts);
		    else
			pmcd_trace(TR_WRONG_PDU, ap->outFd, PDU_ERROR, sts);
		    sts = PM_ERR_IPC; /* Wrong PDU type */
		}
		if (pinpdu > 0)
		    __pmUnpinPDUBuf(pb);

		if (ap->ipcType != AGENT_DSO && sts <= 0)
		    CleanupAgent(ap, reason, fd);

	    }
	}
    }

    return ready;
}

/* Loop, synchronously processing requests from clients. */

static void
ClientLoop(void)
{
    int		i, sts;
    int         fdix;
    int		challenge;
    int		maxFd;
    int		checkAgents;
    int		reload_ns = 0;
    __pmFdSet	readableFds;
    ClientInfo	*cp;
    __pmPDUInfo	xchallenge;

    for (;;) {

	/* Figure out which file descriptors to wait for input on.  Keep
	 * track of the highest numbered descriptor for the select call.
	 */
	readableFds = clientFds;
	maxFd = maxClientFd + 1;

	/* If an agent was not ready, it may send an ERROR PDU to indicate it
	 * is now ready.  Add such agents to the list of file descriptors.
	 */
	checkAgents = 0;
	for (i = 0; i < nAgents; i++) {
	    AgentInfo	*ap = &agent[i];
	    int		fd;

	    if (ap->status.notReady) {
		fd = ap->outFd;
		__pmFD_SET(fd, &readableFds);
		if (fd > maxFd)
		    maxFd = fd + 1;
		checkAgents = 1;
#ifdef PCP_DEBUG
		if (pmDebug & DBG_TRACE_APPL0)
		    __pmNotifyErr(LOG_INFO,
				 "not ready: check %s agent on fd %d (max = %d)\n",
				 ap->pmDomainLabel, fd, maxFd);
#endif
	    }
	}

	sts = __pmSelectRead(maxFd, &readableFds, NULL);

	if (sts > 0) {
#ifdef PCP_DEBUG
	    if (pmDebug & DBG_TRACE_APPL0)
		for (i = 0; i <= maxClientFd; i++)
		    if (__pmFD_ISSET(i, &readableFds))
			fprintf(stderr, "DATA: from %s (fd %d)\n", FdToString(i), i);
#endif
	    /* Accept any new client connections */
	    for (i = 0; i < nReqPorts; i++) {
	        /* Check both the inet and ipv6 fds. */
	        for (fdix = FIRST_FD; fdix <= LAST_FD; ++fdix) {
		    int rfd = reqPorts[i].fds[fdix];
		    if (rfd == -1)
		        continue;
		    if (__pmFD_ISSET(rfd, &readableFds)) {
		        int	sts, s;
			int	accepted = 1;

			cp = AcceptNewClient(rfd);

			/* Accept failed and no client added */
			if (cp == NULL)
			    continue;

			sts = __pmAccAddClient(cp->addr, &cp->denyOps);
			if (sts >= 0) {
			    memset(&cp->pduInfo, 0, sizeof(cp->pduInfo));
			    cp->pduInfo.version = PDU_VERSION;
			    cp->pduInfo.licensed = 1;
			    if (__pmSecureServerHasFeature(PM_SERVER_FEATURE_SECURE))
				cp->pduInfo.features |= PDU_FLAG_SECURE;
			    if (__pmSecureServerHasFeature(PM_SERVER_FEATURE_COMPRESS))
				cp->pduInfo.features |= PDU_FLAG_COMPRESS;
			    challenge = *(int*)(&cp->pduInfo);
			    sts = 0;
			}
			else {
			    /* __pmAccAddClient failed, this is grim! */
			    challenge = 0;
			    accepted = 0;
			}

			if (_pmcd_trace_mask)
			    pmcd_trace(TR_XMIT_PDU, cp->fd, PDU_ERROR, sts);
			xchallenge = *(__pmPDUInfo *)&challenge;
			xchallenge = __htonpmPDUInfo(xchallenge);

			/* reset (no meaning, use fd table to version) */
			cp->pduInfo.version = UNKNOWN_VERSION;

			s = __pmSendXtendError(cp->fd, FROM_ANON, sts, *(unsigned int *)&xchallenge);
			if (s < 0) {
			    __pmNotifyErr(LOG_ERR,
				"ClientLoop: error sending Conn ACK PDU to new client %s\n",
				pmErrStr(s));
			    if (sts >= 0)
			        /*
				 * prefer earlier failure status if any, else
				 * use the one from __pmSendXtendError()
				 */
			        sts = s;
			    accepted = 0;
			}
			if (!accepted)
			    CleanupClient(cp, sts);
		    }
		}
	    }

	    if (checkAgents)
		reload_ns = HandleReadyAgents(&readableFds);
	    HandleClientInput(&readableFds);
	}
	else if (sts == -1 && neterror() != EINTR) {
	    __pmNotifyErr(LOG_ERR, "ClientLoop select: %s\n", netstrerror());
	    break;
	}
	if (restart) {
	    restart = 0;
	    reload_ns = 1;
	    SignalRestart();
	}
	if (reload_ns) {
	    reload_ns = 0;
	    SignalReloadPMNS();
	}
	if (timeToDie) {
	    SignalShutdown();
	    break;
	}
	if (AgentDied) {
	    AgentDied = 0;
	    for (i = 0; i < nAgents; i++) {
		if (!agent[i].status.connected)
		    mapdom[agent[i].pmDomainId] = nAgents;
	    }
	}
    }
}

#ifdef HAVE_SA_SIGINFO
void
SigIntProc(int sig, siginfo_t *sip, void *x)
{
    killer_sig = sig;
    if (sip != NULL) {
	killer_pid = sip->si_pid;
	killer_uid = sip->si_uid;
    }
    timeToDie = 1;
}
#else
void SigIntProc(int sig)
{
    killer_sig = sig;
#ifndef IS_MINGW
    signal(SIGINT, SigIntProc);
    signal(SIGTERM, SigIntProc);
    timeToDie = 1;
#else
    SignalShutdown();
#endif
}
#endif

void SigHupProc(int s)
{
#ifndef IS_MINGW
    signal(SIGHUP, SigHupProc);
    restart = 1;
#else
    SignalRestart();
    SignalReloadPMNS();
#endif
}

#if HAVE_TRACE_BACK_STACK
/*
 * max callback procedure depth (MAX_PCS) and max function name length
 * (MAX_SIZE)
 */
#define MAX_PCS 30
#define MAX_SIZE 48

#include <libexc.h>

static void
do_traceback(FILE *f)
{
    __uint64_t	call_addr[MAX_PCS];
    char	*call_fn[MAX_PCS];
    char	names[MAX_PCS][MAX_SIZE];
    int		res;
    int		i;

    for (i = 0; i < MAX_PCS; i++)
	call_fn[i] = names[i];
    res = trace_back_stack(MAX_PCS, call_addr, call_fn, MAX_PCS, MAX_SIZE);
    for (i = 1; i < res; i++)
#if defined(HAVE_64BIT_PTR)
	fprintf(f, "  0x%016llx [%s]\n", call_addr[i], call_fn[i]);
#else
	fprintf(f, "  0x%08lx [%s]\n", (__uint32_t)call_addr[i], call_fn[i]);
#endif
    return;
}
#endif

void SigBad(int sig)
{
    __pmNotifyErr(LOG_ERR, "Unexpected signal %d ...\n", sig);
#if HAVE_TRACE_BACK_STACK
    fprintf(stderr, "\nProcedure call traceback ...\n");
    do_traceback(stderr);
#endif
    fprintf(stderr, "\nDumping to core ...\n");
    fflush(stderr);
    abort();
}

int
main(int argc, char *argv[])
{
    int		i, j;
    int		n;
    int		sts;
    int		status;
    char	*envstr;
    unsigned	nReqPortsOK = 0;
#ifdef HAVE_SA_SIGINFO
    static struct sigaction act;
#endif

    umask(022);
    __pmProcessDataSize(NULL);
    __pmSetInternalState(PM_STATE_PMCS);

    /*
     * get optional stuff from environment ... PMCD_PORT ...
     * same code is in connect.c of libpcp
     */
    if ((envstr = getenv("PMCD_PORT")) != NULL) {
	char	*p = envstr;
	char	*endptr;
	int	port;

	for ( ; ; ) {
	    port = (int)strtol(p, &endptr, 0);
	    if ((*endptr != '\0' && *endptr != ',') || port < 0) {
		__pmNotifyErr(LOG_WARNING,
			 "pmcd: ignored bad PMCD_PORT = '%s'", p);
	    }
	    else {
		nport++;
		if ((portlist = (int *)realloc(portlist, nport * sizeof(int))) == NULL) {
		    __pmNoMem("pmcd: can't grow port list", nport * sizeof(int), PM_FATAL_ERR);
		}
		portlist[nport-1] = port;
	    }
	    if (*endptr == '\0')
		break;
	    p = &endptr[1];
	}
    }

    ParseOptions(argc, argv);

    if (run_daemon) {
	fflush(stderr);
	StartDaemon(argc, argv);
    }

#ifdef HAVE_SA_SIGINFO
    act.sa_sigaction = SigIntProc;
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);
#else
    __pmSetSignalHandler(SIGINT, SigIntProc);
    __pmSetSignalHandler(SIGTERM, SigIntProc);
#endif
    __pmSetSignalHandler(SIGHUP, SigHupProc);
    __pmSetSignalHandler(SIGBUS, SigBad);
    __pmSetSignalHandler(SIGSEGV, SigBad);

    /* seed random numbers for authorisation */
    srand48((long)time(0));

    if (nport == 0) {
	/*
	 * no ports from $PMCD_PORT, nor from -p, set up defaults
	 * for compatibility this used to be SERVER_PORT,4321 but
	 * has now transitioned to just SERVER_PORT
	 */
	nport = 1;
	if ((portlist = (int *)realloc(portlist, nport * sizeof(int))) == NULL) {
	    __pmNoMem("pmcd: can't grow port list", nport * sizeof(int), PM_FATAL_ERR);
	}
	portlist[0] = SERVER_PORT;
    }

    /*
     * check for duplicate ports, warn and remove duplicates
     */
    for (i = 0; i < nport; i++) {
	for (n = i+1; n < nport; n++) {
	    if (portlist[i] == portlist[n])
		break;
	}
	if (n < nport) {
	    __pmNotifyErr(LOG_WARNING,
		     "pmcd: duplicate client request port (%d) will be ignored\n",
		     portlist[n]);
	    portlist[n] = -1;
	}
    }

    if (nintf == 0) {
	/*
	 * no -i IP_ADDR options specified, allow connections on any
	 * IP addr
	 */
	for (n = 0; n < nport; n++) {
	    if (portlist[n] != -1)
		AddRequestPort(NULL, portlist[n]);
	}
    }
    else {
	for (i = 0; i < nintf; i++) {
	    for (n = 0; n < nport; n++) {
		if (portlist[n] == -1)
		    continue;
		if (!AddRequestPort(intflist[i], portlist[n])) {
		    fprintf(stderr, "pmcd: bad IP spec: -i %s\n", intflist[i]);
		    exit(1);
		}
	    }
	}
    }

    /*
     * Open request ports for client connections.
     * Open an inet and an IPv6 socket for each client, if appropriate.
     */
    for (i = 0; i < nReqPorts; i++) {
	int family;

	/*
	 * If the spec is NULL or "INADDR_ANY", then we open one socket
	 * for each address family (inet, IPv6).  Otherwise, the address
	 * family will be determined by OpenRequestSocket.
	 */
	if (reqPorts[i].ipSpec == NULL || strcmp(reqPorts[i].ipSpec, "INADDR_ANY") == 0) {
	    family = AF_INET;
	    reqPorts[i].fds[INET_FD] =
	      OpenRequestSocket(reqPorts[i].port, reqPorts[i].ipSpec, &family);
	    family = AF_INET6;
	    reqPorts[i].fds[IPV6_FD] =
	      OpenRequestSocket(reqPorts[i].port, reqPorts[i].ipSpec, &family);
	}
	else {
	    int fd = OpenRequestSocket(reqPorts[i].port, reqPorts[i].ipSpec, &family);
	    if (family == AF_INET)
	        reqPorts[i].fds[INET_FD] = fd;
	    else if (family == AF_INET6)
	        reqPorts[i].fds[IPV6_FD] = fd;
	    else
	        __pmNotifyErr(LOG_WARNING,
			      "pmcd: invalid request socket spec: %s\n", reqPorts[i].ipSpec);
	}
	if (reqPorts[i].fds[INET_FD] != -1) {
	    if (reqPorts[i].fds[INET_FD] > maxReqPortFd)
	        maxReqPortFd = reqPorts[i].fds[INET_FD];
	    nReqPortsOK++;
	}
	if (reqPorts[i].fds[IPV6_FD] != -1) {
	    if (reqPorts[i].fds[IPV6_FD] > maxReqPortFd)
	        maxReqPortFd = reqPorts[i].fds[IPV6_FD];
	    nReqPortsOK++;
	}
    }
    if (nReqPortsOK == 0) {
	__pmNotifyErr(LOG_ERR, "pmcd: can't open any request ports, exiting\n");
	DontStart();
    }	

    __pmOpenLog("pmcd", logfile, stderr, &status);
    /* close old stdout, and force stdout into same stream as stderr */
    fflush(stdout);
    close(fileno(stdout));
    sts = dup(fileno(stderr));
    /* if this fails beware of the sky falling in */
    assert(sts >= 0);

    if (dupok)
	sts = pmLoadASCIINameSpace(pmnsfile, 1);
    else
	sts = pmLoadNameSpace(pmnsfile);
    if (sts < 0) {
	fprintf(stderr, "Error: pmLoadNameSpace: %s\n", pmErrStr(sts));
	DontStart();
    }

    if (ParseInitAgents(configFileName) < 0) {
	/* error already reported in ParseInitAgents() */
	DontStart();
    }

    if (nAgents <= 0) {
	fprintf(stderr, "Error: No PMDAs found in the configuration file \"%s\"\n",
		configFileName);
	DontStart();
    }

    if (run_daemon) {
	if (CreatePIDfile() < 0)
	    DontStart();
	if (__pmSetProcessIdentity(username) < 0)
	    DontStart();
    }

    if (__pmSecureServerSetup(certdb, dbpassfile) < 0)
	DontStart();

    PrintAgentInfo(stderr);
    __pmAccDumpHosts(stderr);
    fprintf(stderr, "\npmcd: PID = %" FMT_PID, getpid());
    fprintf(stderr, ", PDU version = %u", PDU_VERSION);
    fputc('\n', stderr);
    fputs("pmcd request port(s):\n"
	  "  sts fd   port  IP addr\n"
	  "  === ==== ===== ==========\n", stderr);
    for (i = 0; i < nReqPorts; i++) {
	ReqPortInfo *rp = &reqPorts[i];
	for (j = FIRST_FD; j <= LAST_FD; ++j) {
	    fprintf(stderr, "  %s %4d %5d %s\n",
		    (rp->fds[j] != -1) ? "ok " : "err",
		    rp->fds[j], rp->port - 1 + j/* IPv6 TESTING */,
		    rp->ipSpec ? rp->ipSpec : "(any address)");
	}
    }
    fflush(stderr);

    /* all the work is done here */
    ClientLoop();

    Shutdown();
    exit(0);
}

/* The bad host list is a list of IP addresses for hosts that have had clients
 * cleaned up because of an access violation (permission or connection limit).
 * This is used to ensure that the message printed in PMCD's log file when a
 * client is terminated like this only appears once per host.  That stops the
 * log from growing too large if repeated access violations occur.
 * The list is cleared when PMCD is reconfigured.
 */

static int		 nBadHosts;
static int		 szBadHosts;
static __pmSockAddr	**badHost;

static int
AddBadHost(struct __pmSockAddr *hostId)
{
    int		i, need;

    for (i = 0; i < nBadHosts; i++)
        if (__pmSockAddrCompare(hostId, badHost[i]) == 0)
	    /* already there */
	    return 0;

    /* allocate more entries if required */
    if (nBadHosts == szBadHosts) {
	szBadHosts += 8;
	need = szBadHosts * (int)sizeof(badHost[0]);
	if ((badHost = (__pmSockAddr **)realloc(badHost, need)) == NULL) {
	    __pmNoMem("pmcd.AddBadHost", need, PM_FATAL_ERR);
	}
    }
    badHost[nBadHosts++] = __pmSockAddrDup(hostId);
    return 1;
}

void
ResetBadHosts(void)
{
    if (szBadHosts) {
        while (nBadHosts > 0) {
	    --nBadHosts;
	    free (badHost[nBadHosts]);
	}
	free(badHost);
    }
    nBadHosts = 0;
    szBadHosts = 0;
}

void
CleanupClient(ClientInfo *cp, int sts)
{
    char	*caddr;
    int		i, msg;
    int		force;

#ifdef PCP_DEBUG
    force = pmDebug & DBG_TRACE_APPL0;
#else
    force = 0;
#endif

    if (sts != 0 || force) {
	/* for access violations, only print the message if this host hasn't
	 * been dinged for an access violation since startup or reconfiguration
	 */
	if (sts == PM_ERR_PERMISSION || sts == PM_ERR_CONNLIMIT) {
	    if ( (msg = AddBadHost(cp->addr)) ) {
		caddr = __pmSockAddrToString(cp->addr);
		fprintf(stderr, "access violation from host %s:\n", caddr);
		free(caddr);
	    }
	}
	else
	    msg = 0;

	if (msg || force) {
	    for (i = 0; i < nClients; i++) {
		if (cp == &client[i])
		    break;
	    }
	    fprintf(stderr, "endclient client[%d]: (fd %d) %s (%d)\n",
		    i, cp->fd, pmErrStr(sts), sts);
	}
    }

    /* If the client is being cleaned up because its connection was refused
     * don't do this because it hasn't actually contributed to the connection
     * count
     */
    if (sts != PM_ERR_PERMISSION && sts != PM_ERR_CONNLIMIT)
        __pmAccDelClient(cp->addr);

    pmcd_trace(TR_DEL_CLIENT, cp->fd, sts, 0);
    DeleteClient(cp);

    if (maxClientFd < maxReqPortFd)
	maxClientFd = maxReqPortFd;

    for (i = 0; i < nAgents; i++)
	if (agent[i].profClient == cp)
	    agent[i].profClient = NULL;
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
	if (fd == reqPorts[i].fds[INET_FD]) {
	    sprintf(fdStr, "pmcd inet request socket %s", reqPorts[i].ipSpec);
	    return fdStr;
	}
	if (fd == reqPorts[i].fds[IPV6_FD]) {
	    sprintf(fdStr, "pmcd ipv6 request socket %s", reqPorts[i].ipSpec);
	    return fdStr;
	}
    }
    for (i = 0; i < nClients; i++)
        if (client[i].status.connected) {
	    if (fd == client[i].fd) {
	        sprintf(fdStr, "client[%d] input socket", i);
		return fdStr;
	    }
	}
    for (i = 0; i < nAgents; i++)
	if (agent[i].status.connected) {
	    if (fd == agent[i].inFd) {
		sprintf(fdStr, "agent[%d] input", i);
		return fdStr;
	    }
	    else if (fd  == agent[i].outFd) {
		sprintf(fdStr, "agent[%d] output", i);
		return fdStr;
	    }
	}
    return stdFds[0];
}
#endif
