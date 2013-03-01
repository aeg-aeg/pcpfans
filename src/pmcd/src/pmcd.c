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

#define SHUTDOWNWAIT	12	/* < PMDAs wait previously used in rc_pcp */
#define MAXPENDING	5	/* maximum number of pending connections */
#define FDNAMELEN	40	/* maximum length of a fd description */
#define STRINGIFY(s)	#s
#define TO_STRING(s)	STRINGIFY(s)

#ifdef PCP_DEBUG
static char	*FdToString(int);
#endif

int		AgentDied;		/* for updating mapdom[] */
static int	timeToDie;		/* For SIGINT handling */
static int	restart;		/* For SIGHUP restart */
static int	maxReqPortFd;		/* Largest request port fd */
static char	configFileName[MAXPATHLEN]; /* path to pmcd.conf */
static char	*logfile = "pmcd.log";	/* log file name */
static int	run_daemon = 1;		/* run as a daemon, see -f */
int		_creds_timeout = 3;	/* Timeout for agents credential PDU */
static char	*fatalfile = "/dev/tty";/* fatal messages at startup go here */
static char	*pmnsfile = PM_NS_DEFAULT;
static char	*username;
static char	*certdb;		/* certificate database path (NSS) */
static char	*dbpassfile;		/* certificate database password file */
static int	dupok;			/* set to 1 for -N pmnsfile */

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

static void
ParseOptions(int argc, char *argv[])
{
    int		c;
    int		sts;
    int		errflag = 0;
    int		usage = 0;
    char	*endptr;
    int		val;

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
		    fputs("pmcd: -L requires a positive value\n", stderr);
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
		if (__pmServerAddPorts(optarg) < 0) {
		    fprintf(stderr,
			"pmcd: -p requires a positive numeric argument (%s)\n",
			optarg);
		    errflag++;
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

/*
 * Determine which clients (if any) have sent data to the server and handle it
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
    __pmServerCloseRequestPorts();
    __pmSecureServerShutdown();
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

static void
CheckNewClient(__pmFdSet * fdset, int rfd)
{
    int		s, sts, challenge, accepted = 1;
    ClientInfo	*cp;
    __pmPDUInfo	xchallenge;

    if (__pmFD_ISSET(rfd, fdset)) {
	if ((cp = AcceptNewClient(rfd)) == NULL)
	    return;	/* Accept failed and no client added */

	sts = __pmAccAddClient(cp->addr, &cp->denyOps);
	if (sts >= 0) {
	    memset(&cp->pduInfo, 0, sizeof(cp->pduInfo));
	    cp->pduInfo.version = PDU_VERSION;
	    cp->pduInfo.licensed = 1;
	    if (__pmServerHasFeature(PM_SERVER_FEATURE_SECURE))
		cp->pduInfo.features |= PDU_FLAG_SECURE;
	    if (__pmServerHasFeature(PM_SERVER_FEATURE_COMPRESS))
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

/* Loop, synchronously processing requests from clients. */

static void
ClientLoop(void)
{
    int		i, fd, sts;
    int		maxFd;
    int		checkAgents;
    int		reload_ns = 0;
    __pmFdSet	readableFds;

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
	    __pmServerAddNewClients(&readableFds, CheckNewClient);
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
    int		sts;
    int		nport = 0;
    char	*envstr;
#ifdef HAVE_SA_SIGINFO
    static struct sigaction act;
#endif

    umask(022);
    __pmSetProgname(argv[0]);
    __pmProcessDataSize(NULL);
    __pmGetUsername(&username);
    __pmSetInternalState(PM_STATE_PMCS);

    if ((envstr = getenv("PMCD_PORT")) != NULL)
	nport = __pmServerAddPorts(envstr);
    ParseOptions(argc, argv);
    if (nport == 0)
	__pmServerAddPorts(TO_STRING(SERVER_PORT));

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

    if ((sts = __pmServerOpenRequestPorts(&clientFds, MAXPENDING)) < 0)
	DontStart();
    maxReqPortFd = maxClientFd = sts;

    __pmOpenLog(pmProgname, logfile, stderr, &sts);
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
    fprintf(stderr, ", PDU version = %u\n", PDU_VERSION);
    __pmServerDumpRequestPorts(stderr);
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
