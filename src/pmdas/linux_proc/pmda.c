/*
 * proc PMDA
 *
 * Copyright (c) 2000,2004,2007-2008 Silicon Graphics, Inc.  All Rights Reserved.
 * Portions Copyright (c) 2002 International Business Machines Corp.
 * Portions Copyright (c) 2007-2011 Aconex.  All Rights Reserved.
 * Portions Copyright (c) 2012 Red Hat, Inc. All Rights Reserved.
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

#include "pmapi.h"
#include "impl.h"
#include "pmda.h"
#include "domain.h"
#include "dynamic.h"

#include <ctype.h>
#include <unistd.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <utmp.h>
#include <pwd.h>
#include <grp.h>

#include "../linux/convert.h"
#include "filesys.h"
#include "clusters.h"
#include "indom.h"

#include "getinfo.h"
#include "proc_pid.h"
#include "proc_runq.h"
#include "ksym.h"
#include "cgroups.h"

static proc_pid_t		proc_pid;
static struct utsname		kernel_uname;
static proc_runq_t		proc_runq;

static int		_isDSO = 1;	/* =0 I am a daemon */

/* globals */
size_t _pm_system_pagesize; /* for hinv.pagesize and used elsewhere */

/*
 * The proc instance domain table is direct lookup and sparse.
 * It is initialized in proc_init(), see below.
 */
pmdaIndom indomtab[NUM_INDOMS];

/*
 * all metrics supported in this PMDA - one table entry for each
 */
pmdaMetric proc_metrictab[] = {

/*
 * proc/<pid>/stat cluster
 */

/* proc.nprocs */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,99), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.pid */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,0), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.cmd */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,1), PM_TYPE_STRING, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.sname */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,2), PM_TYPE_STRING, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.ppid */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,3), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.pgrp */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,4), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.session */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,5), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.tty */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,6), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.tty_pgrp */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,7), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.flags */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,8), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.minflt */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,9), PM_TYPE_U32, PROC_INDOM, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* proc.psinfo.cmin_flt */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,10), PM_TYPE_U32, PROC_INDOM, PM_SEM_COUNTER, 
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* proc.psinfo.maj_flt */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,11), PM_TYPE_U32, PROC_INDOM, PM_SEM_COUNTER, 
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* proc.psinfo.cmaj_flt */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,12), PM_TYPE_U32, PROC_INDOM, PM_SEM_COUNTER, 
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* proc.psinfo.utime */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,13), KERNEL_ULONG, PROC_INDOM, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) } },

/* proc.psinfo.stime */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,14), KERNEL_ULONG, PROC_INDOM, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) } },

/* proc.psinfo.cutime */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,15), KERNEL_ULONG, PROC_INDOM, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) } },

/* proc.psinfo.cstime */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,16), KERNEL_ULONG, PROC_INDOM, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) } },

/* proc.psinfo.priority */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,17), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.nice */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,18), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

#if 0
/* invalid field */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,19), PM_TYPE_U32, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },
#endif

/* proc.psinfo.it_real_value */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,20), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.start_time */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,21), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,1,0,0,PM_TIME_SEC,0) } },

/* proc.psinfo.vsize */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,22), PM_TYPE_U32, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* proc.psinfo.rss */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,23), PM_TYPE_U32, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* proc.psinfo.rss_rlim */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,24), PM_TYPE_U32, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* proc.psinfo.start_code */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,25), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.end_code */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,26), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.start_stack */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,27), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.esp */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,28), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.eip */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,29), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.signal */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,30), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.blocked */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,31), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.sigignore */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,32), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.sigcatch */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,33), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.wchan */
#if defined(HAVE_64BIT_PTR)
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,34), PM_TYPE_U64, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },
#elif defined(HAVE_32BIT_PTR)
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,34), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },
#else
    error! unsupported pointer size
#endif

/* proc.psinfo.nswap */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,35), PM_TYPE_U32, PROC_INDOM, PM_SEM_COUNTER, 
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* proc.psinfo.cnswap */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,36), PM_TYPE_U32, PROC_INDOM, PM_SEM_COUNTER, 
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* proc.psinfo.exit_signal */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,37), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.processor -- added by Mike Mason <mmlnx@us.ibm.com> */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,38), PM_TYPE_U32, PROC_INDOM, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.psinfo.ttyname */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,39), PM_TYPE_STRING, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* proc.psinfo.wchan_s -- added by Mike Mason <mmlnx@us.ibm.com> */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,40), PM_TYPE_STRING, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.psinfo.psargs -- modified by Mike Mason <mmlnx@us.ibm.com> */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STAT,41), PM_TYPE_STRING, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/*
 * proc/<pid>/status cluster
 * Cluster added by Mike Mason <mmlnx@us.ibm.com>
 */

/* proc.id.uid */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,0), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.id.euid */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,1), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.id.suid */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,2), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.id.fsuid */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,3), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.id.gid */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,4), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.id.egid */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,5), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.id.sgid */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,6), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.id.fsgid */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,7), PM_TYPE_U32, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.id.uid_nm */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,8), PM_TYPE_STRING, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.id.euid_nm */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,9), PM_TYPE_STRING, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.id.suid_nm */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,10), PM_TYPE_STRING, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.id.fsuid_nm */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,11), PM_TYPE_STRING, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.id.gid_nm */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,12), PM_TYPE_STRING, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.id.egid_nm */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,13), PM_TYPE_STRING, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.id.sgid_nm */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,14), PM_TYPE_STRING, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.id.fsgid_nm */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,15), PM_TYPE_STRING, PROC_INDOM, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.psinfo.signal_s */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,16), PM_TYPE_STRING, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.psinfo.blocked_s */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,17), PM_TYPE_STRING, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.psinfo.sigignore_s */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,18), PM_TYPE_STRING, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.psinfo.sigcatch_s */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,19), PM_TYPE_STRING, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* proc.memory.vmsize */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,20), PM_TYPE_U32, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0)}},

/* proc.memory.vmlock */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,21), PM_TYPE_U32, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0)}},

/* proc.memory.vmrss */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,22), PM_TYPE_U32, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0)}},

/* proc.memory.vmdata */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,23), PM_TYPE_U32, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0)}},

/* proc.memory.vmstack */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,24), PM_TYPE_U32, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0)}},

/* proc.memory.vmexe */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,25), PM_TYPE_U32, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0)}},

/* proc.memory.vmlib */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATUS,26), PM_TYPE_U32, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0)}},

/*
 * proc/<pid>/statm cluster
 */

/* proc.memory.size */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATM,0), PM_TYPE_U32, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* proc.memory.rss */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATM,1), PM_TYPE_U32, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* proc.memory.share */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATM,2), PM_TYPE_U32, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* proc.memory.textrss */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATM,3), PM_TYPE_U32, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* proc.memory.librss */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATM,4), PM_TYPE_U32, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* proc.memory.datrss */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATM,5), PM_TYPE_U32, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* proc.memory.dirty */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATM,6), PM_TYPE_U32, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* proc.memory.maps -- added by Mike Mason <mmlnx@us.ibm.com> */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_STATM,7), PM_TYPE_STRING, PROC_INDOM, PM_SEM_INSTANT, 
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/*
 * proc/<pid>/schedstat cluster
 */

/* proc.schedstat.cpu_time */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_SCHEDSTAT,0), PM_TYPE_U64, PROC_INDOM, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,1,0,0,PM_TIME_NSEC,0)}},
/* proc.schedstat.run_delay */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_SCHEDSTAT,1), PM_TYPE_U64, PROC_INDOM, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,1,0,0,PM_TIME_NSEC,0)}},
/* proc.schedstat.pcount */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_SCHEDSTAT,2), KERNEL_ULONG, PROC_INDOM, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE)}},

/*
 * proc/<pid>/io cluster
 */
/* proc.io.rchar */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_IO,0), PM_TYPE_U64, PROC_INDOM, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE)}},
/* proc.io.wchar */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_IO,1), PM_TYPE_U64, PROC_INDOM, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE)}},
/* proc.io.syscr */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_IO,2), PM_TYPE_U64, PROC_INDOM, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE)}},
/* proc.io.syscw */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_IO,3), PM_TYPE_U64, PROC_INDOM, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE)}},
/* proc.io.read_bytes */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_IO,4), PM_TYPE_U64, PROC_INDOM, PM_SEM_COUNTER,
    PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0)}},
/* proc.io.write_bytes */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_IO,5), PM_TYPE_U64, PROC_INDOM, PM_SEM_COUNTER,
    PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0)}},
/* proc.io.cancelled_write_bytes */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_IO,6), PM_TYPE_U64, PROC_INDOM, PM_SEM_COUNTER,
    PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0)}},

/*
 * proc.runq cluster
 */

/* proc.runq.runnable */
  { &proc_runq.runnable,
    { PMDA_PMID(CLUSTER_PROC_RUNQ, 0), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* proc.runq.blocked */
  { &proc_runq.blocked,
    { PMDA_PMID(CLUSTER_PROC_RUNQ, 1), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* proc.runq.sleeping */
  { &proc_runq.sleeping,
    { PMDA_PMID(CLUSTER_PROC_RUNQ, 2), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* proc.runq.stopped */
  { &proc_runq.stopped,
    { PMDA_PMID(CLUSTER_PROC_RUNQ, 3), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* proc.runq.swapped */
  { &proc_runq.swapped,
    { PMDA_PMID(CLUSTER_PROC_RUNQ, 4), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* proc.runq.defunct */
  { &proc_runq.defunct,
    { PMDA_PMID(CLUSTER_PROC_RUNQ, 5), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* proc.runq.unknown */
  { &proc_runq.unknown,
    { PMDA_PMID(CLUSTER_PROC_RUNQ, 6), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* proc.runq.kernel */
  { &proc_runq.kernel,
    { PMDA_PMID(CLUSTER_PROC_RUNQ, 7), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },



/*
 * control groups cluster
 */
    /* cgroups.subsys.hierarchy */
    { NULL, {PMDA_PMID(CLUSTER_CGROUP_SUBSYS,0), PM_TYPE_U32,
    CGROUP_SUBSYS_INDOM, PM_SEM_INSTANT, PMDA_PMUNITS(0,0,0,0,0,0) }, },
    
    /* cgroups.subsys.count */
    { NULL, {PMDA_PMID(CLUSTER_CGROUP_SUBSYS,1), PM_TYPE_U32,
    PM_INDOM_NULL, PM_SEM_INSTANT, PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* cgroups.mounts.subsys */
    { NULL, {PMDA_PMID(CLUSTER_CGROUP_MOUNTS,0), PM_TYPE_STRING,
    CGROUP_MOUNTS_INDOM, PM_SEM_INSTANT, PMDA_PMUNITS(0,0,0,0,0,0) }, },

    /* cgroups.mounts.count */
    { NULL, {PMDA_PMID(CLUSTER_CGROUP_MOUNTS,1), PM_TYPE_U32,
    PM_INDOM_NULL, PM_SEM_INSTANT, PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

#if 0	/* not yet implemented */
    /* cgroup.groups.cpuset.[<group>.]tasks.pid */
    { NULL, {PMDA_PMID(CLUSTER_CPUSET_PROCS,0), PM_TYPE_U32,
    PROC_INDOM, PM_SEM_INSTANT, PMDA_PMUNITS(0,0,0,0,0,0) }, },
#endif

    /* cgroup.groups.cpuset.[<group>.]cpus */
    { NULL, {PMDA_PMID(CLUSTER_CPUSET_GROUPS,0), PM_TYPE_STRING,
    PM_INDOM_NULL, PM_SEM_INSTANT, PMDA_PMUNITS(0,0,0,0,0,0) }, },

    /* cgroup.groups.cpuset.[<group>.]mems */
    { NULL, {PMDA_PMID(CLUSTER_CPUSET_GROUPS,1), PM_TYPE_STRING,
    PM_INDOM_NULL, PM_SEM_INSTANT, PMDA_PMUNITS(0,0,0,0,0,0) }, },

#if 0	/* not yet implemented */
    /* cgroup.groups.cpuacct.[<group>.]tasks.pid */
    { NULL, {PMDA_PMID(CLUSTER_CPUACCT_PROCS,0), PM_TYPE_U32,
    PROC_INDOM, PM_SEM_INSTANT, PMDA_PMUNITS(0,0,0,0,0,0) }, },
#endif

    /* cgroup.groups.cpuacct.[<group>.]stat.user */
    { NULL, {PMDA_PMID(CLUSTER_CPUACCT_GROUPS,0), PM_TYPE_U64,
    PM_INDOM_NULL, PM_SEM_COUNTER, PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

    /* cgroup.groups.cpuacct.[<group>.]stat.system */
    { NULL, {PMDA_PMID(CLUSTER_CPUACCT_GROUPS,1), PM_TYPE_U64,
    PM_INDOM_NULL, PM_SEM_COUNTER, PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

    /* cgroup.groups.cpuacct.[<group>.]usage */
    { NULL, {PMDA_PMID(CLUSTER_CPUACCT_GROUPS,2), PM_TYPE_U64,
    PM_INDOM_NULL, PM_SEM_COUNTER, PMDA_PMUNITS(0,1,0,0,PM_TIME_NSEC,0) }, },

    /* cgroup.groups.cpuacct.[<group>.]usage_percpu */
    { NULL, {PMDA_PMID(CLUSTER_CPUACCT_GROUPS,3), PM_TYPE_U64,
    CPU_INDOM, PM_SEM_COUNTER, PMDA_PMUNITS(0,1,0,0,PM_TIME_NSEC,0) }, },

#if 0
    /* cgroup.groups.cpusched.[<group>.]tasks.pid */
    { NULL, {PMDA_PMID(CLUSTER_CPUSCHED_PROCS,0), PM_TYPE_U32,
    PROC_INDOM, PM_SEM_INSTANT, PMDA_PMUNITS(0,0,0,0,0,0) }, },
#endif

    /* cgroup.groups.cpusched.[<group>.]shares */
    { NULL, {PMDA_PMID(CLUSTER_CPUSCHED_GROUPS,0), PM_TYPE_U64,
    PM_INDOM_NULL, PM_SEM_COUNTER, PMDA_PMUNITS(0,0,0,0,0,0) }, },

#if 0
    /* cgroup.groups.memory.[<group>.]tasks.pid */
    { NULL, {PMDA_PMID(CLUSTER_MEMORY_PROCS,0), PM_TYPE_U32,
    PROC_INDOM, PM_SEM_INSTANT, PMDA_PMUNITS(0,0,0,0,0,0) }, },
#endif

    /* cgroup.groups.memory.[<group>.]stat.cache */
    { NULL, {PMDA_PMID(CLUSTER_MEMORY_GROUPS,0), PM_TYPE_U64,
    PM_INDOM_NULL, PM_SEM_COUNTER, PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

    /* cgroup.groups.memory.[<group>.]stat.rss */
    { NULL, {PMDA_PMID(CLUSTER_MEMORY_GROUPS,1), PM_TYPE_U64,
    PM_INDOM_NULL, PM_SEM_COUNTER, PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

    /* cgroup.groups.memory.[<group>.]stat.pgin */
    { NULL, {PMDA_PMID(CLUSTER_MEMORY_GROUPS,2), PM_TYPE_U64,
    PM_INDOM_NULL, PM_SEM_COUNTER, PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

    /* cgroup.groups.memory.[<group>.]stat.pgout */
    { NULL, {PMDA_PMID(CLUSTER_MEMORY_GROUPS,3), PM_TYPE_U64,
    PM_INDOM_NULL, PM_SEM_COUNTER, PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

    /* cgroup.groups.memory.[<group>.]stat.swap */
    { NULL, {PMDA_PMID(CLUSTER_MEMORY_GROUPS,4), PM_TYPE_U64,
    PM_INDOM_NULL, PM_SEM_COUNTER, PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

    /* cgroup.groups.memory.[<group>.]stat.active_anon */
    { NULL, {PMDA_PMID(CLUSTER_MEMORY_GROUPS,5), PM_TYPE_U64,
    PM_INDOM_NULL, PM_SEM_COUNTER, PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

    /* cgroup.groups.memory.[<group>.]stat.inactive_anon */
    { NULL, {PMDA_PMID(CLUSTER_MEMORY_GROUPS,6), PM_TYPE_U64,
    PM_INDOM_NULL, PM_SEM_COUNTER, PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

    /* cgroup.groups.memory.[<group>.]stat.active_file */
    { NULL, {PMDA_PMID(CLUSTER_MEMORY_GROUPS,7), PM_TYPE_U64,
    PM_INDOM_NULL, PM_SEM_COUNTER, PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

    /* cgroup.groups.memory.[<group>.]stat.inactive_file */
    { NULL, {PMDA_PMID(CLUSTER_MEMORY_GROUPS,8), PM_TYPE_U64,
    PM_INDOM_NULL, PM_SEM_COUNTER, PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

    /* cgroup.groups.memory.[<group>.]stat.unevictable */
    { NULL, {PMDA_PMID(CLUSTER_MEMORY_GROUPS,9), PM_TYPE_U64,
    PM_INDOM_NULL, PM_SEM_COUNTER, PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

#if 0
    /* cgroup.groups.netclass.[<group>.]tasks.pid */
    { NULL, {PMDA_PMID(CLUSTER_NET_CLS_PROCS,0), PM_TYPE_U32,
    PROC_INDOM, PM_SEM_INSTANT, PMDA_PMUNITS(0,0,0,0,0,0) }, },
#endif

    /* cgroup.groups.netclass.[<group>.]classid */
    { NULL, {PMDA_PMID(CLUSTER_NET_CLS_GROUPS,0), PM_TYPE_U64,
    PM_INDOM_NULL, PM_SEM_COUNTER, PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/*
 * proc/<pid>/fd cluster
 */

    /* proc.fd.count */
    { NULL, {PMDA_PMID(CLUSTER_PID_FD,0), PM_TYPE_U32, PROC_INDOM, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },
};

static int
refresh_cpu_indom(pmInDom indom)
{
    char buf[MAXPATHLEN];
    char *space;
    FILE *fp;

    if ((fp = fopen("/proc/stat", "r")) == (FILE *)NULL)
        return -oserror();
    pmdaCacheOp(indom, PMDA_CACHE_INACTIVE);
    while (fgets(buf, sizeof(buf), fp) != NULL) {
    	if (strncmp(buf, "cpu", 3) == 0 && isdigit((int)buf[3])) {
	    if ((space = strchr(buf, ' ')) != NULL) {
	    	*space = '\0';
		pmdaCacheStore(indom, PMDA_CACHE_ADD, buf, NULL);
	    }
	}
    }
    fclose(fp);

    return pmdaCacheOp(indom, PMDA_CACHE_SIZE_ACTIVE);
}

int
refresh_cgroups(pmdaExt *pmda, __pmnsTree **tree)
{
    int changed;
    time_t rightnow = 0;
    static time_t previoustime;
    static __pmnsTree *previoustree;

    if (tree) {
	if ((rightnow = time(NULL)) == previoustime) {
	    *tree = previoustree;
	    return 0;
	}
    }

    refresh_cpu_indom(INDOM(CPU_INDOM));
    refresh_filesys(INDOM(CGROUP_MOUNTS_INDOM));
    refresh_cgroup_subsys(INDOM(CGROUP_SUBSYS_INDOM));
    changed = refresh_cgroup_groups(pmda, INDOM(CGROUP_MOUNTS_INDOM), tree);

    if (tree) {
	previoustime = rightnow;
	previoustree = *tree;
    }
    return changed;
}

static void
proc_refresh(pmdaExt *pmda, int *need_refresh)
{
    int need_refresh_mtab = 0;

    if (need_refresh[CLUSTER_CGROUP_SUBSYS] ||
	need_refresh[CLUSTER_CGROUP_MOUNTS] ||
	need_refresh[CLUSTER_CPUSET_PROCS] ||
	need_refresh[CLUSTER_CPUSET_GROUPS] ||
	need_refresh[CLUSTER_CPUACCT_PROCS] || 
	need_refresh[CLUSTER_CPUACCT_GROUPS] || 
	need_refresh[CLUSTER_CPUSCHED_PROCS] ||
	need_refresh[CLUSTER_CPUSCHED_GROUPS] ||
	need_refresh[CLUSTER_MEMORY_PROCS] ||
	need_refresh[CLUSTER_MEMORY_GROUPS] ||
        need_refresh[CLUSTER_NET_CLS_PROCS] ||
        need_refresh[CLUSTER_NET_CLS_GROUPS]) {
	need_refresh_mtab |= refresh_cgroups(pmda, NULL);
    }

    if (need_refresh[CLUSTER_PID_STAT] || need_refresh[CLUSTER_PID_STATM] || 
	need_refresh[CLUSTER_PID_STATUS] || need_refresh[CLUSTER_PID_IO] ||
	need_refresh[CLUSTER_PID_SCHEDSTAT] || need_refresh[CLUSTER_PID_FD])
	refresh_proc_pid(&proc_pid);

    if (need_refresh[CLUSTER_PROC_RUNQ])
	refresh_proc_runq(&proc_runq);

    if (need_refresh_mtab)
	proc_dynamic_metrictable(pmda);
}

static int
proc_instance(pmInDom indom, int inst, char *name, __pmInResult **result, pmdaExt *pmda)
{
    __pmInDom_int	*indomp = (__pmInDom_int *)&indom;
    int			need_refresh[NUM_CLUSTERS];
    char		newname[11];		/* see Note below */

    memset(need_refresh, 0, sizeof(need_refresh));
    switch (indomp->serial) {
    case CPU_INDOM:
	/*
	 * Used by cgroup.groups.cpuacct.[<group>.]usage_percpu
	 * and cgroup.groups.cpuacct.usage_percpu
	 */
	need_refresh[CLUSTER_CPUACCT_GROUPS]++;
	break;
    case PROC_INDOM:
    	need_refresh[CLUSTER_PID_STAT]++;
    	need_refresh[CLUSTER_PID_STATM]++;
        need_refresh[CLUSTER_PID_STATUS]++;
        need_refresh[CLUSTER_PID_SCHEDSTAT]++;
        need_refresh[CLUSTER_PID_IO]++;
        need_refresh[CLUSTER_PID_FD]++;
	break;
    case CGROUP_SUBSYS_INDOM:
    	need_refresh[CLUSTER_CGROUP_SUBSYS]++;
	break;
    case CGROUP_MOUNTS_INDOM:
    	need_refresh[CLUSTER_CGROUP_MOUNTS]++;
	break;
    /* no default label : pmdaInstance will pick up errors */
    }

    if (indomp->serial == PROC_INDOM && inst == PM_IN_NULL && name != NULL) {
    	/*
	 * For the proc indom, if the name is a pid (as a string), and it
	 * contains only digits (i.e. it's not a full instance name) then
	 * reformat it to be exactly six digits, with leading zeros.
	 *
	 * Note that although format %06d is used here and in proc_pid.c,
	 *      the pid could be longer than this (in which case there
	 *      are no leading zeroes.  The size of newname[] is chosen
	 *	to comfortably accommodate a 32-bit pid (Linux maximum),
	 *      or max value of 4294967295 (10 digits)
	 */
	char *p;
	for (p = name; *p != '\0'; p++) {
	    if (!isdigit((int)*p))
	    	break;
	}
	if (*p == '\0') {
	    snprintf(newname, sizeof(newname), "%06d", atoi(name));
	    name = newname;
	}
    }

    proc_refresh(pmda, need_refresh);
    return pmdaInstance(indom, inst, name, result, pmda);
}

/*
 * callback provided to pmdaFetch
 */

static int
proc_fetchCallBack(pmdaMetric *mdesc, unsigned int inst, pmAtomValue *atom)
{
    __pmID_int		*idp = (__pmID_int *)&(mdesc->m_desc.pmid);
    int			sts;
    char		*f;
    unsigned long	ul;
    int			*ip;
    proc_pid_entry_t	*entry;
    struct filesys	*fs;
    static long		hz = -1;

    if (hz == -1)
    	hz = sysconf(_SC_CLK_TCK);

    if (mdesc->m_user != NULL) {
	/* 
	 * The metric value is extracted directly via the address specified
	 * in metrictab.  Note: not all metrics support this - those that
	 * don't have NULL for the m_user field in their respective
         * metrictab slot.
	 */

	switch (mdesc->m_desc.type) {
	case PM_TYPE_32:
	    atom->l = *(__int32_t *)mdesc->m_user;
	    break;
	case PM_TYPE_U32:
	    atom->ul = *(__uint32_t *)mdesc->m_user;
	    break;
	case PM_TYPE_64:
	    atom->ll = *(__int64_t *)mdesc->m_user;
	    break;
	case PM_TYPE_U64:
	    atom->ull = *(__uint64_t *)mdesc->m_user;
	    break;
	case PM_TYPE_FLOAT:
	    atom->f = *(float *)mdesc->m_user;
	    break;
	case PM_TYPE_DOUBLE:
	    atom->d = *(double *)mdesc->m_user;
	    break;
	case PM_TYPE_STRING:
	    atom->cp = (char *)mdesc->m_user;
	    break;
	default:
	    return 0;
	}
    }
    else
    switch (idp->cluster) {

    case CLUSTER_PID_STAT:
	if (idp->item == 99) /* proc.nprocs */
	    atom->ul = proc_pid.indom->it_numinst;
	else {
	    static char ttyname[MAXPATHLEN];

	    if ((entry = fetch_proc_pid_stat(inst, &proc_pid)) == NULL)
	    	return PM_ERR_INST;

	    switch (idp->item) {


	    case PROC_PID_STAT_PID:
	    	atom->ul = entry->id;
		break;

	    case PROC_PID_STAT_TTYNAME:
		if ((f = _pm_getfield(entry->stat_buf, PROC_PID_STAT_TTY)) == NULL)
		    atom->cp = "?";
		else {
		    dev_t dev = (dev_t)atoi(f);
		    atom->cp = get_ttyname_info(inst, dev, ttyname);
		}
		break;

	    case PROC_PID_STAT_CMD:
		if ((f = _pm_getfield(entry->stat_buf, idp->item)) == NULL)
		    return PM_ERR_INST;
		atom->cp = f + 1;
		atom->cp[strlen(atom->cp)-1] = '\0';
		break;

	    case PROC_PID_STAT_PSARGS:
		atom->cp = entry->name + 7;
		break;

	    case PROC_PID_STAT_STATE:
		/*
		 * string
		 */
		if ((f = _pm_getfield(entry->stat_buf, idp->item)) == NULL)
		    return PM_ERR_INST;
	    	atom->cp = f;
		break;

	    case PROC_PID_STAT_VSIZE:
	    case PROC_PID_STAT_RSS_RLIM:
		/*
		 * bytes converted to kbytes
		 */
		if ((f = _pm_getfield(entry->stat_buf, idp->item)) == NULL)
		    return PM_ERR_INST;
		sscanf(f, "%u", &atom->ul);
		atom->ul /= 1024;
		break;

	    case PROC_PID_STAT_RSS:
		/*
		 * pages converted to kbytes
		 */
		if ((f = _pm_getfield(entry->stat_buf, idp->item)) == NULL)
		    return PM_ERR_INST;
		sscanf(f, "%u", &atom->ul);
		atom->ul *= _pm_system_pagesize / 1024;
		break;

	    case PROC_PID_STAT_UTIME:
	    case PROC_PID_STAT_STIME:
	    case PROC_PID_STAT_CUTIME:
	    case PROC_PID_STAT_CSTIME:
		/*
		 * unsigned jiffies converted to unsigned milliseconds
		 */
		if ((f = _pm_getfield(entry->stat_buf, idp->item)) == NULL)
		    return PM_ERR_INST;

		sscanf(f, "%lu", &ul);
		_pm_assign_ulong(atom, 1000 * (double)ul / hz);
		break;
	    
	    case PROC_PID_STAT_PRIORITY:
	    case PROC_PID_STAT_NICE:
		/*
		 * signed decimal int
		 */
		if ((f = _pm_getfield(entry->stat_buf, idp->item)) == NULL)
		    return PM_ERR_INST;
		sscanf(f, "%d", &atom->l);
		break;

	    case PROC_PID_STAT_WCHAN:
		if ((f = _pm_getfield(entry->stat_buf, idp->item)) == NULL)
			return PM_ERR_INST;
#if defined(HAVE_64BIT_PTR)
		sscanf(f, "%lu", &atom->ull); /* 64bit address */
#else
		sscanf(f, "%u", &atom->ul);    /* 32bit address */
#endif
		break;

	    case PROC_PID_STAT_WCHAN_SYMBOL:
		if (entry->wchan_buf)	/* 2.6 kernel, /proc/<pid>/wchan */
		    atom->cp = entry->wchan_buf;
		else {		/* old school (2.4 kernels, at least) */
		    char *wc;
		    /*
		     * Convert address to symbol name if requested
		     * Added by Mike Mason <mmlnx@us.ibm.com>
		     */
		    f = _pm_getfield(entry->stat_buf, PROC_PID_STAT_WCHAN);
		    if (f == NULL)
			return PM_ERR_INST;
#if defined(HAVE_64BIT_PTR)
		    sscanf(f, "%lu", &atom->ull); /* 64bit address */
		    if ((wc = wchan(atom->ull)))
			atom->cp = wc;
		    else
			atom->cp = atom->ull ? f : "";
#else
		    sscanf(f, "%u", &atom->ul);    /* 32bit address */
		    if ((wc = wchan((__psint_t)atom->ul)))
			atom->cp = wc;
		    else
			atom->cp = atom->ul ? f : "";
#endif
		}
		break;

	    default:
		/*
		 * unsigned decimal int
		 */
		if (idp->item >= 0 && idp->item < NR_PROC_PID_STAT) {
		    if ((f = _pm_getfield(entry->stat_buf, idp->item)) == NULL)
		    	return PM_ERR_INST;
		    sscanf(f, "%u", &atom->ul);
		}
		else
		    return PM_ERR_PMID;
		break;
	    }
	}
	break;

    case CLUSTER_PID_STATM:
	if (idp->item == PROC_PID_STATM_MAPS) {	/* proc.memory.maps */
	    if ((entry = fetch_proc_pid_maps(inst, &proc_pid)) == NULL)
		    return PM_ERR_INST;
	    atom->cp = entry->maps_buf;
	} else {
	    if ((entry = fetch_proc_pid_statm(inst, &proc_pid)) == NULL)
		return PM_ERR_INST;

	    if (idp->item >= 0 && idp->item <= PROC_PID_STATM_DIRTY) {
		/* unsigned int */
		if ((f = _pm_getfield(entry->statm_buf, idp->item)) == NULL)
		    return PM_ERR_INST;
		sscanf(f, "%u", &atom->ul);
		atom->ul *= _pm_system_pagesize / 1024;
	    }
	    else
		return PM_ERR_PMID;
	}
    	break;

    case CLUSTER_PID_SCHEDSTAT:
	if ((entry = fetch_proc_pid_schedstat(inst, &proc_pid)) == NULL)
	    return (oserror() == ENOENT) ? PM_ERR_APPVERSION : PM_ERR_INST;

	if (idp->item >= 0 && idp->item < NR_PROC_PID_SCHED) {
	    if ((f = _pm_getfield(entry->schedstat_buf, idp->item)) == NULL)
		return PM_ERR_INST;
	    if (idp->item == PROC_PID_SCHED_PCOUNT &&
		mdesc->m_desc.type == PM_TYPE_U32)
		sscanf(f, "%u", &atom->ul);
	    else
#if defined(HAVE_64BIT_PTR)
		sscanf(f, "%lu", &atom->ull); /* 64bit address */
#else
		sscanf(f, "%u", &atom->ul);    /* 32bit address */
#endif
	}
	else
	    return PM_ERR_PMID;
    	break;

    case CLUSTER_PID_IO:
	if ((entry = fetch_proc_pid_io(inst, &proc_pid)) == NULL)
		return PM_ERR_INST;

	switch (idp->item) {

	case PROC_PID_IO_RCHAR:
	    if ((f = _pm_getfield(entry->io_lines.rchar, 1)) == NULL)
		atom->ull = 0;
	    else
		sscanf(f, "%llu", (unsigned long long *)&atom->ull);
	    break;
	case PROC_PID_IO_WCHAR:
	    if ((f = _pm_getfield(entry->io_lines.wchar, 1)) == NULL)
		atom->ull = 0;
	    else
		sscanf(f, "%llu", (unsigned long long *)&atom->ull);
	    break;
	case PROC_PID_IO_SYSCR:
	    if ((f = _pm_getfield(entry->io_lines.syscr, 1)) == NULL)
		atom->ull = 0;
	    else
		sscanf(f, "%llu", (unsigned long long *)&atom->ull);
	    break;
	case PROC_PID_IO_SYSCW:
	    if ((f = _pm_getfield(entry->io_lines.syscw, 1)) == NULL)
		atom->ull = 0;
	    else
		sscanf(f, "%llu", (unsigned long long *)&atom->ull);
	    break;
	case PROC_PID_IO_READ_BYTES:
	    if ((f = _pm_getfield(entry->io_lines.readb, 1)) == NULL)
		atom->ull = 0;
	    else
		sscanf(f, "%llu", (unsigned long long *)&atom->ull);
	    break;
	case PROC_PID_IO_WRITE_BYTES:
	    if ((f = _pm_getfield(entry->io_lines.writeb, 1)) == NULL)
		atom->ull = 0;
	    else
		sscanf(f, "%llu", (unsigned long long *)&atom->ull);
	    break;
	case PROC_PID_IO_CANCELLED_BYTES:
	    if ((f = _pm_getfield(entry->io_lines.cancel, 1)) == NULL)
		atom->ull = 0;
	    else
		sscanf(f, "%llu", (unsigned long long *)&atom->ull);
	    break;

	default:
	    return PM_ERR_PMID;
	}
	break;

	/*
	 * Cluster added by Mike Mason <mmlnx@us.ibm.com>
	 */
    case CLUSTER_PID_STATUS:
	if ((entry = fetch_proc_pid_status(inst, &proc_pid)) == NULL)
		return PM_ERR_INST;

	switch (idp->item) {

	case PROC_PID_STATUS_UID:
	case PROC_PID_STATUS_EUID:
	case PROC_PID_STATUS_SUID:
	case PROC_PID_STATUS_FSUID:
	case PROC_PID_STATUS_UID_NM:
	case PROC_PID_STATUS_EUID_NM:
	case PROC_PID_STATUS_SUID_NM:
	case PROC_PID_STATUS_FSUID_NM:
	{
	    struct passwd *pwe;

	    if ((f = _pm_getfield(entry->status_lines.uid, (idp->item % 4) + 1)) == NULL)
		return PM_ERR_INST;
	    sscanf(f, "%u", &atom->ul);
	    if (idp->item > PROC_PID_STATUS_FSUID) {
		if ((pwe = getpwuid((uid_t)atom->ul)) != NULL)
		    atom->cp = pwe->pw_name;
		else
		    atom->cp = "UNKNOWN";
	    }
	}
	break;

	case PROC_PID_STATUS_GID:
	case PROC_PID_STATUS_EGID:
	case PROC_PID_STATUS_SGID:
	case PROC_PID_STATUS_FSGID:
	case PROC_PID_STATUS_GID_NM:
	case PROC_PID_STATUS_EGID_NM:
	case PROC_PID_STATUS_SGID_NM:
	case PROC_PID_STATUS_FSGID_NM:
	{
	    struct group *gre;

	    if ((f = _pm_getfield(entry->status_lines.gid, (idp->item % 4) + 1)) == NULL)
		return PM_ERR_INST;
	    sscanf(f, "%u", &atom->ul);
	    if (idp->item > PROC_PID_STATUS_FSGID) {
		if ((gre = getgrgid((gid_t)atom->ul)) != NULL) {
		    atom->cp = gre->gr_name;
		} else {
		    atom->cp = "UNKNOWN";
		}
	    }
	}
	break;

	case PROC_PID_STATUS_SIGNAL:
	if ((atom->cp = _pm_getfield(entry->status_lines.sigpnd, 1)) == NULL)
	    return PM_ERR_INST;
	break;

	case PROC_PID_STATUS_BLOCKED:
	if ((atom->cp = _pm_getfield(entry->status_lines.sigblk, 1)) == NULL)
	    return PM_ERR_INST;
	break;

	case PROC_PID_STATUS_SIGCATCH:
	if ((atom->cp = _pm_getfield(entry->status_lines.sigcgt, 1)) == NULL)
	    return PM_ERR_INST;
	break;

	case PROC_PID_STATUS_SIGIGNORE:
	if ((atom->cp = _pm_getfield(entry->status_lines.sigign, 1)) == NULL)
	    return PM_ERR_INST;
	break;

	case PROC_PID_STATUS_VMSIZE:
	if ((f = _pm_getfield(entry->status_lines.vmsize, 1)) == NULL)
	    atom->ul = 0;
	else
	    sscanf(f, "%u", &atom->ul);
	break;

	case PROC_PID_STATUS_VMLOCK:
	if ((f = _pm_getfield(entry->status_lines.vmlck, 1)) == NULL)
	    atom->ul = 0;
	else
	    sscanf(f, "%u", &atom->ul);
	break;

	case PROC_PID_STATUS_VMRSS:
	if ((f = _pm_getfield(entry->status_lines.vmrss, 1)) == NULL)
	    atom->ul = 0;
	else
	    sscanf(f, "%u", &atom->ul);
	break;

	case PROC_PID_STATUS_VMDATA:
	if ((f = _pm_getfield(entry->status_lines.vmdata, 1)) == NULL)
	    atom->ul = 0;
	else
	    sscanf(f, "%u", &atom->ul);
	break;

	case PROC_PID_STATUS_VMSTACK:
	if ((f = _pm_getfield(entry->status_lines.vmstk, 1)) == NULL)
	    atom->ul = 0;
	else
	    sscanf(f, "%u", &atom->ul);
	break;

	case PROC_PID_STATUS_VMEXE:
	if ((f = _pm_getfield(entry->status_lines.vmexe, 1)) == NULL)
	    atom->ul = 0;
	else
	    sscanf(f, "%u", &atom->ul);
	break;

	case PROC_PID_STATUS_VMLIB:
	if ((f = _pm_getfield(entry->status_lines.vmlib, 1)) == NULL)
	    atom->ul = 0;
	else
	    sscanf(f, "%u", &atom->ul);
	break;

	default:
	    return PM_ERR_PMID;
	}
	break;

    case CLUSTER_CGROUP_SUBSYS:
	switch (idp->item) {
	case 0:	/* cgroup.subsys.hierarchy */
	    sts = pmdaCacheLookup(INDOM(CGROUP_SUBSYS_INDOM), inst, NULL, (void **)&ip);
	    if (sts < 0)
		return sts;
	    if (sts != PMDA_CACHE_ACTIVE)
	    	return PM_ERR_INST;
	    atom->ul = *ip;
	    break;

	case 1: /* cgroup.subsys.count */
	    atom->ul = pmdaCacheOp(INDOM(CGROUP_SUBSYS_INDOM), PMDA_CACHE_SIZE_ACTIVE);
	    break;
	}
	break;

    case CLUSTER_CGROUP_MOUNTS:
	switch (idp->item) {
	case 0:	/* cgroup.mounts.subsys */
	    sts = pmdaCacheLookup(INDOM(CGROUP_MOUNTS_INDOM), inst, NULL, (void **)&fs);
	    if (sts < 0)
		return sts;
	    if (sts != PMDA_CACHE_ACTIVE)
	    	return PM_ERR_INST;
	    atom->cp = cgroup_find_subsys(INDOM(CGROUP_SUBSYS_INDOM), fs->options);
	    break;

	case 1: /* cgroup.mounts.count */
	    atom->ul = pmdaCacheOp(INDOM(CGROUP_MOUNTS_INDOM), PMDA_CACHE_SIZE_ACTIVE);
	    break;
	}
	break;

    case CLUSTER_CPUSET_GROUPS:
    case CLUSTER_CPUACCT_GROUPS:
    case CLUSTER_CPUSCHED_GROUPS:
    case CLUSTER_MEMORY_GROUPS:
    case CLUSTER_NET_CLS_GROUPS:
	return cgroup_group_fetch(idp->cluster, idp->item, inst, atom);

    case CLUSTER_CPUSET_PROCS:
    case CLUSTER_CPUACCT_PROCS:
    case CLUSTER_CPUSCHED_PROCS:
    case CLUSTER_MEMORY_PROCS:
    case CLUSTER_NET_CLS_PROCS:
	return cgroup_procs_fetch(idp->cluster, idp->item, inst, atom);

    case CLUSTER_PID_FD:
	if ((entry = fetch_proc_pid_fd(inst, &proc_pid)) == NULL)
		return PM_ERR_INST;
	if (idp->item != PROC_PID_FD_COUNT)
		return PM_ERR_INST;

	atom->ul = entry->fd_count;
	break;

    default: /* unknown cluster */
	return PM_ERR_PMID;
    }

    return 1;
}


static int
proc_fetch(int numpmid, pmID pmidlist[], pmResult **resp, pmdaExt *pmda)
{
    int		i;
    int		need_refresh[NUM_CLUSTERS];

    memset(need_refresh, 0, sizeof(need_refresh));
    for (i=0; i < numpmid; i++) {
	__pmID_int *idp = (__pmID_int *)&(pmidlist[i]);
	if (idp->cluster >= 0 && idp->cluster < NUM_CLUSTERS) {
	    need_refresh[idp->cluster]++;
	}

    }

    proc_refresh(pmda, need_refresh);
    return pmdaFetch(numpmid, pmidlist, resp, pmda);
}

static int
proc_store(pmResult *result, pmdaExt *pmda)
{
    return PM_ERR_PERMISSION;
}

static int
proc_text(int ident, int type, char **buf, pmdaExt *pmda)
{
    if ((type & PM_TEXT_PMID) == PM_TEXT_PMID) {
	int sts = proc_dynamic_lookup_text(ident, type, buf, pmda);
	if (sts != -ENOENT)
	    return sts;
    }
    return pmdaText(ident, type, buf, pmda);
}

static int
proc_pmid(const char *name, pmID *pmid, pmdaExt *pmda)
{
    __pmnsTree *tree = proc_dynamic_lookup_name(pmda, name);
    if (tree == NULL)
	return PM_ERR_NAME;
    return pmdaTreePMID(tree, name, pmid);
}

static int
proc_name(pmID pmid, char ***nameset, pmdaExt *pmda)
{
    __pmnsTree *tree = proc_dynamic_lookup_pmid(pmda, pmid);
    if (tree == NULL)
	return PM_ERR_PMID;
    return pmdaTreeName(tree, pmid, nameset);
}

static int
proc_children(const char *name, int flag, char ***kids, int **sts, pmdaExt *pmda)
{
    __pmnsTree *tree = proc_dynamic_lookup_name(pmda, name);
    if (tree == NULL)
	return PM_ERR_NAME;
    return pmdaTreeChildren(tree, name, flag, kids, sts);
}

int
proc_metrictable_size(void)
{
    return sizeof(proc_metrictab)/sizeof(proc_metrictab[0]);
}

/*
 * Initialise the agent (both daemon and DSO).
 */

void 
proc_init(pmdaInterface *dp)
{
    _pm_system_pagesize = getpagesize();
    if (_isDSO) {
	char helppath[MAXPATHLEN];
	int sep = __pmPathSeparator();
	snprintf(helppath, sizeof(helppath), "%s%c" "proc" "%c" "help",
		pmGetConfig("PCP_PMDAS_DIR"), sep, sep);
	pmdaDSO(dp, PMDA_INTERFACE_4, "proc DSO", helppath);
    }

    if (dp->status != 0)
	return;

    dp->version.four.instance = proc_instance;
    dp->version.four.store = proc_store;
    dp->version.four.fetch = proc_fetch;
    dp->version.four.text = proc_text;
    dp->version.four.pmid = proc_pmid;
    dp->version.four.name = proc_name;
    dp->version.four.children = proc_children;
    pmdaSetFetchCallBack(dp, proc_fetchCallBack);

    /*
     * Initialize the instance domain table.
     */
    memset(indomtab, 0, sizeof(indomtab));
    indomtab[CPU_INDOM].it_indom = CPU_INDOM;
    indomtab[PROC_INDOM].it_indom = PROC_INDOM;
    indomtab[CGROUP_SUBSYS_INDOM].it_indom = CGROUP_SUBSYS_INDOM;
    indomtab[CGROUP_MOUNTS_INDOM].it_indom = CGROUP_MOUNTS_INDOM;

    proc_pid.indom = &indomtab[PROC_INDOM];
 
    /* 
     * Read System.map and /proc/ksyms. Used to translate wait channel
     * addresses to symbol names. 
     * Added by Mike Mason <mmlnx@us.ibm.com>
     */
    read_ksym_sources(kernel_uname.release);

    cgroup_init();

    pmdaInit(dp, indomtab, sizeof(indomtab)/sizeof(indomtab[0]), proc_metrictab,
             sizeof(proc_metrictab)/sizeof(proc_metrictab[0]));

    /* cgroup metrics use the pmdaCache API for indom indexing */
    pmdaCacheOp(INDOM(CPU_INDOM), PMDA_CACHE_CULL);
    pmdaCacheOp(INDOM(CGROUP_SUBSYS_INDOM), PMDA_CACHE_CULL);
    pmdaCacheOp(INDOM(CGROUP_MOUNTS_INDOM), PMDA_CACHE_CULL);
}


static void
usage(void)
{
    fprintf(stderr, "Usage: %s [options]\n\n", pmProgname);
    fputs("Options:\n"
	  "  -d domain   use domain (numeric) for metrics domain of PMDA\n"
	  "  -l logfile  write log into logfile rather than using default log name\n"
	  "  -U username account to run under (default is root, for proc.io metrics)\n",
	  stderr);		
    exit(1);
}

/*
 * Set up the agent if running as a daemon.
 */

int
main(int argc, char **argv)
{
    int			sep = __pmPathSeparator();
    int			err = 0;
    int			c;
    pmdaInterface	dispatch;
    char		helppath[MAXPATHLEN];
    char		*username = "root";	/* proc.io.* require root! */

    _isDSO = 0;
    __pmSetProgname(argv[0]);

    snprintf(helppath, sizeof(helppath), "%s%c" "proc" "%c" "help",
		pmGetConfig("PCP_PMDAS_DIR"), sep, sep);
    pmdaDaemon(&dispatch, PMDA_INTERFACE_4, pmProgname, PROC, "proc.log", helppath);

    while ((c = pmdaGetOpt(argc, argv, "D:d:l:U:?", &dispatch, &err)) != EOF) {
	switch (c) {
	case 'U':
	    username = optarg;
	    break;
	default:
    	    err++;
	}
    }

    if (err)
    	usage();

    pmdaOpenLog(&dispatch);
    __pmSetProcessIdentity(username);

    proc_init(&dispatch);
    pmdaConnect(&dispatch);
    pmdaMain(&dispatch);

    exit(0);
}
