/*
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

#ifndef _LOGGER_H
#define _LOGGER_H

#include "pmapi.h"
#include "impl.h"
#include <assert.h>

/*
 * a task is a bundle of fetches to be done together
 */
typedef struct _task {
    struct _task	*t_next;
    struct timeval	t_delta;
    int			t_state;	/* logging state */
    int			t_numpmid;
    pmID		*t_pmidlist;
    char		**t_namelist;
    pmDesc		*t_desclist;
    fetchctl_t		*t_fetch;
    int			t_afid;
    int			t_size;
} task_t;

extern task_t		*tasklist;	/* master list of tasks */
extern __pmLogCtl	logctl;		/* global log control */

/* config file parser states */
#define GLOBAL  0
#define INSPEC  1

/* generic error messages */
extern char	*chk_emess[];
extern void die(char *, int);

/*
 * hash control for per-metric (really per-metric per-log specification)
 * -- used to establish and maintain state for ControlLog operations
 */
extern __pmHashCtl	pm_hash;

/* another hash list used for maintaining information about all metrics and
 * instances that have EVER appeared in the log as opposed to just those
 * currently being logged.  It's a history list.
 */
extern __pmHashCtl	hist_hash;

typedef struct {
    int	ih_inst;
    int	ih_flags;
} insthist_t;

typedef struct {
    pmID	ph_pmid;
    pmInDom	ph_indom;
    int		ph_numinst;
    insthist_t	*ph_instlist;
} pmidhist_t;

/* access control goo */
#define PM_OP_LOG_ADV	0x1
#define PM_OP_LOG_MAND	0x2
#define PM_OP_LOG_ENQ	0x4

#define PM_OP_NONE	0x0
#define PM_OP_ALL	0x7

#define PMLC_SET_MAYBE(val, flag) \
	val = ((val) & ~0x10) | (((flag) & 0x1) << 4)
#define PMLC_GET_MAYBE(val) \
	(((val) & 0x10) >> 4)

/* volume switch types */
#define VOL_SW_SIGHUP  0
#define VOL_SW_PMLC    1
#define VOL_SW_COUNTER 2
#define VOL_SW_BYTES   3
#define VOL_SW_TIME    4
#define VOL_SW_MAX     5

/* flush requested via SIGUSR1 */
extern int	wantflush;

/* unbuffered writes requested via -u */
extern int	unbuffered;

/* initial time of day from remote PMCD */
extern struct timeval	epoch;

/* offset to start of last written pmResult */
extern int	last_log_offset;

/* yylex() gets input from here ... */
extern FILE		*fconfig;
extern FILE		*yyin;
extern char		*configfile;
extern int		lineno;

extern int myFetch(int, pmID *, __pmPDU **);
extern void yyerror(char *);
extern void yywarn(char *);
extern int yylex(void);
extern int yyparse(void);
extern void dometric(const char *);
extern void buildinst(int *, int **, char ***, int, char *);
extern void freeinst(int *, int *, char **);
extern void log_callback(int, void *);
extern int chk_one(task_t *, pmID, int);
extern int chk_all(task_t *, pmID);
extern optreq_t *findoptreq(pmID, int);
extern int newvolume(int);
extern void disconnect(int);
#if CAN_RECONNECT
extern int reconnect(void);
#endif
extern int do_preamble(void);
extern void run_done(int,char *);
extern __pmPDU *rewrite_pdu(__pmPDU *, int);
extern int putmark(void);
extern int do_flush(void);

#include <sys/param.h>
extern char pmlc_host[];

#define LOG_DELTA_ONCE		-1
#define LOG_DELTA_DEFAULT	-2

/* command line parameters */
extern char	    	*archBase;		/* base name for log files */
extern char		*pmcd_host;		/* collecting from PMCD on this host */
extern int		primary;		/* Non-zero for primary logger */
extern int		rflag;
extern struct timeval	delta;			/* default logging interval */
extern int		ctlport;		/* pmlogger control port number */
extern char		*note;			/* note for port map file */

/* pmlc support */
extern void init_ports(void);
extern int control_req(void);
extern int client_req(void);
extern __pmHashCtl	hist_hash;
extern unsigned int	clientops;	/* access control (deny ops) */
extern struct timeval	last_stamp;
extern int		clientfd;
extern int		ctlfd;
extern int		exit_samples;
extern int		vol_switch_samples;
extern __int64_t	vol_switch_bytes;
extern int		vol_samples_counter;
extern int		archive_version; 
extern int		parse_done;
extern __int64_t	exit_bytes;
extern __int64_t	vol_bytes;

/* event record handling */
extern int do_events(pmValueSet *);

/* QA testing and error injection support ... see do_request() */
extern int	qa_case;
#define QA_OFF		100
#define QA_SLEEPY	101

#endif /* _LOGGER_H */
