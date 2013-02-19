/*
 * Copyright (c) 2012 Red Hat.
 * Copyright (c) 1995-2001 Silicon Graphics, Inc.  All Rights Reserved.
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
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef _PMCD_H
#define _PMCD_H

#include "pmapi.h"
#include "impl.h"
#include "pmda.h"

#ifdef IS_MINGW
#ifdef PMCD_INTERNAL
#define PMCD_INTERN __declspec(dllexport)
#define PMCD_EXTERN
#else
#define PMCD_INTERN
#define PMCD_EXTERN __declspec(dllimport)
#endif
#else /*!MINGW*/
#define PMCD_INTERN
#define PMCD_EXTERN extern
#endif

#include "client.h"

/* Structures of type-specific info for each kind of domain agent-PMCD 
 * connection (DSO, socket, pipe).
 */

typedef void (*DsoInitPtr)(pmdaInterface*);

typedef struct {
    char		*pathName;	/* Where the DSO lives */
    int			xlatePath;	/* translated pathname? */
    char		*entryPoint;	/* Name of the entry point */
    void		*dlHandle;	/* Handle for DSO */
    DsoInitPtr		initFn;		/* Function to initialise DSO */
					/* and return dispatch table */
    pmdaInterface	dispatch;      	/* Dispatch table for dso agent */
} DsoInfo;

typedef struct {
    int	  addrDomain;			/* AF_UNIX or AF_INET */
    int	  port;				/* Port number if an INET socket */
    char  *name;			/* Port name if supplied for INET */
					/* or socket name for UNIX */
    char  *commandLine;			/* Optional command to start agent */
    char* *argv;			/* Arg list built from commandLine */
    pid_t agentPid;			/* Process ID of agent if PMCD started */
} SocketInfo;

typedef struct {
    char* commandLine;			/* Command line to use for child */
    char* *argv;			/* Arg list built from command line */
    pid_t agentPid;			/* Process ID of the agent */
} PipeInfo;

/* The agent table and its size. */

typedef struct {
    int        pmDomainId;		/* PMD identifier */
    int        ipcType;			/* DSO, socket or pipe */
    int        pduVersion;		/* PDU_VERSION for this agent */
    int        inFd, outFd;		/* For input to/output from agent */
    int	       done;			/* Set when processed for this Fetch */
    ClientInfo *profClient;		/* Last client to send profile to agent */
    int	       profIndex;		/* Index of profile that client sent */
    char       *pmDomainLabel;		/* Textual label for agent's PMD */
    struct {				/* Status of agent */
	unsigned int
	    connected : 1,		/* Agent connected to pmcd */
	    busy : 1,			/* Processing a request */
	    isChild : 1,		/* Is a child process of pmcd */
	    madeDsoResult : 1,		/* Pmcd made a "bad" pmResult (DSO only) */
	    restartKeep : 1,		/* Keep agent if set during restart */
	    notReady : 1,		/* Agent not ready to process PDUs */
	    startNotReady : 1;		/* Agent starts in non-ready state */
    } status;
    int		reason;			/* if ! connected */
    union {				/* per-ipcType info */
	DsoInfo    dso;
	SocketInfo socket;
	PipeInfo   pipe;
    } ipc;
} AgentInfo;

PMCD_EXTERN AgentInfo	*agent;		/* Array of domain agent structs */
PMCD_EXTERN int		nAgents;	/* Number of agents in array */

/*
 * DomainId-to-AgentIndex map
 * 9 bits of DomainId, max value is 510 because 511 is special (see
 * DYNAMIC_PMID in impl.h)
 */
#define MAXDOMID	510
extern int		mapdom[];	/* the map */

/* Domain agent-PMCD connection types (AgentInfo.ipcType) */

#define	AGENT_DSO	0
#define AGENT_SOCKET	1
#define AGENT_PIPE	2

/* Masks for operations used in access controls for clients. */
#define PMCD_OP_FETCH	0x1
#define	PMCD_OP_STORE	0x2

#define PMCD_OP_NONE	0x0
#define	PMCD_OP_ALL	0x3

/* Agent termination reasons */
#define AT_CONFIG	1
#define AT_COMM		2
#define AT_EXIT		3

/*
 * Agent termination reasons for "reason" in AgentInfo, and pmcd.agent.state
 * as exported by PMCD PMDA ... these encode the low byte, next byte contains
 * exit status and next byte encodes signal
 */
				/* 0 connected */
				/* 1 connected, not ready */
#define REASON_EXIT	2
#define REASON_NOSTART	4
#define REASON_PROTOCOL	8

extern AgentInfo *FindDomainAgent(int);
extern void CleanupAgent(AgentInfo *, int, int);
extern int HarvestAgents(unsigned int);
extern void CleanupClient(ClientInfo*, int);
extern char* FdToString(int);
extern pmResult **SplitResult(pmResult *);
extern void Shutdown(void);

/* timeout to PMDAs (secs) */
PMCD_EXTERN int	_pmcd_timeout;

/* timeout for credentials */
extern int	_creds_timeout;

/* global PMCD PMDA variables */

/*
 * trace types
 */

#define TR_ADD_CLIENT	1
#define TR_DEL_CLIENT	2
#define TR_ADD_AGENT	3
#define TR_DEL_AGENT	4
#define TR_EOF		5
#define TR_XMIT_PDU	7
#define TR_RECV_PDU	8
#define TR_WRONG_PDU	9
#define TR_XMIT_ERR	10
#define TR_RECV_TIMEOUT	11
#define TR_RECV_ERR	12

/*
 * trace control
 */
PMCD_EXTERN int		_pmcd_trace_mask;
PMCD_EXTERN int		_pmcd_trace_nbufs;

/*
 * trace mask bits
 */
#define TR_MASK_CONN	1
#define TR_MASK_PDU	2
#define TR_MASK_NOBUF	256

/*
 * routines
 */
extern void pmcd_init_trace(int);
extern void pmcd_trace(int, int, int, int);
extern void pmcd_dump_trace(FILE *);
extern int pmcd_load_libpcp_pmda(void);

/*
 * set state change status for clients
 */
extern void MarkStateChanges(int);

/*
 * Highest known file descriptor used for a Client or an Agent connection.
 * This is reported in the pmcd.openfds metric.
 */
PMCD_EXTERN int pmcd_hi_openfds;
extern void pmcd_openfds_sethi(int fd);

#endif /* _PMCD_H */
