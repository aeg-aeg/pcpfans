/*
 * Linux PMDA
 *
 * Copyright (c) 2000,2004,2007-2008 Silicon Graphics, Inc.  All Rights Reserved.
 * Portions Copyright (c) 2002 International Business Machines Corp.
 * Portions Copyright (c) 2007-2011 Aconex.  All Rights Reserved.
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
#include <sys/vfs.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <utmp.h>
#include <pwd.h>
#include <grp.h>

#include "convert.h"
#include "clusters.h"
#include "indom.h"

#include "proc_cpuinfo.h"
#include "proc_stat.h"
#include "proc_meminfo.h"
#include "proc_loadavg.h"
#include "proc_net_dev.h"
#include "filesys.h"
#include "swapdev.h"
#include "getinfo.h"
#include "proc_net_rpc.h"
#include "proc_net_sockstat.h"
#include "proc_net_tcp.h"
#include "proc_pid.h"
#include "proc_partitions.h"
#include "proc_runq.h"
#include "proc_net_snmp.h"
#include "proc_scsi.h"
#include "proc_fs_xfs.h"
#include "proc_slabinfo.h"
#include "proc_uptime.h"
#include "sem_limits.h"
#include "msg_limits.h"
#include "shm_limits.h"
#include "ksym.h"
#include "proc_sys_fs.h"
#include "proc_vmstat.h"
#include "sysfs_kernel.h"
#include "linux_table.h"
#include "numa_meminfo.h"
#include "interrupts.h"
#include "cgroups.h"

/*
 * Legacy value from deprecated infiniband.h, preserved for backward
 * compatibility in linux_store() below.
 */

#define IB_COUNTERS_ALL  20

static proc_stat_t		proc_stat;
static proc_meminfo_t		proc_meminfo;
static proc_loadavg_t		proc_loadavg;
static proc_net_rpc_t		proc_net_rpc;
static proc_net_tcp_t		proc_net_tcp;
static proc_net_sockstat_t	proc_net_sockstat;
static proc_pid_t		proc_pid;
static struct utsname		kernel_uname;
static char 			uname_string[sizeof(kernel_uname)];
static proc_runq_t		proc_runq;
static proc_net_snmp_t		proc_net_snmp;
static proc_scsi_t		proc_scsi;
static proc_fs_xfs_t		proc_fs_xfs;
static proc_cpuinfo_t		proc_cpuinfo;
static proc_slabinfo_t		proc_slabinfo;
static sem_limits_t		sem_limits;
static msg_limits_t		msg_limits;
static shm_limits_t		shm_limits;
static proc_uptime_t		proc_uptime;
static proc_sys_fs_t		proc_sys_fs;
static proc_vmstat_t		proc_vmstat;
static sysfs_kernel_t		sysfs_kernel;
static numa_meminfo_t		numa_meminfo;

static int		_isDSO = 1;	/* =0 I am a daemon */

/* globals */
size_t _pm_system_pagesize; /* for hinv.pagesize and used elsewhere */
int _pm_have_proc_vmstat; /* if /proc/vmstat is available */
int _pm_intr_size; /* size in bytes of interrupt sum count metric */
int _pm_ctxt_size; /* size in bytes of context switch count metric */
int _pm_cputime_size; /* size in bytes of most of the cputime metrics */
int _pm_idletime_size; /* size in bytes of the idle cputime metric */

/*
 * Metric Instance Domains (statically initialized ones only)
 */
static pmdaInstid loadavg_indom_id[] = {
    { 1, "1 minute" }, { 5, "5 minute" }, { 15, "15 minute" }
};

static pmdaInstid nfs_indom_id[] = {
	{ 0, "null" },
	{ 1, "getattr" },
	{ 2, "setattr" },
	{ 3, "root" },
	{ 4, "lookup" },
	{ 5, "readlink" },
	{ 6, "read" },
	{ 7, "wrcache" },
	{ 8, "write" },
	{ 9, "create" },
	{ 10, "remove" },
	{ 11, "rename" },
	{ 12, "link" },
	{ 13, "symlink" },
	{ 14, "mkdir" },
	{ 15, "rmdir" },
	{ 16, "readdir" },
	{ 17, "statfs" }
};

static pmdaInstid nfs3_indom_id[] = {
	{ 0, "null" },
	{ 1, "getattr" },
	{ 2, "setattr" },
	{ 3, "lookup" },
	{ 4, "access" },
	{ 5, "readlink" },
	{ 6, "read" },
	{ 7, "write" },
	{ 8, "create" },
	{ 9, "mkdir" },
	{ 10, "symlink" },
	{ 11, "mknod" },
	{ 12, "remove" },
	{ 13, "rmdir" },
	{ 14, "rename" },
	{ 15, "link" },
	{ 16, "readdir" },
	{ 17, "readdir+" },
	{ 18, "statfs" },
	{ 19, "fsinfo" },
	{ 20, "pathconf" },
	{ 21, "commit" }
};

static pmdaInstid nfs4_cli_indom_id[] = {
	{ 0,  "null" },
	{ 1,  "read" },
	{ 2,  "write" },
	{ 3,  "commit" },
	{ 4,  "open" },
	{ 5,  "open_conf" },
	{ 6,  "open_noat" },
	{ 7,  "open_dgrd" },
	{ 8,  "close" },
	{ 9,  "setattr" },
	{ 10, "fsinfo" },
	{ 11, "renew" },
	{ 12, "setclntid" },
	{ 13, "confirm" },
	{ 14, "lock" },
	{ 15, "lockt" },
	{ 16, "locku" },
	{ 17, "access" },
	{ 18, "getattr" },
	{ 19, "lookup" },
	{ 20, "lookup_root" },
	{ 21, "remove" },
	{ 22, "rename" },
	{ 23, "link" },
	{ 24, "symlink" },
	{ 25, "create" },
	{ 26, "pathconf" },
	{ 27, "statfs" },
	{ 28, "readlink" },
	{ 29, "readdir" },
	{ 30, "server_caps" },
	{ 31, "delegreturn" },
	{ 32, "getacl" },
	{ 33, "setacl" },
	{ 34, "fs_locatns" },
};

static pmdaInstid nfs4_svr_indom_id[] = {
	{ 0,  "null" },
	{ 1,  "op0-unused" },
	{ 2,  "op1-unused"},
	{ 3,  "minorversion"},	/* future use */
	{ 4,  "access" },
	{ 5,  "close" },
	{ 6,  "commit" },
	{ 7,  "create" },
	{ 8,  "delegpurge" },
	{ 9,  "delegreturn" },
	{ 10, "getattr" },
	{ 11, "getfh" },
	{ 12, "link" },
	{ 13, "lock" },
	{ 14, "lockt" },
	{ 15, "locku" },
	{ 16, "lookup" },
	{ 17, "lookup_root" },
	{ 18, "nverify" },
	{ 19, "open" },
	{ 20, "openattr" },
	{ 21, "open_conf" },
	{ 22, "open_dgrd" },
	{ 23, "putfh" },
	{ 24, "putpubfh" },
	{ 25, "putrootfh" },
	{ 26, "read" },
	{ 27, "readdir" },
	{ 28, "readlink" },
	{ 29, "remove" },
	{ 30, "rename" },
	{ 31, "renew" },
	{ 32, "restorefh" },
	{ 33, "savefh" },
	{ 34, "secinfo" },
	{ 35, "setattr" },
	{ 36, "setcltid" },
	{ 37, "setcltidconf" },
	{ 38, "verify" },
	{ 39, "write" },
	{ 40, "rellockowner" },
};

pmdaIndom indomtab[] = {
    { CPU_INDOM, 0, NULL },
    { DISK_INDOM, 0, NULL }, /* cached */
    { LOADAVG_INDOM, 3, loadavg_indom_id },
    { NET_DEV_INDOM, 0, NULL },
    { PROC_INTERRUPTS_INDOM, 0, NULL },	/* deprecated */
    { FILESYS_INDOM, 0, NULL },
    { SWAPDEV_INDOM, 0, NULL },
    { NFS_INDOM, NR_RPC_COUNTERS, nfs_indom_id },
    { NFS3_INDOM, NR_RPC3_COUNTERS, nfs3_indom_id },
    { PROC_INDOM, 0, NULL },
    { PARTITIONS_INDOM, 0, NULL }, /* cached */
    { SCSI_INDOM, 0, NULL },
    { SLAB_INDOM, 0, NULL },
    { IB_INDOM, 0, NULL }, /* deprecated */
    { NFS4_CLI_INDOM, NR_RPC4_CLI_COUNTERS, nfs4_cli_indom_id },
    { NFS4_SVR_INDOM, NR_RPC4_SVR_COUNTERS, nfs4_svr_indom_id },
    { QUOTA_PRJ_INDOM, 0, NULL },
    { NET_INET_INDOM, 0, NULL },
    { TMPFS_INDOM, 0, NULL },
    { NODE_INDOM, 0, NULL },
    { CGROUP_SUBSYS_INDOM, 0, NULL },
    { CGROUP_MOUNTS_INDOM, 0, NULL },
};


/*
 * all metrics supported in this PMDA - one table entry for each
 */

pmdaMetric linux_metrictab[] = {

/*
 * /proc/stat cluster
 */

/* kernel.percpu.cpu.user */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,0), KERNEL_UTYPE, CPU_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.percpu.cpu.nice */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,1), KERNEL_UTYPE, CPU_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.percpu.cpu.sys */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,2), KERNEL_UTYPE, CPU_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.percpu.cpu.idle */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,3), KERNEL_UTYPE, CPU_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.percpu.cpu.wait.total */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,30), KERNEL_UTYPE, CPU_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.percpu.cpu.intr */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,31), KERNEL_UTYPE, CPU_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.percpu.cpu.irq.soft */
    { NULL,
      { PMDA_PMID(CLUSTER_STAT,56), KERNEL_UTYPE, CPU_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.percpu.cpu.irq.hard */
    { NULL,
      { PMDA_PMID(CLUSTER_STAT,57), KERNEL_UTYPE, CPU_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.percpu.cpu.steal */
    { NULL,
      { PMDA_PMID(CLUSTER_STAT,58), KERNEL_UTYPE, CPU_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.percpu.cpu.guest */
    { NULL,
      { PMDA_PMID(CLUSTER_STAT,61), KERNEL_UTYPE, CPU_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.pernode.cpu.user */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,62), KERNEL_UTYPE, NODE_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.pernode.cpu.nice */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,63), KERNEL_UTYPE, NODE_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.pernode.cpu.sys */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,64), KERNEL_UTYPE, NODE_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.pernode.cpu.idle */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,65), KERNEL_UTYPE, NODE_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.pernode.cpu.wait.total */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,69), KERNEL_UTYPE, NODE_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.pernode.cpu.intr */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,66), KERNEL_UTYPE, NODE_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.pernode.cpu.irq.soft */
    { NULL,
      { PMDA_PMID(CLUSTER_STAT,70), KERNEL_UTYPE, NODE_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.pernode.cpu.irq.hard */
    { NULL,
      { PMDA_PMID(CLUSTER_STAT,71), KERNEL_UTYPE, NODE_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.pernode.cpu.steal */
    { NULL,
      { PMDA_PMID(CLUSTER_STAT,67), KERNEL_UTYPE, NODE_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.pernode.cpu.guest */
    { NULL,
      { PMDA_PMID(CLUSTER_STAT,68), KERNEL_UTYPE, NODE_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* disk.dev.read */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,4), KERNEL_ULONG, DISK_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* disk.dev.write */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,5), KERNEL_ULONG, DISK_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* disk.dev.blkread */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,6), PM_TYPE_U64, DISK_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* disk.dev.blkwrite */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,7), PM_TYPE_U64, DISK_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* disk.dev.avactive */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,46), PM_TYPE_U32, DISK_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* disk.dev.aveq */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,47), PM_TYPE_U32, DISK_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* disk.dev.read_merge */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,49), KERNEL_ULONG, DISK_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* disk.dev.write_merge */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,50), KERNEL_ULONG, DISK_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* disk.dev.scheduler */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,59), PM_TYPE_STRING, DISK_INDOM,
	PM_SEM_INSTANT, PMDA_PMUNITS(0,0,0,0,0,0) }, },

/* disk.all.avactive */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,44), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* disk.all.aveq */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,45), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* disk.all.read_merge */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,51), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* disk.all.write_merge */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,52), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* swap.pagesin */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,8), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* swap.pagesout */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,9), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* swap.in */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,10), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* swap.out */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,11), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* kernel.all.intr */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,12), KERNEL_UTYPE, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* kernel.all.pswitch */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,13), KERNEL_UTYPE, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* kernel.all.sysfork */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,14), KERNEL_ULONG, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* kernel.all.cpu.user */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,20), KERNEL_UTYPE, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.all.cpu.nice */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,21), KERNEL_UTYPE, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.all.cpu.sys */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,22), KERNEL_UTYPE, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.all.cpu.idle */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,23), KERNEL_UTYPE, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.all.cpu.intr */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,34), KERNEL_UTYPE, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.all.cpu.wait.total */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,35), KERNEL_UTYPE, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.all.cpu.irq.soft */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,53), KERNEL_UTYPE, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.all.cpu.irq.hard */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,54), KERNEL_UTYPE, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.all.cpu.steal */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,55), KERNEL_UTYPE, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* kernel.all.cpu.guest */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,60), KERNEL_UTYPE, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,1,0,0,PM_TIME_MSEC,0) }, },

/* disk.all.read */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,24), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* disk.all.write */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,25), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* disk.all.blkread */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,26), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* disk.all.blkwrite */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,27), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* disk.dev.total */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,28), PM_TYPE_U64, DISK_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* disk.dev.blktotal */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,36), PM_TYPE_U64, DISK_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* disk.all.total */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,29), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* disk.all.blktotal */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,37), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* hinv.ncpu */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,32), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE, 
      PMDA_PMUNITS(0,0,0,0,0,0) }, },

/* hinv.ndisk */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,33), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE, 
      PMDA_PMUNITS(0,0,0,0,0,0) }, },

/* kernel.all.hz */
    { NULL,
      { PMDA_PMID(CLUSTER_STAT,48), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE, 
      PMDA_PMUNITS(0,-1,1,0,PM_TIME_SEC,PM_COUNT_ONE) }, },

/*
 * /proc/uptime cluster
 * Uptime modified and idletime added by Mike Mason <mmlnx@us.ibm.com>
 */

/* kernel.all.uptime */
    { NULL,
      { PMDA_PMID(CLUSTER_UPTIME,0), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(0,1,0,0,PM_TIME_SEC,0) }, },

/* kernel.all.idletime */
    { NULL,
      { PMDA_PMID(CLUSTER_UPTIME,1), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(0,1,0,0,PM_TIME_SEC,0) }, },

/*
 * /proc/meminfo cluster
 */

/* mem.physmem */
    { NULL, 
      { PMDA_PMID(CLUSTER_MEMINFO,0), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_DISCRETE, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.used */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,1), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.free */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,2), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.shared */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,3), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.bufmem */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,4), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.cached */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,5), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.active */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,14), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.inactive */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,15), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.swapCached */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,13), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.highTotal */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,16), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.highFree */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,17), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.lowTotal */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,18), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.lowFree */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,19), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.swapTotal */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,20), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.swapFree */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,21), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.dirty */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,22), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.writeback */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,23), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.mapped */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,24), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.slab */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,25), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.committed_AS */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,26), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.pageTables */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,27), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.reverseMaps */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,28), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.cache_clean */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,29), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.anonpages */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,30), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.commitLimit */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,31), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.bounce */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,32), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.NFS_Unstable */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,33), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.slabReclaimable */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,34), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.slabUnreclaimable */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,35), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.active_anon */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,36), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.inactive_anon */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,37), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.active_file */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,38), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.inactive_file */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,39), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.unevictable */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,40), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.mlocked */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,41), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.shmem */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,42), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.kernelStack */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,43), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.hugepagesTotal */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,44), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* mem.util.hugepagesFree */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,45), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* mem.util.hugepagesRsvd */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,46), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* mem.util.hugepagesSurp */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,47), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* mem.util.directMap4k */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,48), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.directMap2M */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,49), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.vmallocTotal */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,50), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.vmallocUsed */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,51), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.vmallocChunk */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,52), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.mmap_copy */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,53), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.quicklists */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,54), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.corrupthardware */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,55), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.util.mmap_copy */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,56), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.total */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,0), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.free */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,1), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.used */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,2), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.active */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,3), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.inactive */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,4), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.active_anon */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,5), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.inactive_anon */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,6), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.active_file */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,7), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.inactive_file */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,8), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.highTotal */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,9), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.highFree */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,10), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.lowTotal */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,11), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.lowFree */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,12), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.unevictable */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,13), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.mlocked */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,14), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.dirty */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,15), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.writeback */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,16), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.filePages */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,17), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.mapped */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,18), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.anonpages */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,19), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.shmem */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,20), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.kernelStack */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,21), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.pageTables */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,22), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.NFS_Unstable */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,23), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.bounce */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,24), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.writebackTmp */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,25), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.slab */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,26), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.slabReclaimable */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,27), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.slabUnreclaimable */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,28), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* mem.numa.util.hugepagesTotal */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,29), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* mem.numa.util.hugepagesFree */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,30), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* mem.numa.util.hugepagesSurp */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,31), PM_TYPE_U64, NODE_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* mem.numa.alloc.hit */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,32), PM_TYPE_U64, NODE_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* mem.numa.alloc.miss */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,33), PM_TYPE_U64, NODE_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* mem.numa.alloc.foreign */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,34), PM_TYPE_U64, NODE_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* mem.numa.alloc.interleave_hit */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,35), PM_TYPE_U64, NODE_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* mem.numa.alloc.local_node */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,36), PM_TYPE_U64, NODE_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* mem.numa.alloc.other_node */
    { NULL,
      { PMDA_PMID(CLUSTER_NUMA_MEMINFO,37), PM_TYPE_U64, NODE_INDOM, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },


/* swap.length */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,6), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0) }, },

/* swap.used */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,7), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0) }, },

/* swap.free */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,8), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0) }, },

/* hinv.physmem */
    { NULL, 
      { PMDA_PMID(CLUSTER_MEMINFO,9), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
      PMDA_PMUNITS(1,0,0,PM_SPACE_MBYTE,0,0) }, },

/* mem.freemem */
    { NULL, 
      { PMDA_PMID(CLUSTER_MEMINFO,10), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* hinv.pagesize */
    { NULL, 
      { PMDA_PMID(CLUSTER_MEMINFO,11), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0) }, },

/* mem.util.other */
    { NULL,
      { PMDA_PMID(CLUSTER_MEMINFO,12), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/*
 * /proc/slabinfo cluster
 */

    /* mem.slabinfo.objects.active */
    { NULL,
      { PMDA_PMID(CLUSTER_SLAB,0), PM_TYPE_U64, SLAB_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,0,0,0,0) } },

    /* mem.slabinfo.objects.total */
    { NULL,
      { PMDA_PMID(CLUSTER_SLAB,1), PM_TYPE_U64, SLAB_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,0,0,0,0) } },

    /* mem.slabinfo.objects.size */
    { NULL,
      { PMDA_PMID(CLUSTER_SLAB,2), PM_TYPE_U32, SLAB_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0) }, },

    /* mem.slabinfo.slabs.active */
    { NULL,
      { PMDA_PMID(CLUSTER_SLAB,3), PM_TYPE_U32, SLAB_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,0,0,0,0) } },

    /* mem.slabinfo.slabs.total */
    { NULL,
      { PMDA_PMID(CLUSTER_SLAB,4), PM_TYPE_U32, SLAB_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,0,0,0,0) } },

    /* mem.slabinfo.slabs.pages_per_slab */
    { NULL,
      { PMDA_PMID(CLUSTER_SLAB,5), PM_TYPE_U32, SLAB_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,0,0,0,0) } },

    /* mem.slabinfo.slabs.objects_per_slab */
    { NULL,
      { PMDA_PMID(CLUSTER_SLAB,6), PM_TYPE_U32, SLAB_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,0,0,0,0) } },

    /* mem.slabinfo.slabs.total_size */
    { NULL,
      { PMDA_PMID(CLUSTER_SLAB,7), PM_TYPE_U64, SLAB_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0) }, },

/*
 * /proc/loadavg cluster
 */

    /* kernel.all.load */
    { NULL,
      { PMDA_PMID(CLUSTER_LOADAVG,0), PM_TYPE_FLOAT, LOADAVG_INDOM, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,0,0,0,0) } },

    /* kernel.all.lastpid -- added by Mike Mason <mmlnx@us.ibm.com> */
    { NULL,
      { PMDA_PMID(CLUSTER_LOADAVG, 1), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,0,0,0,0) } },

    /* kernel.all.runnable */
    { NULL,
      { PMDA_PMID(CLUSTER_LOADAVG, 2), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,0,0,0,0) } },

    /* kernel.all.nprocs */
    { NULL,
      { PMDA_PMID(CLUSTER_LOADAVG, 3), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,0,0,0,0) } },

/*
 * /proc/net/dev cluster
 */

/* network.interface.in.bytes */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,0), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0) }, },

/* network.interface.in.packets */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,1), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* network.interface.in.errors */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,2), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* network.interface.in.drops */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,3), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* network.interface.in.fifo */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,4), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* network.interface.in.frame */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,5), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* network.interface.in.compressed */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,6), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* network.interface.in.mcasts */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,7), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* network.interface.out.bytes */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,8), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0) }, },

/* network.interface.out.packets */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,9), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* network.interface.out.errors */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,10), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* network.interface.out.drops */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,11), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* network.interface.out.fifo */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,12), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* network.interface.collisions */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,13), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* network.interface.out.carrier */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,14), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* network.interface.out.compressed */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,15), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* network.interface.total.bytes */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,16), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(1,0,0,0,PM_SPACE_BYTE,0) }, },

/* network.interface.total.packets */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,17), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* network.interface.total.errors */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,18), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* network.interface.total.drops */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,19), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* network.interface.total.mcasts */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,20), PM_TYPE_U64, NET_DEV_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* network.interface.mtu */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,21), PM_TYPE_U32, NET_DEV_INDOM, PM_SEM_INSTANT, 
      PMDA_PMUNITS(1,0,0,0,PM_SPACE_BYTE,0) }, },

/* network.interface.speed */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,22), PM_TYPE_FLOAT, NET_DEV_INDOM, PM_SEM_DISCRETE, 
      PMDA_PMUNITS(1,-1,0,PM_SPACE_MBYTE,PM_TIME_SEC,0) }, },

/* network.interface.baudrate */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,23), PM_TYPE_U32, NET_DEV_INDOM, PM_SEM_DISCRETE, 
      PMDA_PMUNITS(1,-1,0,PM_SPACE_BYTE,PM_TIME_SEC,0) }, },

/* network.interface.duplex */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,24), PM_TYPE_U32, NET_DEV_INDOM, PM_SEM_DISCRETE, 
      PMDA_PMUNITS(0,0,0,0,0,0) }, },

/* network.interface.up */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,25), PM_TYPE_U32, NET_DEV_INDOM, PM_SEM_INSTANT, 
      PMDA_PMUNITS(0,0,0,0,0,0) }, },

/* network.interface.running */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_DEV,26), PM_TYPE_U32, NET_DEV_INDOM, PM_SEM_INSTANT, 
      PMDA_PMUNITS(0,0,0,0,0,0) }, },

/* network.interface.ipaddr */
    { NULL, 
      { PMDA_PMID(CLUSTER_NET_INET,0), PM_TYPE_STRING, NET_INET_INDOM, PM_SEM_INSTANT, 
      PMDA_PMUNITS(0,0,0,0,0,0) }, },

/*
 * filesys cluster
 */

/* hinv.nmounts */
  { NULL,
    { PMDA_PMID(CLUSTER_FILESYS,0), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* filesys.capacity */
  { NULL,
    { PMDA_PMID(CLUSTER_FILESYS,1), PM_TYPE_U64, FILESYS_INDOM, PM_SEM_DISCRETE,
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* filesys.used */
  { NULL,
    { PMDA_PMID(CLUSTER_FILESYS,2), PM_TYPE_U64, FILESYS_INDOM, PM_SEM_INSTANT,
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* filesys.free */
  { NULL,
     { PMDA_PMID(CLUSTER_FILESYS,3), PM_TYPE_U64, FILESYS_INDOM, PM_SEM_INSTANT,
     PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* filesys.maxfiles */
  { NULL,
     { PMDA_PMID(CLUSTER_FILESYS,4), PM_TYPE_U32, FILESYS_INDOM, PM_SEM_DISCRETE,
     PMDA_PMUNITS(0,0,0,0,0,0) } },

/* filesys.usedfiles */
  { NULL,
     { PMDA_PMID(CLUSTER_FILESYS,5), PM_TYPE_U32, FILESYS_INDOM, PM_SEM_INSTANT,
     PMDA_PMUNITS(0,0,0,0,0,0) } },

/* filesys.freefiles */
  { NULL,
     { PMDA_PMID(CLUSTER_FILESYS,6), PM_TYPE_U32, FILESYS_INDOM, PM_SEM_INSTANT,
     PMDA_PMUNITS(0,0,0,0,0,0) } },

/* filesys.mountdir */
  { NULL,
     { PMDA_PMID(CLUSTER_FILESYS,7), PM_TYPE_STRING, FILESYS_INDOM, PM_SEM_DISCRETE,
     PMDA_PMUNITS(0,0,0,0,0,0) } },

/* filesys.full */
  { NULL,
     { PMDA_PMID(CLUSTER_FILESYS,8), PM_TYPE_DOUBLE, FILESYS_INDOM, PM_SEM_INSTANT,
     PMDA_PMUNITS(0,0,0,0,0,0) } },

/* filesys.blocksize */
  { NULL,
    { PMDA_PMID(CLUSTER_FILESYS,9), PM_TYPE_U32, FILESYS_INDOM, PM_SEM_INSTANT,
    PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0) } },

/* filesys.avail */
  { NULL,
    { PMDA_PMID(CLUSTER_FILESYS,10), PM_TYPE_U64, FILESYS_INDOM, PM_SEM_INSTANT,
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* filesys.readonly */
  { NULL,
    { PMDA_PMID(CLUSTER_FILESYS,11), PM_TYPE_U32, FILESYS_INDOM, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/*
 * tmpfs filesystem cluster
 */

/* tmpfs.capacity */
  { NULL,
    { PMDA_PMID(CLUSTER_TMPFS,1), PM_TYPE_U64, TMPFS_INDOM, PM_SEM_DISCRETE,
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* tmpfs.used */
  { NULL,
    { PMDA_PMID(CLUSTER_TMPFS,2), PM_TYPE_U64, TMPFS_INDOM, PM_SEM_INSTANT,
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* tmpfs.free */
  { NULL,
     { PMDA_PMID(CLUSTER_TMPFS,3), PM_TYPE_U64, TMPFS_INDOM, PM_SEM_INSTANT,
     PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* tmpfs.maxfiles */
  { NULL,
     { PMDA_PMID(CLUSTER_TMPFS,4), PM_TYPE_U32, TMPFS_INDOM, PM_SEM_DISCRETE,
     PMDA_PMUNITS(0,0,0,0,0,0) } },

/* tmpfs.usedfiles */
  { NULL,
     { PMDA_PMID(CLUSTER_TMPFS,5), PM_TYPE_U32, TMPFS_INDOM, PM_SEM_INSTANT,
     PMDA_PMUNITS(0,0,0,0,0,0) } },

/* tmpfs.freefiles */
  { NULL,
     { PMDA_PMID(CLUSTER_TMPFS,6), PM_TYPE_U32, TMPFS_INDOM, PM_SEM_INSTANT,
     PMDA_PMUNITS(0,0,0,0,0,0) } },

/* tmpfs.full */
  { NULL,
     { PMDA_PMID(CLUSTER_TMPFS,7), PM_TYPE_DOUBLE, TMPFS_INDOM, PM_SEM_INSTANT,
     PMDA_PMUNITS(0,0,0,0,0,0) } },

/*
 * swapdev cluster
 */

/* swapdev.free */
  { NULL,
    { PMDA_PMID(CLUSTER_SWAPDEV,0), PM_TYPE_U32, SWAPDEV_INDOM, PM_SEM_INSTANT,
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* swapdev.length */
  { NULL,
    { PMDA_PMID(CLUSTER_SWAPDEV,1), PM_TYPE_U32, SWAPDEV_INDOM, PM_SEM_DISCRETE,
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* swapdev.maxswap */
  { NULL,
    { PMDA_PMID(CLUSTER_SWAPDEV,2), PM_TYPE_U32, SWAPDEV_INDOM, PM_SEM_DISCRETE,
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* swapdev.vlength */
  { NULL,
    { PMDA_PMID(CLUSTER_SWAPDEV,3), PM_TYPE_U32, SWAPDEV_INDOM, PM_SEM_DISCRETE,
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) } },

/* swapdev.priority */
  { NULL,
    { PMDA_PMID(CLUSTER_SWAPDEV,4), PM_TYPE_32, SWAPDEV_INDOM, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/*
 * socket stat cluster
 */

/* network.sockstat.tcp.inuse */
  { &proc_net_sockstat.tcp[_PM_SOCKSTAT_INUSE],
    { PMDA_PMID(CLUSTER_NET_SOCKSTAT,0), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.sockstat.tcp.highest */
  { &proc_net_sockstat.tcp[_PM_SOCKSTAT_HIGHEST],
    { PMDA_PMID(CLUSTER_NET_SOCKSTAT,1), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.sockstat.tcp.util */
  { &proc_net_sockstat.tcp[_PM_SOCKSTAT_UTIL],
    { PMDA_PMID(CLUSTER_NET_SOCKSTAT,2), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* network.sockstat.udp.inuse */
  { &proc_net_sockstat.udp[_PM_SOCKSTAT_INUSE],
    { PMDA_PMID(CLUSTER_NET_SOCKSTAT,3), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.sockstat.udp.highest */
  { &proc_net_sockstat.udp[_PM_SOCKSTAT_HIGHEST],
    { PMDA_PMID(CLUSTER_NET_SOCKSTAT,4), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.sockstat.udp.util */
  { &proc_net_sockstat.udp[_PM_SOCKSTAT_UTIL],
    { PMDA_PMID(CLUSTER_NET_SOCKSTAT,5), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* network.sockstat.raw.inuse */
  { &proc_net_sockstat.raw[_PM_SOCKSTAT_INUSE],
    { PMDA_PMID(CLUSTER_NET_SOCKSTAT,6), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.sockstat.raw.highest */
  { &proc_net_sockstat.raw[_PM_SOCKSTAT_HIGHEST],
    { PMDA_PMID(CLUSTER_NET_SOCKSTAT,7), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.sockstat.raw.util */
  { &proc_net_sockstat.raw[_PM_SOCKSTAT_UTIL],
    { PMDA_PMID(CLUSTER_NET_SOCKSTAT,8), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/*
 * nfs cluster
 */

/* nfs.client.calls */
  { NULL,
    { PMDA_PMID(CLUSTER_NET_NFS,1), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* nfs.client.reqs */
  { NULL,
    { PMDA_PMID(CLUSTER_NET_NFS,4), PM_TYPE_U32, NFS_INDOM, PM_SEM_COUNTER, 
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* nfs.server.calls */
  { NULL,
    { PMDA_PMID(CLUSTER_NET_NFS,50), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* nfs.server.reqs */
  { NULL,
    { PMDA_PMID(CLUSTER_NET_NFS,12), PM_TYPE_U32, NFS_INDOM, PM_SEM_COUNTER, 
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* nfs3.client.calls */
  { NULL,
    { PMDA_PMID(CLUSTER_NET_NFS,60), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* nfs3.client.reqs */
  { NULL,
    { PMDA_PMID(CLUSTER_NET_NFS,61), PM_TYPE_U32, NFS3_INDOM, PM_SEM_COUNTER, 
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* nfs3.server.calls */
  { NULL,
    { PMDA_PMID(CLUSTER_NET_NFS,62), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* nfs3.server.reqs */
  { NULL,
    { PMDA_PMID(CLUSTER_NET_NFS,63), PM_TYPE_U32, NFS3_INDOM, PM_SEM_COUNTER, 
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* nfs4.client.calls */
  { NULL,
    { PMDA_PMID(CLUSTER_NET_NFS,64), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* nfs4.client.reqs */
  { NULL,
    { PMDA_PMID(CLUSTER_NET_NFS,65), PM_TYPE_U32, NFS4_CLI_INDOM, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* nfs4.server.calls */
  { NULL,
    { PMDA_PMID(CLUSTER_NET_NFS,66), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* nfs4.server.reqs */
  { NULL,
    { PMDA_PMID(CLUSTER_NET_NFS,67), PM_TYPE_U32, NFS4_SVR_INDOM, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.client.rpccnt */
  { &proc_net_rpc.client.rpccnt,
    { PMDA_PMID(CLUSTER_NET_NFS,20), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.client.rpcretrans */
  { &proc_net_rpc.client.rpcretrans,
    { PMDA_PMID(CLUSTER_NET_NFS,21), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.client.rpcauthrefresh */
  { &proc_net_rpc.client.rpcauthrefresh,
    { PMDA_PMID(CLUSTER_NET_NFS,22), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.client.netcnt */
  { &proc_net_rpc.client.netcnt,
    { PMDA_PMID(CLUSTER_NET_NFS,24), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.client.netudpcnt */
  { &proc_net_rpc.client.netudpcnt,
    { PMDA_PMID(CLUSTER_NET_NFS,25), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.client.nettcpcnt */
  { &proc_net_rpc.client.nettcpcnt,
    { PMDA_PMID(CLUSTER_NET_NFS,26), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.client.nettcpconn */
  { &proc_net_rpc.client.nettcpconn,
    { PMDA_PMID(CLUSTER_NET_NFS,27), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.rpccnt */
  { &proc_net_rpc.server.rpccnt,
    { PMDA_PMID(CLUSTER_NET_NFS,30), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.rpcerr */
  { &proc_net_rpc.server.rpcerr,
    { PMDA_PMID(CLUSTER_NET_NFS,31), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.rpcbadfmt */
  { &proc_net_rpc.server.rpcbadfmt,
    { PMDA_PMID(CLUSTER_NET_NFS,32), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.rpcbadauth */
  { &proc_net_rpc.server.rpcbadauth,
    { PMDA_PMID(CLUSTER_NET_NFS,33), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.rpcbadclnt */
  { &proc_net_rpc.server.rpcbadclnt,
    { PMDA_PMID(CLUSTER_NET_NFS,34), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.rchits */
  { &proc_net_rpc.server.rchits,
    { PMDA_PMID(CLUSTER_NET_NFS,35), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.rcmisses */
  { &proc_net_rpc.server.rcmisses,
    { PMDA_PMID(CLUSTER_NET_NFS,36), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.rcnocache */
  { &proc_net_rpc.server.rcnocache,
    { PMDA_PMID(CLUSTER_NET_NFS,37), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.fh_cached */
  { &proc_net_rpc.server.fh_cached,
    { PMDA_PMID(CLUSTER_NET_NFS,38), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.fh_valid */
  { &proc_net_rpc.server.fh_valid,
    { PMDA_PMID(CLUSTER_NET_NFS,39), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.fh_fixup */
  { &proc_net_rpc.server.fh_fixup,
    { PMDA_PMID(CLUSTER_NET_NFS,40), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.fh_lookup */
  { &proc_net_rpc.server.fh_lookup,
    { PMDA_PMID(CLUSTER_NET_NFS,41), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.fh_stale */
  { &proc_net_rpc.server.fh_stale,
    { PMDA_PMID(CLUSTER_NET_NFS,42), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.fh_concurrent */
  { &proc_net_rpc.server.fh_concurrent,
    { PMDA_PMID(CLUSTER_NET_NFS,43), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.netcnt */
  { &proc_net_rpc.server.netcnt,
    { PMDA_PMID(CLUSTER_NET_NFS,44), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.netudpcnt */
  { &proc_net_rpc.server.netudpcnt,
    { PMDA_PMID(CLUSTER_NET_NFS,45), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.nettcpcnt */
  { &proc_net_rpc.server.nettcpcnt,
    { PMDA_PMID(CLUSTER_NET_NFS,46), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.nettcpconn */
  { &proc_net_rpc.server.nettcpcnt,
    { PMDA_PMID(CLUSTER_NET_NFS,47), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.fh_anon */
  { &proc_net_rpc.server.fh_anon,
    { PMDA_PMID(CLUSTER_NET_NFS,51), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.fh_nocache_dir */
  { &proc_net_rpc.server.fh_nocache_dir,
    { PMDA_PMID(CLUSTER_NET_NFS,52), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.fh_nocache_nondir */
  { &proc_net_rpc.server.fh_nocache_nondir,
    { PMDA_PMID(CLUSTER_NET_NFS,53), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.io_read */
  { &proc_net_rpc.server.io_read,
    { PMDA_PMID(CLUSTER_NET_NFS,54), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0) } },

/* rpc.server.io_write */
  { &proc_net_rpc.server.io_write,
    { PMDA_PMID(CLUSTER_NET_NFS,55), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0) } },

/* rpc.server.th_cnt */
  { &proc_net_rpc.server.th_cnt,
    { PMDA_PMID(CLUSTER_NET_NFS,56), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* rpc.server.th_fullcnt */
  { &proc_net_rpc.server.th_fullcnt,
    { PMDA_PMID(CLUSTER_NET_NFS,57), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

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
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0)}},
/* proc.io.write_bytes */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_IO,5), PM_TYPE_U64, PROC_INDOM, PM_SEM_COUNTER,
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0)}},
/* proc.io.cancelled_write_bytes */
  { NULL,
    { PMDA_PMID(CLUSTER_PID_IO,6), PM_TYPE_U64, PROC_INDOM, PM_SEM_COUNTER,
    PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0)}},

/*
 * /proc/partitions cluster
 */

/* disk.partitions.read */
    { NULL, 
      { PMDA_PMID(CLUSTER_PARTITIONS,0), PM_TYPE_U32, PARTITIONS_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* disk.partitions.write */
    { NULL, 
      { PMDA_PMID(CLUSTER_PARTITIONS,1), PM_TYPE_U32, PARTITIONS_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* disk.partitions.total */
    { NULL, 
      { PMDA_PMID(CLUSTER_PARTITIONS,2), PM_TYPE_U32, PARTITIONS_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* disk.partitions.blkread */
    { NULL, 
      { PMDA_PMID(CLUSTER_PARTITIONS,3), PM_TYPE_U32, PARTITIONS_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* disk.partitions.blkwrite */
    { NULL, 
      { PMDA_PMID(CLUSTER_PARTITIONS,4), PM_TYPE_U32, PARTITIONS_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* disk.partitions.blktotal */
    { NULL, 
      { PMDA_PMID(CLUSTER_PARTITIONS,5), PM_TYPE_U32, PARTITIONS_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* disk.partitions.read_bytes */
    { NULL, 
      { PMDA_PMID(CLUSTER_PARTITIONS,6), PM_TYPE_U32, PARTITIONS_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* disk.partitions.write_bytes */
    { NULL, 
      { PMDA_PMID(CLUSTER_PARTITIONS,7), PM_TYPE_U32, PARTITIONS_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* disk.partitions.total_bytes */
    { NULL, 
      { PMDA_PMID(CLUSTER_PARTITIONS,8), PM_TYPE_U32, PARTITIONS_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* disk.dev.read_bytes */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,38), PM_TYPE_U32, DISK_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* disk.dev.write_bytes */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,39), PM_TYPE_U32, DISK_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* disk.dev.total_bytes */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,40), PM_TYPE_U32, DISK_INDOM, PM_SEM_COUNTER, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* disk.all.read_bytes */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,41), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* disk.all.write_bytes */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,42), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* disk.all.total_bytes */
    { NULL, 
      { PMDA_PMID(CLUSTER_STAT,43), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/*
 * kernel_uname cluster
 */

/* kernel.uname.release */
  { kernel_uname.release,
    { PMDA_PMID(CLUSTER_KERNEL_UNAME, 0), PM_TYPE_STRING, PM_INDOM_NULL, PM_SEM_INSTANT, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* kernel.uname.version */
  { kernel_uname.version,
    { PMDA_PMID(CLUSTER_KERNEL_UNAME, 1), PM_TYPE_STRING, PM_INDOM_NULL, PM_SEM_INSTANT, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* kernel.uname.sysname */
  { kernel_uname.sysname,
    { PMDA_PMID(CLUSTER_KERNEL_UNAME, 2), PM_TYPE_STRING, PM_INDOM_NULL, PM_SEM_INSTANT, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* kernel.uname.machine */
  { kernel_uname.machine,
    { PMDA_PMID(CLUSTER_KERNEL_UNAME, 3), PM_TYPE_STRING, PM_INDOM_NULL, PM_SEM_DISCRETE, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* kernel.uname.nodename */
  { kernel_uname.nodename,
    { PMDA_PMID(CLUSTER_KERNEL_UNAME, 4), PM_TYPE_STRING, PM_INDOM_NULL, PM_SEM_INSTANT, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* pmda.uname */
  { NULL,
    { PMDA_PMID(CLUSTER_KERNEL_UNAME, 5), PM_TYPE_STRING, PM_INDOM_NULL, PM_SEM_INSTANT, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* pmda.version */
  { NULL,
    { PMDA_PMID(CLUSTER_KERNEL_UNAME, 6), PM_TYPE_STRING, PM_INDOM_NULL, PM_SEM_INSTANT, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* kernel.uname.distro */
  { NULL,
    { PMDA_PMID(CLUSTER_KERNEL_UNAME, 7), PM_TYPE_STRING, PM_INDOM_NULL, PM_SEM_INSTANT, 
    PMDA_PMUNITS(0,0,0,0,0,0) } },

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
 * network snmp cluster
 */

/* network.ip.forwarding */
  { &proc_net_snmp.ip[_PM_SNMP_IP_FORWARDING], 
    { PMDA_PMID(CLUSTER_NET_SNMP,0), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.ip.defaultttl */
  { &proc_net_snmp.ip[_PM_SNMP_IP_DEFAULTTTL], 
    { PMDA_PMID(CLUSTER_NET_SNMP,1), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.ip.inreceives */
  { &proc_net_snmp.ip[_PM_SNMP_IP_INRECEIVES], 
    { PMDA_PMID(CLUSTER_NET_SNMP,2), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.ip.inhdrerrors */
  { &proc_net_snmp.ip[_PM_SNMP_IP_INHDRERRORS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,3), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.ip.inaddrerrors */
  { &proc_net_snmp.ip[_PM_SNMP_IP_INADDRERRORS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,4), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.ip.forwdatagrams */
  { &proc_net_snmp.ip[_PM_SNMP_IP_FORWDATAGRAMS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,5), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.ip.inunknownprotos */
  { &proc_net_snmp.ip[_PM_SNMP_IP_INUNKNOWNPROTOS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,6), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.ip.indiscards */
  { &proc_net_snmp.ip[_PM_SNMP_IP_INDISCARDS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,7), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.ip.indelivers */
  { &proc_net_snmp.ip[_PM_SNMP_IP_INDELIVERS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,8), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.ip.outrequests */
  { &proc_net_snmp.ip[_PM_SNMP_IP_OUTREQUESTS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,9), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.ip.outdiscards */
  { &proc_net_snmp.ip[_PM_SNMP_IP_OUTDISCARDS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,10), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.ip.outnoroutes */
  { &proc_net_snmp.ip[_PM_SNMP_IP_OUTNOROUTES], 
    { PMDA_PMID(CLUSTER_NET_SNMP,11), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.ip.reasmtimeout */
  { &proc_net_snmp.ip[_PM_SNMP_IP_REASMTIMEOUT], 
    { PMDA_PMID(CLUSTER_NET_SNMP,12), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.ip.reasmreqds */
  { &proc_net_snmp.ip[_PM_SNMP_IP_REASMREQDS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,13), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.ip.reasmoks */
  { &proc_net_snmp.ip[_PM_SNMP_IP_REASMOKS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,14), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.ip.reasmfails */
  { &proc_net_snmp.ip[_PM_SNMP_IP_REASMFAILS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,15), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.ip.fragoks */
  { &proc_net_snmp.ip[_PM_SNMP_IP_FRAGOKS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,16), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.ip.fragfails */
  { &proc_net_snmp.ip[_PM_SNMP_IP_FRAGFAILS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,17), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.ip.fragcreates */
  { &proc_net_snmp.ip[_PM_SNMP_IP_FRAGCREATES], 
    { PMDA_PMID(CLUSTER_NET_SNMP,18), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },


/* network.icmp.inmsgs */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_INMSGS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,20), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.inerrors */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_INERRORS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,21), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.indestunreachs */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_INDESTUNREACHS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,22), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.intimeexcds */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_INTIMEEXCDS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,23), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.inparmprobs */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_INPARMPROBS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,24), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.insrcquenchs */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_INSRCQUENCHS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,25), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.inredirects */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_INREDIRECTS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,26), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.inechos */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_INECHOS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,27), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.inechoreps */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_INECHOREPS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,28), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.intimestamps */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_INTIMESTAMPS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,29), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.intimestampreps */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_INTIMESTAMPREPS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,30), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.inaddrmasks */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_INADDRMASKS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,31), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.inaddrmaskreps */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_INADDRMASKREPS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,32), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.outmsgs */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_OUTMSGS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,33), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.outerrors */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_OUTERRORS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,34), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.outdestunreachs */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_OUTDESTUNREACHS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,35), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.outtimeexcds */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_OUTTIMEEXCDS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,36), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.outparmprobs */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_OUTPARMPROBS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,37), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.outsrcquenchs */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_OUTSRCQUENCHS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,38), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.outredirects */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_OUTREDIRECTS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,39), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.outechos */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_OUTECHOS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,40), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.outechoreps */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_OUTECHOREPS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,41), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.outtimestamps */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_OUTTIMESTAMPS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,42), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.outtimestampreps */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_OUTTIMESTAMPREPS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,43), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.outaddrmasks */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_OUTADDRMASKS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,44), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.icmp.outaddrmaskreps */
  { &proc_net_snmp.icmp[_PM_SNMP_ICMP_OUTADDRMASKREPS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,45), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },


/* network.tcp.rtoalgorithm */
  { &proc_net_snmp.tcp[_PM_SNMP_TCP_RTOALGORITHM], 
    { PMDA_PMID(CLUSTER_NET_SNMP,50), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcp.rtomin */
  { &proc_net_snmp.tcp[_PM_SNMP_TCP_RTOMIN], 
    { PMDA_PMID(CLUSTER_NET_SNMP,51), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcp.rtomax */
  { &proc_net_snmp.tcp[_PM_SNMP_TCP_RTOMAX], 
    { PMDA_PMID(CLUSTER_NET_SNMP,52), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcp.maxconn */
  { &proc_net_snmp.tcp[_PM_SNMP_TCP_MAXCONN], 
    { PMDA_PMID(CLUSTER_NET_SNMP,53), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },
			
/* network.tcpconn.established */
  { &proc_net_tcp.stat[_PM_TCP_ESTABLISHED],
    { PMDA_PMID(CLUSTER_NET_TCP, 1), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcpconn.syn_sent */
  { &proc_net_tcp.stat[_PM_TCP_SYN_SENT],
    { PMDA_PMID(CLUSTER_NET_TCP, 2), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcpconn.syn_recv */
  { &proc_net_tcp.stat[_PM_TCP_SYN_RECV],
    { PMDA_PMID(CLUSTER_NET_TCP, 3), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcpconn.fin_wait1 */
  { &proc_net_tcp.stat[_PM_TCP_FIN_WAIT1],
    { PMDA_PMID(CLUSTER_NET_TCP, 4), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcpconn.fin_wait2 */
  { &proc_net_tcp.stat[_PM_TCP_FIN_WAIT2],
    { PMDA_PMID(CLUSTER_NET_TCP, 5), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcpconn.time_wait */
  { &proc_net_tcp.stat[_PM_TCP_TIME_WAIT],
    { PMDA_PMID(CLUSTER_NET_TCP, 6), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcpconn.close */
  { &proc_net_tcp.stat[_PM_TCP_CLOSE],
    { PMDA_PMID(CLUSTER_NET_TCP, 7), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcpconn.close_wait */
  { &proc_net_tcp.stat[_PM_TCP_CLOSE_WAIT],
    { PMDA_PMID(CLUSTER_NET_TCP, 8), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcpconn.last_ack */
  { &proc_net_tcp.stat[_PM_TCP_LAST_ACK],
    { PMDA_PMID(CLUSTER_NET_TCP, 9), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcpconn.listen */
  { &proc_net_tcp.stat[_PM_TCP_LISTEN],
    { PMDA_PMID(CLUSTER_NET_TCP, 10), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcpconn.closing */
  { &proc_net_tcp.stat[_PM_TCP_CLOSING],
    { PMDA_PMID(CLUSTER_NET_TCP, 11), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcp.activeopens */
  { &proc_net_snmp.tcp[_PM_SNMP_TCP_ACTIVEOPENS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,54), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcp.passiveopens */
  { &proc_net_snmp.tcp[_PM_SNMP_TCP_PASSIVEOPENS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,55), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcp.attemptfails */
  { &proc_net_snmp.tcp[_PM_SNMP_TCP_ATTEMPTFAILS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,56), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcp.estabresets */
  { &proc_net_snmp.tcp[_PM_SNMP_TCP_ESTABRESETS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,57), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcp.currestab */
  { &proc_net_snmp.tcp[_PM_SNMP_TCP_CURRESTAB], 
    { PMDA_PMID(CLUSTER_NET_SNMP,58), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcp.insegs */
  { &proc_net_snmp.tcp[_PM_SNMP_TCP_INSEGS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,59), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcp.outsegs */
  { &proc_net_snmp.tcp[_PM_SNMP_TCP_OUTSEGS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,60), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcp.retranssegs */
  { &proc_net_snmp.tcp[_PM_SNMP_TCP_RETRANSSEGS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,61), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcp.inerrs */
  { &proc_net_snmp.tcp[_PM_SNMP_TCP_INERRS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,62), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.tcp.outrsts */
  { &proc_net_snmp.tcp[_PM_SNMP_TCP_OUTRSTS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,63), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.udp.indatagrams */
  { &proc_net_snmp.udp[_PM_SNMP_UDP_INDATAGRAMS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,70), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.udp.noports */
  { &proc_net_snmp.udp[_PM_SNMP_UDP_NOPORTS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,71), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.udp.inerrors */
  { &proc_net_snmp.udp[_PM_SNMP_UDP_INERRORS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,72), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.udp.outdatagrams */
  { &proc_net_snmp.udp[_PM_SNMP_UDP_OUTDATAGRAMS], 
    { PMDA_PMID(CLUSTER_NET_SNMP,74), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.udp.recvbuferrors */
  { &proc_net_snmp.udp[_PM_SNMP_UDP_RECVBUFERRORS],
    { PMDA_PMID(CLUSTER_NET_SNMP,75), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.udp.sndbuferrors */
  { &proc_net_snmp.udp[_PM_SNMP_UDP_SNDBUFERRORS],
    { PMDA_PMID(CLUSTER_NET_SNMP,76), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.udplite.indatagrams */
  { &proc_net_snmp.udplite[_PM_SNMP_UDPLITE_INDATAGRAMS],
    { PMDA_PMID(CLUSTER_NET_SNMP,77), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.udplite.noports */
  { &proc_net_snmp.udplite[_PM_SNMP_UDPLITE_NOPORTS],
    { PMDA_PMID(CLUSTER_NET_SNMP,78), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.udplite.inerrors */
  { &proc_net_snmp.udplite[_PM_SNMP_UDPLITE_INERRORS],
    { PMDA_PMID(CLUSTER_NET_SNMP,79), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.udplite.outdatagrams */
  { &proc_net_snmp.udplite[_PM_SNMP_UDPLITE_OUTDATAGRAMS],
    { PMDA_PMID(CLUSTER_NET_SNMP,80), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.udplite.recvbuferrors */
  { &proc_net_snmp.udplite[_PM_SNMP_UDPLITE_RECVBUFERRORS],
    { PMDA_PMID(CLUSTER_NET_SNMP,81), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* network.udplite.sndbuferrors */
  { &proc_net_snmp.udplite[_PM_SNMP_UDPLITE_SNDBUFERRORS],
    { PMDA_PMID(CLUSTER_NET_SNMP,82), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* hinv.map.scsi */
    { NULL, 
      { PMDA_PMID(CLUSTER_SCSI,0), PM_TYPE_STRING, SCSI_INDOM, PM_SEM_DISCRETE, 
      PMDA_PMUNITS(0,0,0,0,0,0) }, },

/*
 * /proc/fs/xfs/stat cluster
 */

/* xfs.allocs.alloc_extent */
    { &proc_fs_xfs.xs_allocx,
      { PMDA_PMID(CLUSTER_XFS,0), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.allocs.alloc_block */
    { &proc_fs_xfs.xs_allocb,
      { PMDA_PMID(CLUSTER_XFS,1), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.allocs.free_extent*/
    { &proc_fs_xfs.xs_freex,
      { PMDA_PMID(CLUSTER_XFS,2), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.allocs.free_block */
    { &proc_fs_xfs.xs_freeb,
      { PMDA_PMID(CLUSTER_XFS,3), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* xfs.alloc_btree.lookup */
    { &proc_fs_xfs.xs_abt_lookup,
      { PMDA_PMID(CLUSTER_XFS,4), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.alloc_btree.compare */
    { &proc_fs_xfs.xs_abt_compare,
      { PMDA_PMID(CLUSTER_XFS,5), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.alloc_btree.insrec */
    { &proc_fs_xfs.xs_abt_insrec,
      { PMDA_PMID(CLUSTER_XFS,6), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.alloc_btree.delrec */
    { &proc_fs_xfs.xs_abt_delrec,
      { PMDA_PMID(CLUSTER_XFS,7), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* xfs.block_map.read_ops */
    { &proc_fs_xfs.xs_blk_mapr,
      { PMDA_PMID(CLUSTER_XFS,8), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.block_map.write_ops */
    { &proc_fs_xfs.xs_blk_mapw,
      { PMDA_PMID(CLUSTER_XFS,9), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.block_map.unmap */
    { &proc_fs_xfs.xs_blk_unmap,
      { PMDA_PMID(CLUSTER_XFS,10), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.block_map.add_exlist */
    { &proc_fs_xfs.xs_add_exlist,
      { PMDA_PMID(CLUSTER_XFS,11), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.block_map.del_exlist */
    { &proc_fs_xfs.xs_del_exlist,
      { PMDA_PMID(CLUSTER_XFS,12), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.block_map.look_exlist */
    { &proc_fs_xfs.xs_look_exlist,
      { PMDA_PMID(CLUSTER_XFS,13), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.block_map.cmp_exlist */
    { &proc_fs_xfs.xs_cmp_exlist,
      { PMDA_PMID(CLUSTER_XFS,14), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* xfs.bmap_btree.lookup */
    { &proc_fs_xfs.xs_bmbt_lookup,
      { PMDA_PMID(CLUSTER_XFS,15), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.bmap_btree.compare */
    { &proc_fs_xfs.xs_bmbt_compare,
      { PMDA_PMID(CLUSTER_XFS,16), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.bmap_btree.insrec */
    { &proc_fs_xfs.xs_bmbt_insrec,
      { PMDA_PMID(CLUSTER_XFS,17), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.bmap_btree.delrec */
    { &proc_fs_xfs.xs_bmbt_delrec,
      { PMDA_PMID(CLUSTER_XFS,18), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* xfs.dir_ops.lookup */
    { &proc_fs_xfs.xs_dir_lookup,
      { PMDA_PMID(CLUSTER_XFS,19), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.dir_ops.create */
    { &proc_fs_xfs.xs_dir_create,
      { PMDA_PMID(CLUSTER_XFS,20), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.dir_ops.remove */
    { &proc_fs_xfs.xs_dir_remove,
      { PMDA_PMID(CLUSTER_XFS,21), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.dir_ops.getdents */
    { &proc_fs_xfs.xs_dir_getdents,
      { PMDA_PMID(CLUSTER_XFS,22), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* xfs.transactions.sync */
    { &proc_fs_xfs.xs_trans_sync,
      { PMDA_PMID(CLUSTER_XFS,23), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.transactions.async */
    { &proc_fs_xfs.xs_trans_async,
      { PMDA_PMID(CLUSTER_XFS,24), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.transactions.empty */
    { &proc_fs_xfs.xs_trans_empty,
      { PMDA_PMID(CLUSTER_XFS,25), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* xfs.inode_ops.ig_attempts */
    { &proc_fs_xfs.xs_ig_attempts,
      { PMDA_PMID(CLUSTER_XFS,26), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.inode_ops.ig_found */
    { &proc_fs_xfs.xs_ig_found,
      { PMDA_PMID(CLUSTER_XFS,27), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.inode_ops.ig_frecycle */
    { &proc_fs_xfs.xs_ig_frecycle,
      { PMDA_PMID(CLUSTER_XFS,28), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.inode_ops.ig_missed */
    { &proc_fs_xfs.xs_ig_missed,
      { PMDA_PMID(CLUSTER_XFS,29), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.inode_ops.ig_dup */
    { &proc_fs_xfs.xs_ig_dup,
      { PMDA_PMID(CLUSTER_XFS,30), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.inode_ops.ig_reclaims */
    { &proc_fs_xfs.xs_ig_reclaims,
      { PMDA_PMID(CLUSTER_XFS,31), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.inode_ops.ig_attrchg */
    { &proc_fs_xfs.xs_ig_attrchg,
      { PMDA_PMID(CLUSTER_XFS,32), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* xfs.log.writes */
    { &proc_fs_xfs.xs_log_writes,
      { PMDA_PMID(CLUSTER_XFS,33), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.log.blocks */
    { &proc_fs_xfs.xs_log_blocks,
      { PMDA_PMID(CLUSTER_XFS,34), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },
/* xfs.log.noiclogs */
    { &proc_fs_xfs.xs_log_noiclogs,
      { PMDA_PMID(CLUSTER_XFS,35), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.log.force */
    { &proc_fs_xfs.xs_log_force,
      { PMDA_PMID(CLUSTER_XFS,36), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.log.force_sleep */
    { &proc_fs_xfs.xs_log_force_sleep,
      { PMDA_PMID(CLUSTER_XFS,37), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* xfs.log_tail.try_logspace */
    { &proc_fs_xfs.xs_try_logspace,
      { PMDA_PMID(CLUSTER_XFS,38), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.log_tail.sleep_logspace */
    { &proc_fs_xfs.xs_sleep_logspace,
      { PMDA_PMID(CLUSTER_XFS,39), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.log_tail.push_ail.pushes */
    { &proc_fs_xfs.xs_push_ail,
      { PMDA_PMID(CLUSTER_XFS,40), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.log_tail.push_ail.success */
    { &proc_fs_xfs.xs_push_ail_success,
      { PMDA_PMID(CLUSTER_XFS,41), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.log_tail.push_ail.pushbuf */
    { &proc_fs_xfs.xs_push_ail_pushbuf,
      { PMDA_PMID(CLUSTER_XFS,42), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.log_tail.push_ail.pinned */
    { &proc_fs_xfs.xs_push_ail_pinned,
      { PMDA_PMID(CLUSTER_XFS,43), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.log_tail.push_ail.locked */
    { &proc_fs_xfs.xs_push_ail_locked,
      { PMDA_PMID(CLUSTER_XFS,44), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.log_tail.push_ail.flushing */
    { &proc_fs_xfs.xs_push_ail_flushing,
      { PMDA_PMID(CLUSTER_XFS,45), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.log_tail.push_ail.restarts */
    { &proc_fs_xfs.xs_push_ail_restarts,
      { PMDA_PMID(CLUSTER_XFS,46), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.log_tail.push_ail.flush */
    { &proc_fs_xfs.xs_push_ail_flush,
      { PMDA_PMID(CLUSTER_XFS,47), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* xfs.xstrat.bytes */
    { &proc_fs_xfs.xpc.xs_xstrat_bytes,
      { PMDA_PMID(CLUSTER_XFS,48), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0) }, },
/* xfs.xstrat.quick */
    { &proc_fs_xfs.xs_xstrat_quick,
      { PMDA_PMID(CLUSTER_XFS,49), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.xstrat.split */
    { &proc_fs_xfs.xs_xstrat_split,
      { PMDA_PMID(CLUSTER_XFS,50), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* xfs.write */
    { &proc_fs_xfs.xs_write_calls,
      { PMDA_PMID(CLUSTER_XFS,51), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.write_bytes */
    { &proc_fs_xfs.xpc.xs_write_bytes,
      { PMDA_PMID(CLUSTER_XFS,52), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0) }, },
/* xfs.read */
    { &proc_fs_xfs.xs_read_calls,
      { PMDA_PMID(CLUSTER_XFS,53), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.read_bytes */
    { &proc_fs_xfs.xpc.xs_read_bytes,
      { PMDA_PMID(CLUSTER_XFS,54), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0) }, },

/* xfs.attr.get */
    { &proc_fs_xfs.xs_attr_get,
      { PMDA_PMID(CLUSTER_XFS,55), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.attr.set */
    { &proc_fs_xfs.xs_attr_set,
      { PMDA_PMID(CLUSTER_XFS,56), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.attr.remove */
    { &proc_fs_xfs.xs_attr_remove,
      { PMDA_PMID(CLUSTER_XFS,57), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.attr.list */
    { &proc_fs_xfs.xs_attr_list,
      { PMDA_PMID(CLUSTER_XFS,58), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* xfs.quota.reclaims */
    { &proc_fs_xfs.xs_qm_dqreclaims,
      { PMDA_PMID(CLUSTER_XFS,59), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.quota.reclaim_misses */
    { &proc_fs_xfs.xs_qm_dqreclaim_misses,
      { PMDA_PMID(CLUSTER_XFS,60), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.quota.dquot_dups */
    { &proc_fs_xfs.xs_qm_dquot_dups,
      { PMDA_PMID(CLUSTER_XFS,61), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.quota.cachemisses */
    { &proc_fs_xfs.xs_qm_dqcachemisses,
      { PMDA_PMID(CLUSTER_XFS,62), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.quota.cachehits */
    { &proc_fs_xfs.xs_qm_dqcachehits,
      { PMDA_PMID(CLUSTER_XFS,63), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.quota.wants */
    { &proc_fs_xfs.xs_qm_dqwants,
      { PMDA_PMID(CLUSTER_XFS,64), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.quota.shake_reclaims */
    { &proc_fs_xfs.xs_qm_dqshake_reclaims,
      { PMDA_PMID(CLUSTER_XFS,65), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.quota.inact_reclaims */
    { &proc_fs_xfs.xs_qm_dqinact_reclaims,
      { PMDA_PMID(CLUSTER_XFS,66), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* xfs.iflush_count */
    { &proc_fs_xfs.xs_iflush_count,
      { PMDA_PMID(CLUSTER_XFS,67), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.icluster_flushcnt */
    { &proc_fs_xfs.xs_icluster_flushcnt,
      { PMDA_PMID(CLUSTER_XFS,68), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.icluster_flushinode */
    { &proc_fs_xfs.xs_icluster_flushinode,
      { PMDA_PMID(CLUSTER_XFS,69), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* xfs.buffer.get */
    { &proc_fs_xfs.xs_buf_get,
      { PMDA_PMID(CLUSTER_XFSBUF,0), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.buffer.create */
    { &proc_fs_xfs.xs_buf_create,
      { PMDA_PMID(CLUSTER_XFSBUF,1), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.buffer.get_locked */
    { &proc_fs_xfs.xs_buf_get_locked,
      { PMDA_PMID(CLUSTER_XFSBUF,2), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.buffer.get_locked_waited */
    { &proc_fs_xfs.xs_buf_get_locked_waited,
      { PMDA_PMID(CLUSTER_XFSBUF,3), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.buffer.busy_locked */
    { &proc_fs_xfs.xs_buf_busy_locked,
      { PMDA_PMID(CLUSTER_XFSBUF,4), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.buffer.miss_locked */
    { &proc_fs_xfs.xs_buf_miss_locked,
      { PMDA_PMID(CLUSTER_XFSBUF,5), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.buffer.page_retries */
    { &proc_fs_xfs.xs_buf_page_retries,
      { PMDA_PMID(CLUSTER_XFSBUF,6), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.buffer.page_found */         
    { &proc_fs_xfs.xs_buf_page_found,
      { PMDA_PMID(CLUSTER_XFSBUF,7), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.buffer.get_read */         
    { &proc_fs_xfs.xs_buf_get_read,
      { PMDA_PMID(CLUSTER_XFSBUF,8), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* xfs.vnodes.active */
    { &proc_fs_xfs.vnodes.vn_active,
      { PMDA_PMID(CLUSTER_XFS,70), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,0,0,0,0) }, },
/* xfs.vnodes.alloc */
    { &proc_fs_xfs.vnodes.vn_alloc,
      { PMDA_PMID(CLUSTER_XFS,71), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.vnodes.get */
    { &proc_fs_xfs.vnodes.vn_get,
      { PMDA_PMID(CLUSTER_XFS,72), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.vnodes.hold */
    { &proc_fs_xfs.vnodes.vn_hold,
      { PMDA_PMID(CLUSTER_XFS,73), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.vnodes.rele */
    { &proc_fs_xfs.vnodes.vn_rele,
      { PMDA_PMID(CLUSTER_XFS,74), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.vnodes.reclaim */
    { &proc_fs_xfs.vnodes.vn_reclaim,
      { PMDA_PMID(CLUSTER_XFS,75), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.vnodes.remove */
    { &proc_fs_xfs.vnodes.vn_remove,
      { PMDA_PMID(CLUSTER_XFS,76), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.vnodes.free */
    { &proc_fs_xfs.vnodes.vn_free,
      { PMDA_PMID(CLUSTER_XFS,77), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* xfs.log.write_ratio */
    { &proc_fs_xfs.xs_log_write_ratio,
      { PMDA_PMID(CLUSTER_XFS,78), PM_TYPE_FLOAT, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,0,0,0,0) }, },
/* xfs.control.reset */
    { NULL,
      { PMDA_PMID(CLUSTER_XFS,79), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
      PMDA_PMUNITS(0,0,0,0,0,0) }, },

/* xfs.btree.alloc_blocks.lookup */
    { &proc_fs_xfs.xs_abtb_2_lookup,
      { PMDA_PMID(CLUSTER_XFS,80), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_blocks.compare */
    { &proc_fs_xfs.xs_abtb_2_compare,
      { PMDA_PMID(CLUSTER_XFS,81), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_blocks.insrec */
    { &proc_fs_xfs.xs_abtb_2_insrec,
      { PMDA_PMID(CLUSTER_XFS,82), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_blocks.delrec */
    { &proc_fs_xfs.xs_abtb_2_delrec,
      { PMDA_PMID(CLUSTER_XFS,83), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_blocks.newroot */
    { &proc_fs_xfs.xs_abtb_2_newroot,
      { PMDA_PMID(CLUSTER_XFS,84), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_blocks.killroot */
    { &proc_fs_xfs.xs_abtb_2_killroot,
      { PMDA_PMID(CLUSTER_XFS,85), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_blocks.increment */
    { &proc_fs_xfs.xs_abtb_2_increment,
      { PMDA_PMID(CLUSTER_XFS,86), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_blocks.decrement */
    { &proc_fs_xfs.xs_abtb_2_decrement,
      { PMDA_PMID(CLUSTER_XFS,87), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_blocks.lshift */
    { &proc_fs_xfs.xs_abtb_2_lshift,
      { PMDA_PMID(CLUSTER_XFS,88), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_blocks.rshift */
    { &proc_fs_xfs.xs_abtb_2_rshift,
      { PMDA_PMID(CLUSTER_XFS,89), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_blocks.split */
    { &proc_fs_xfs.xs_abtb_2_split,
      { PMDA_PMID(CLUSTER_XFS,90), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_blocks.join */
    { &proc_fs_xfs.xs_abtb_2_join,
      { PMDA_PMID(CLUSTER_XFS,91), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_blocks.alloc */
    { &proc_fs_xfs.xs_abtb_2_alloc,
      { PMDA_PMID(CLUSTER_XFS,92), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_blocks.free */
    { &proc_fs_xfs.xs_abtb_2_free,
      { PMDA_PMID(CLUSTER_XFS,93), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_blocks.moves */
    { &proc_fs_xfs.xs_abtb_2_moves,
      { PMDA_PMID(CLUSTER_XFS,94), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* xfs.btree.alloc_contig.lookup */
    { &proc_fs_xfs.xs_abtc_2_lookup,
      { PMDA_PMID(CLUSTER_XFS,95), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_contig.compare */
    { &proc_fs_xfs.xs_abtc_2_compare,
      { PMDA_PMID(CLUSTER_XFS,96), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_contig.insrec */
    { &proc_fs_xfs.xs_abtc_2_insrec,
      { PMDA_PMID(CLUSTER_XFS,97), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_contig.delrec */
    { &proc_fs_xfs.xs_abtc_2_delrec,
      { PMDA_PMID(CLUSTER_XFS,98), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_contig.newroot */
    { &proc_fs_xfs.xs_abtc_2_newroot,
      { PMDA_PMID(CLUSTER_XFS,99), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_contig.killroot */
    { &proc_fs_xfs.xs_abtc_2_killroot,
      { PMDA_PMID(CLUSTER_XFS,100), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_contig.increment */
    { &proc_fs_xfs.xs_abtc_2_increment,
      { PMDA_PMID(CLUSTER_XFS,101), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_contig.decrement */
    { &proc_fs_xfs.xs_abtc_2_decrement,
      { PMDA_PMID(CLUSTER_XFS,102), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_contig.lshift */
    { &proc_fs_xfs.xs_abtc_2_lshift,
      { PMDA_PMID(CLUSTER_XFS,103), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_contig.rshift */
    { &proc_fs_xfs.xs_abtc_2_rshift,
      { PMDA_PMID(CLUSTER_XFS,104), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_contig.split */
    { &proc_fs_xfs.xs_abtc_2_split,
      { PMDA_PMID(CLUSTER_XFS,105), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_contig.join */
    { &proc_fs_xfs.xs_abtc_2_join,
      { PMDA_PMID(CLUSTER_XFS,106), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_contig.alloc */
    { &proc_fs_xfs.xs_abtc_2_alloc,
      { PMDA_PMID(CLUSTER_XFS,107), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_contig.free */
    { &proc_fs_xfs.xs_abtc_2_free,
      { PMDA_PMID(CLUSTER_XFS,108), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.alloc_contig.moves */
    { &proc_fs_xfs.xs_abtc_2_moves,
      { PMDA_PMID(CLUSTER_XFS,109), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* xfs.btree.block_map.lookup */
    { &proc_fs_xfs.xs_bmbt_2_lookup,
      { PMDA_PMID(CLUSTER_XFS,110), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.block_map.compare */
    { &proc_fs_xfs.xs_bmbt_2_compare,
      { PMDA_PMID(CLUSTER_XFS,111), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.block_map.insrec */
    { &proc_fs_xfs.xs_bmbt_2_insrec,
      { PMDA_PMID(CLUSTER_XFS,112), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.block_map.delrec */
    { &proc_fs_xfs.xs_bmbt_2_delrec,
      { PMDA_PMID(CLUSTER_XFS,113), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.block_map.newroot */
    { &proc_fs_xfs.xs_bmbt_2_newroot,
      { PMDA_PMID(CLUSTER_XFS,114), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.block_map.killroot */
    { &proc_fs_xfs.xs_bmbt_2_killroot,
      { PMDA_PMID(CLUSTER_XFS,115), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.block_map.increment */
    { &proc_fs_xfs.xs_bmbt_2_increment,
      { PMDA_PMID(CLUSTER_XFS,116), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.block_map.decrement */
    { &proc_fs_xfs.xs_bmbt_2_decrement,
      { PMDA_PMID(CLUSTER_XFS,117), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.block_map.lshift */
    { &proc_fs_xfs.xs_bmbt_2_lshift,
      { PMDA_PMID(CLUSTER_XFS,118), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.block_map.rshift */
    { &proc_fs_xfs.xs_bmbt_2_rshift,
      { PMDA_PMID(CLUSTER_XFS,119), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.block_map.split */
    { &proc_fs_xfs.xs_bmbt_2_split,
      { PMDA_PMID(CLUSTER_XFS,120), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.block_map.join */
    { &proc_fs_xfs.xs_bmbt_2_join,
      { PMDA_PMID(CLUSTER_XFS,121), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.block_map.alloc */
    { &proc_fs_xfs.xs_bmbt_2_alloc,
      { PMDA_PMID(CLUSTER_XFS,122), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.block_map.free */
    { &proc_fs_xfs.xs_bmbt_2_free,
      { PMDA_PMID(CLUSTER_XFS,123), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.block_map.moves */
    { &proc_fs_xfs.xs_bmbt_2_moves,
      { PMDA_PMID(CLUSTER_XFS,124), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* xfs.btree.inode.lookup */
    { &proc_fs_xfs.xs_ibt_2_compare,
      { PMDA_PMID(CLUSTER_XFS,125), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.inode.compare */
    { &proc_fs_xfs.xs_ibt_2_lookup,
      { PMDA_PMID(CLUSTER_XFS,126), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.inode.insrec */
    { &proc_fs_xfs.xs_ibt_2_insrec,
      { PMDA_PMID(CLUSTER_XFS,127), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.inode.delrec */
    { &proc_fs_xfs.xs_ibt_2_delrec,
      { PMDA_PMID(CLUSTER_XFS,128), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.inode.newroot */
    { &proc_fs_xfs.xs_ibt_2_newroot,
      { PMDA_PMID(CLUSTER_XFS,129), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.inode.killroot */
    { &proc_fs_xfs.xs_ibt_2_killroot,
      { PMDA_PMID(CLUSTER_XFS,130), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.inode.increment */
    { &proc_fs_xfs.xs_ibt_2_increment,
      { PMDA_PMID(CLUSTER_XFS,131), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.inode.decrement */
    { &proc_fs_xfs.xs_ibt_2_decrement,
      { PMDA_PMID(CLUSTER_XFS,132), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.inode.lshift */
    { &proc_fs_xfs.xs_ibt_2_lshift,
      { PMDA_PMID(CLUSTER_XFS,133), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.inode.rshift */
    { &proc_fs_xfs.xs_ibt_2_rshift,
      { PMDA_PMID(CLUSTER_XFS,134), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.inode.split */
    { &proc_fs_xfs.xs_ibt_2_split,
      { PMDA_PMID(CLUSTER_XFS,135), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.inode.join */
    { &proc_fs_xfs.xs_ibt_2_join,
      { PMDA_PMID(CLUSTER_XFS,136), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.inode.alloc */
    { &proc_fs_xfs.xs_ibt_2_alloc,
      { PMDA_PMID(CLUSTER_XFS,137), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.inode.free */
    { &proc_fs_xfs.xs_ibt_2_free,
      { PMDA_PMID(CLUSTER_XFS,138), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },
/* xfs.btree.inode.moves */
    { &proc_fs_xfs.xs_ibt_2_moves,
      { PMDA_PMID(CLUSTER_XFS,139), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/*
 * /proc/cpuinfo cluster (cpu indom)
 */

/* hinv.cpu.clock */
  { NULL,
    { PMDA_PMID(CLUSTER_CPUINFO, 0), PM_TYPE_FLOAT, CPU_INDOM, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,1,0,-6,0) } },

/* hinv.cpu.vendor */
  { NULL,
    { PMDA_PMID(CLUSTER_CPUINFO, 1), PM_TYPE_STRING, CPU_INDOM, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* hinv.cpu.model */
  { NULL,
    { PMDA_PMID(CLUSTER_CPUINFO, 2), PM_TYPE_STRING, CPU_INDOM, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* hinv.cpu.stepping */
  { NULL,
    { PMDA_PMID(CLUSTER_CPUINFO, 3), PM_TYPE_STRING, CPU_INDOM, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* hinv.cpu.cache */
  { NULL,
    { PMDA_PMID(CLUSTER_CPUINFO, 4), PM_TYPE_U32, CPU_INDOM, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,PM_SPACE_KBYTE,0,0) } },

/* hinv.cpu.bogomips */
  { NULL,
    { PMDA_PMID(CLUSTER_CPUINFO, 5), PM_TYPE_FLOAT, CPU_INDOM, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* hinv.map.cpu_num */
  { NULL,
    { PMDA_PMID(CLUSTER_CPUINFO, 6), PM_TYPE_U32, CPU_INDOM, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* hinv.machine */
  { NULL,
    { PMDA_PMID(CLUSTER_CPUINFO, 7), PM_TYPE_STRING, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/* hinv.map.cpu_node */
  { NULL,
    { PMDA_PMID(CLUSTER_CPUINFO, 8), PM_TYPE_U32, CPU_INDOM, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0) } },

/*
 * semaphore limits cluster
 * Cluster added by Mike Mason <mmlnx@us.ibm.com>
 */

/* ipc.sem.max_semmap */
  { NULL,
    { PMDA_PMID(CLUSTER_SEM_LIMITS, 0), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* ipc.sem.max_semid */
  { NULL,
    { PMDA_PMID(CLUSTER_SEM_LIMITS, 1), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* ipc.sem.max_sem */
  { NULL,
    { PMDA_PMID(CLUSTER_SEM_LIMITS, 2), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* ipc.sem.num_undo */
  { NULL,
    { PMDA_PMID(CLUSTER_SEM_LIMITS, 3), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* ipc.sem.max_perid */
  { NULL,
    { PMDA_PMID(CLUSTER_SEM_LIMITS, 4), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* ipc.sem.max_ops */
  { NULL,
    { PMDA_PMID(CLUSTER_SEM_LIMITS, 5), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* ipc.sem.max_undoent */
  { NULL,
    { PMDA_PMID(CLUSTER_SEM_LIMITS, 6), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* ipc.sem.sz_semundo */
  { NULL,
    { PMDA_PMID(CLUSTER_SEM_LIMITS, 7), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* ipc.sem.max_semval */
  { NULL,
    { PMDA_PMID(CLUSTER_SEM_LIMITS, 8), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* ipc.sem.max_exit */
  { NULL,
    { PMDA_PMID(CLUSTER_SEM_LIMITS, 9), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/*
 * message limits cluster
 * Cluster added by Mike Mason <mmlnx@us.ibm.com>
 */

/* ipc.msg.sz_pool */
  { NULL,
    { PMDA_PMID(CLUSTER_MSG_LIMITS, 0), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(1,0,0, PM_SPACE_KBYTE,0,0)}},

/* ipc.msg.mapent */
  { NULL,
    { PMDA_PMID(CLUSTER_MSG_LIMITS, 1), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* ipc.msg.max_msgsz */
  { NULL,
    { PMDA_PMID(CLUSTER_MSG_LIMITS, 2), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* ipc.msg.max_defmsgq */
  { NULL,
    { PMDA_PMID(CLUSTER_MSG_LIMITS, 3), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* ipc.msg.max_msgqid */
  { NULL,
    { PMDA_PMID(CLUSTER_MSG_LIMITS, 4), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* ipc.msg.max_msgseg */
  { NULL,
    { PMDA_PMID(CLUSTER_MSG_LIMITS, 5), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0, 0,0,0)}},

/* ipc.msg.max_smsghdr */
  { NULL,
    { PMDA_PMID(CLUSTER_MSG_LIMITS, 6), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* ipc.msg.max_seg */
  { NULL,
    { PMDA_PMID(CLUSTER_MSG_LIMITS, 7), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/*
 * shared memory limits cluster
 * Cluster added by Mike Mason <mmlnx@us.ibm.com>
 */

/* ipc.shm.max_segsz */
  { NULL,
    { PMDA_PMID(CLUSTER_SHM_LIMITS, 0), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0)}},

/* ipc.shm.min_segsz */
  { NULL,
    { PMDA_PMID(CLUSTER_SHM_LIMITS, 1), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0)}},

/* ipc.shm.max_seg */
  { NULL,
    { PMDA_PMID(CLUSTER_SHM_LIMITS, 2), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* ipc.shm.max_segproc */
  { NULL,
    { PMDA_PMID(CLUSTER_SHM_LIMITS, 3), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/* ipc.shm.max_shmsys */
  { NULL,
    { PMDA_PMID(CLUSTER_SHM_LIMITS, 4), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/*
 * number of users cluster
 */

/* kernel.all.nusers */
  { NULL,
    { PMDA_PMID(CLUSTER_NUSERS, 0), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,0,0,0,0)}},

/*
 * /proc/sys/fs vfs cluster
 */

/* vfs.files */
    { &proc_sys_fs.fs_files_count,
      { PMDA_PMID(CLUSTER_VFS,0), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,0,0,0,0) }, },
    { &proc_sys_fs.fs_files_free,
      { PMDA_PMID(CLUSTER_VFS,1), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,0,0,0,0) }, },
    { &proc_sys_fs.fs_files_max,
      { PMDA_PMID(CLUSTER_VFS,2), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,0,0,0,0) }, },
    { &proc_sys_fs.fs_inodes_count,
      { PMDA_PMID(CLUSTER_VFS,3), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,0,0,0,0) }, },
    { &proc_sys_fs.fs_inodes_free,
      { PMDA_PMID(CLUSTER_VFS,4), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,0,0,0,0) }, },
    { &proc_sys_fs.fs_dentry_count,
      { PMDA_PMID(CLUSTER_VFS,5), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,0,0,0,0) }, },
    { &proc_sys_fs.fs_dentry_free,
      { PMDA_PMID(CLUSTER_VFS,6), PM_TYPE_32, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,0,0,0,0) }, },

    /*
     * mem.vmstat cluster
     */

    /* mem.vmstat.nr_dirty */
    { &proc_vmstat.nr_dirty,
    {PMDA_PMID(28,0), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,0,0,0,0) }, },

    /* mem.vmstat.nr_writeback */
    { &proc_vmstat.nr_writeback,
    {PMDA_PMID(28,1), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,0,0,0,0) }, },

    /* mem.vmstat.nr_unstable */
    { &proc_vmstat.nr_unstable,
    {PMDA_PMID(28,2), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,0,0,0,0) }, },

    /* mem.vmstat.nr_page_table_pages */
    { &proc_vmstat.nr_page_table_pages,
    {PMDA_PMID(28,3), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,0,0,0,0) }, },

    /* mem.vmstat.nr_mapped */
    { &proc_vmstat.nr_mapped,
    {PMDA_PMID(28,4), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,0,0,0,0) }, },

    /* mem.vmstat.nr_slab */
    { &proc_vmstat.nr_slab,
    {PMDA_PMID(28,5), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,0,0,0,0) }, },

    /* mem.vmstat.pgpgin */
    { &proc_vmstat.pgpgin,
    {PMDA_PMID(28,6), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgpgout */
    { &proc_vmstat.pgpgout,
    {PMDA_PMID(28,7), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pswpin */
    { &proc_vmstat.pswpin,
    {PMDA_PMID(28,8), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pswpout */
    { &proc_vmstat.pswpout,
    {PMDA_PMID(28,9), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgalloc_high */
    { &proc_vmstat.pgalloc_high,
    {PMDA_PMID(28,10), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgalloc_normal */
    { &proc_vmstat.pgalloc_normal,
    {PMDA_PMID(28,11), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgalloc_dma */
    { &proc_vmstat.pgalloc_dma,
    {PMDA_PMID(28,12), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgfree */
    { &proc_vmstat.pgfree,
    {PMDA_PMID(28,13), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgactivate */
    { &proc_vmstat.pgactivate,
    {PMDA_PMID(28,14), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgdeactivate */
    { &proc_vmstat.pgdeactivate,
    {PMDA_PMID(28,15), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgfault */
    { &proc_vmstat.pgfault,
    {PMDA_PMID(28,16), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgmajfault */
    { &proc_vmstat.pgmajfault,
    {PMDA_PMID(28,17), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgrefill_high */
    { &proc_vmstat.pgrefill_high,
    {PMDA_PMID(28,18), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgrefill_normal */
    { &proc_vmstat.pgrefill_normal,
    {PMDA_PMID(28,19), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgrefill_dma */
    { &proc_vmstat.pgrefill_dma,
    {PMDA_PMID(28,20), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgsteal_high */
    { &proc_vmstat.pgsteal_high,
    {PMDA_PMID(28,21), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgsteal_normal */
    { &proc_vmstat.pgsteal_normal,
    {PMDA_PMID(28,22), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgsteal_dma */
    { &proc_vmstat.pgsteal_dma,
    {PMDA_PMID(28,23), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgscan_kswapd_high */
    { &proc_vmstat.pgscan_kswapd_high,
    {PMDA_PMID(28,24), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgscan_kswapd_normal */
    { &proc_vmstat.pgscan_kswapd_normal,
    {PMDA_PMID(28,25), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgscan_kswapd_dma */
    { &proc_vmstat.pgscan_kswapd_dma,
    {PMDA_PMID(28,26), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgscan_direct_high */
    { &proc_vmstat.pgscan_direct_high,
    {PMDA_PMID(28,27), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgscan_direct_normal */
    { &proc_vmstat.pgscan_direct_normal,
    {PMDA_PMID(28,28), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgscan_direct_dma */
    { &proc_vmstat.pgscan_direct_dma,
    {PMDA_PMID(28,29), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pginodesteal */
    { &proc_vmstat.pginodesteal,
    {PMDA_PMID(28,30), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.slabs_scanned */
    { &proc_vmstat.slabs_scanned,
    {PMDA_PMID(28,31), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.kswapd_steal */
    { &proc_vmstat.kswapd_steal,
    {PMDA_PMID(28,32), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.kswapd_inodesteal */
    { &proc_vmstat.kswapd_inodesteal,
    {PMDA_PMID(28,33), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pageoutrun */
    { &proc_vmstat.pageoutrun,
    {PMDA_PMID(28,34), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.allocstall */
    { &proc_vmstat.allocstall,
    {PMDA_PMID(28,35), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgrotated */
    { &proc_vmstat.pgrotated,
    {PMDA_PMID(28,36), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.nr_slab_reclaimable */
    { &proc_vmstat.nr_slab_reclaimable,
    {PMDA_PMID(28,37), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,0,0,0,0) }, },

    /* mem.vmstat.nr_slab_unreclaimable */
    { &proc_vmstat.nr_slab_unreclaimable,
    {PMDA_PMID(28,38), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,0,0,0,0) }, },

    /* mem.vmstat.nr_anon_pages */
    { &proc_vmstat.nr_anon_pages,
    {PMDA_PMID(28,39), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,0,0,0,0) }, },

    /* mem.vmstat.nr_bounce */
    { &proc_vmstat.nr_bounce,
    {PMDA_PMID(28,40), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,0,0,0,0) }, },

    /* mem.vmstat.nr_file_pages */
    { &proc_vmstat.nr_file_pages,
    {PMDA_PMID(28,41), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,0,0,0,0) }, },

    /* mem.vmstat.nr_vmscan_write */
    { &proc_vmstat.nr_vmscan_write,
    {PMDA_PMID(28,42), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.htlb_buddy_alloc_fail */
    { &proc_vmstat.htlb_buddy_alloc_fail,
    {PMDA_PMID(28,43), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.htlb_buddy_alloc_success */
    { &proc_vmstat.htlb_buddy_alloc_success,
    {PMDA_PMID(28,44), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.nr_active_anon */
    { &proc_vmstat.nr_active_anon,
    {PMDA_PMID(28,45), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.nr_active_file */
    { &proc_vmstat.nr_active_file,
    {PMDA_PMID(28,46), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.nr_free_pages */
    { &proc_vmstat.nr_free_pages,
    {PMDA_PMID(28,47), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.nr_inactive_anon */
    { &proc_vmstat.nr_inactive_anon,
    {PMDA_PMID(28,48), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.nr_inactive_file */
    { &proc_vmstat.nr_inactive_file,
    {PMDA_PMID(28,49), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.nr_isolated_anon */
    { &proc_vmstat.nr_isolated_anon,
    {PMDA_PMID(28,50), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.nr_isolated_file */
    { &proc_vmstat.nr_isolated_file,
    {PMDA_PMID(28,51), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.nr_kernel_stack */
    { &proc_vmstat.nr_kernel_stack,
    {PMDA_PMID(28,52), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.nr_mlock */
    { &proc_vmstat.nr_mlock,
    {PMDA_PMID(28,53), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.nr_shmem */
    { &proc_vmstat.nr_shmem,
    {PMDA_PMID(28,54), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.nr_unevictable */
    { &proc_vmstat.nr_unevictable,
    {PMDA_PMID(28,55), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.nr_writeback_temp */
    { &proc_vmstat.nr_writeback_temp,
    {PMDA_PMID(28,56), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.compact_blocks_moved */
    { &proc_vmstat.compact_blocks_moved,
    {PMDA_PMID(28,57), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.compact_fail */
    { &proc_vmstat.compact_fail,
    {PMDA_PMID(28,58), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.compact_pagemigrate_failed */
    { &proc_vmstat.compact_pagemigrate_failed,
    {PMDA_PMID(28,59), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.compact_pages_moved */
    { &proc_vmstat.compact_pages_moved,
    {PMDA_PMID(28,60), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.compact_stall */
    { &proc_vmstat.compact_stall,
    {PMDA_PMID(28,61), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.compact_success */
    { &proc_vmstat.compact_success,
    {PMDA_PMID(28,62), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgalloc_dma32 */
    { &proc_vmstat.pgalloc_dma32,
    {PMDA_PMID(28,63), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgalloc_movable */
    { &proc_vmstat.pgalloc_movable,
    {PMDA_PMID(28,64), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgrefill_dma32 */
    { &proc_vmstat.pgrefill_dma32,
    {PMDA_PMID(28,65), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgrefill_movable */
    { &proc_vmstat.pgrefill_movable,
    {PMDA_PMID(28,66), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgscan_direct_dma32 */
    { &proc_vmstat.pgscan_direct_dma32,
    {PMDA_PMID(28,67), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgscan_direct_movable */
    { &proc_vmstat.pgscan_direct_movable,
    {PMDA_PMID(28,68), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgscan_kswapd_dma32 */
    { &proc_vmstat.pgscan_kswapd_dma32,
    {PMDA_PMID(28,69), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgscan_kswapd_movable */
    { &proc_vmstat.pgscan_kswapd_movable,
    {PMDA_PMID(28,70), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgsteal_dma32 */
    { &proc_vmstat.pgsteal_dma32,
    {PMDA_PMID(28,71), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.pgsteal_movable */
    { &proc_vmstat.pgsteal_movable,
    {PMDA_PMID(28,72), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.thp_fault_alloc */
    { &proc_vmstat.thp_fault_alloc,
    {PMDA_PMID(28,73), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.thp_fault_fallback */
    { &proc_vmstat.thp_fault_fallback,
    {PMDA_PMID(28,74), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.thp_collapse_alloc */
    { &proc_vmstat.thp_collapse_alloc,
    {PMDA_PMID(28,75), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.thp_collapse_alloc_failed */
    { &proc_vmstat.thp_collapse_alloc_failed,
    {PMDA_PMID(28,76), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.thp_split */
    { &proc_vmstat.thp_split,
    {PMDA_PMID(28,77), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.unevictable_pgs_cleared */
    { &proc_vmstat.unevictable_pgs_cleared,
    {PMDA_PMID(28,78), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.unevictable_pgs_culled */
    { &proc_vmstat.unevictable_pgs_culled,
    {PMDA_PMID(28,79), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.unevictable_pgs_mlocked */
    { &proc_vmstat.unevictable_pgs_mlocked,
    {PMDA_PMID(28,80), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.unevictable_pgs_mlockfreed */
    { &proc_vmstat.unevictable_pgs_mlockfreed,
    {PMDA_PMID(28,81), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.unevictable_pgs_munlocked */
    { &proc_vmstat.unevictable_pgs_munlocked,
    {PMDA_PMID(28,82), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.unevictable_pgs_rescued */
    { &proc_vmstat.unevictable_pgs_rescued,
    {PMDA_PMID(28,83), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.unevictable_pgs_scanned */
    { &proc_vmstat.unevictable_pgs_scanned,
    {PMDA_PMID(28,84), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.unevictable_pgs_stranded */
    { &proc_vmstat.unevictable_pgs_stranded,
    {PMDA_PMID(28,85), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.zone_reclaim_failed */
    { &proc_vmstat.zone_reclaim_failed,
    {PMDA_PMID(28,86), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.kswapd_low_wmark_hit_quickly */
    { &proc_vmstat.kswapd_low_wmark_hit_quickly,
    {PMDA_PMID(28,87), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.kswapd_high_wmark_hit_quickly */
    { &proc_vmstat.kswapd_high_wmark_hit_quickly,
    {PMDA_PMID(28,88), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.kswapd_skip_congestion_wait */
    { &proc_vmstat.kswapd_skip_congestion_wait,
    {PMDA_PMID(28,89), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.nr_anon_transparent_hugepages */
    { &proc_vmstat.nr_anon_transparent_hugepages,
    {PMDA_PMID(28,90), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.nr_dirtied */
    { &proc_vmstat.nr_dirtied,
    {PMDA_PMID(28,91), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.nr_dirty_background_threshold */
    { &proc_vmstat.nr_dirty_background_threshold,
    {PMDA_PMID(28,92), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.nr_dirty_threshold */
    { &proc_vmstat.nr_dirty_threshold,
    {PMDA_PMID(28,93), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_INSTANT,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.nr_written */
    { &proc_vmstat.nr_written,
    {PMDA_PMID(28,94), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.numa_foreign */
    { &proc_vmstat.numa_foreign,
    {PMDA_PMID(28,95), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.numa_hit */
    { &proc_vmstat.numa_hit,
    {PMDA_PMID(28,96), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.numa_interleave */
    { &proc_vmstat.numa_interleave,
    {PMDA_PMID(28,97), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.numa_local */
    { &proc_vmstat.numa_local,
    {PMDA_PMID(28,98), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.numa_miss */
    { &proc_vmstat.numa_miss,
    {PMDA_PMID(28,99), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* mem.vmstat.numa_other */
    { &proc_vmstat.numa_other,
    {PMDA_PMID(28,100), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
    PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },


/* deprecated: network.ib.in.bytes, use infiniband.* */
    { NULL, 
      { PMDA_PMID(CLUSTER_IB,0), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0) }, },

/* deprecated: network.ib.in.packets, use infiniband.* */
    { NULL, 
      { PMDA_PMID(CLUSTER_IB,1), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* deprecated: network.ib.in.errors.drop, use infiniband.* */
    { NULL,
      { PMDA_PMID(CLUSTER_IB,2), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* deprecated: network.ib.in.errors.filter, use infiniband.* */
    { NULL,
      { PMDA_PMID(CLUSTER_IB,3), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* deprecated: network.ib.in.errors.local, use infiniband.* */
    { NULL,
      { PMDA_PMID(CLUSTER_IB,4), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* deprecated: network.ib.in.errors.remote, use infiniband.* */
    { NULL,
      { PMDA_PMID(CLUSTER_IB,5), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* deprecated: network.ib.out.bytes, use infiniband.* */
    { NULL, 
      { PMDA_PMID(CLUSTER_IB,6), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_BYTE,0,0) }, },

/* deprecated: network.ib.out.packets, use infiniband.* */
    { NULL, 
      { PMDA_PMID(CLUSTER_IB,7), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* deprecated: network.ib.out.errors.drop, use infiniband.* */
    { NULL,
      { PMDA_PMID(CLUSTER_IB,8), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* deprecated: network.ib.out.errors.filter, use infiniband.* */
    { NULL,
      { PMDA_PMID(CLUSTER_IB,9), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* deprecated: network.ib.total.bytes, use infiniband.* */
    { NULL, 
      { PMDA_PMID(CLUSTER_IB,16), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(1,0,0,0,PM_SPACE_BYTE,0) }, },

/* deprecated: network.ib.total.packets, use infiniband.* */
    { NULL, 
      { PMDA_PMID(CLUSTER_IB,17), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER, 
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* deprecated: network.ib.total.errors.drop, use infiniband.* */
    { NULL,
      { PMDA_PMID(CLUSTER_IB,18), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* deprecated: network.ib.total.errors.filter, use infiniband.* */
    { NULL,
      { PMDA_PMID(CLUSTER_IB,19), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* deprecated: network.ib.total.errors.link, use infiniband.* */
    { NULL,
      { PMDA_PMID(CLUSTER_IB,10), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* deprecated: network.ib.total.errors.recover, use infiniband.* */
    { NULL,
      { PMDA_PMID(CLUSTER_IB,11), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* deprecated: network.ib.total.errors.integrity, use infiniband.* */
    { NULL,
      { PMDA_PMID(CLUSTER_IB,12), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* deprecated: network.ib.total.errors.vl15, use infiniband.* */
    { NULL,
      { PMDA_PMID(CLUSTER_IB,13), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* deprecated: network.ib.total.errors.overrun, use infiniband.* */
    { NULL,
      { PMDA_PMID(CLUSTER_IB,14), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* deprecated: network.ib.total.errors.symbol, use infiniband.* */
    { NULL,
      { PMDA_PMID(CLUSTER_IB,15), PM_TYPE_U64, PM_INDOM_NULL, PM_SEM_COUNTER,
      PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/* deprecated: network.ib.status, use infiniband.* */
    { NULL, 
      { PMDA_PMID(CLUSTER_IB,20), PM_TYPE_STRING, PM_INDOM_NULL, PM_SEM_INSTANT,
      PMDA_PMUNITS(0,0,0,0,0,0) }, },

/* deprecated: network.ib.control, use infiniband.* */
    { NULL,
      { PMDA_PMID(CLUSTER_IB,21), PM_TYPE_U32, PM_INDOM_NULL, PM_SEM_DISCRETE,
      PMDA_PMUNITS(0,0,0,0,0,0) }, },

/*
 * quota cluster
 */
/* quota.state.project.accounting */
    { NULL,
      { PMDA_PMID(CLUSTER_QUOTA,0), PM_TYPE_U32, FILESYS_INDOM, PM_SEM_DISCRETE,
      PMDA_PMUNITS(0,0,0,0,0,0) }, },

/* quota.state.project.enforcement */
    { NULL,
      { PMDA_PMID(CLUSTER_QUOTA,1), PM_TYPE_U32, FILESYS_INDOM, PM_SEM_DISCRETE,
      PMDA_PMUNITS(0,0,0,0,0,0) }, },

/* quota.project.space.hard */
    { NULL, 
      { PMDA_PMID(CLUSTER_QUOTA,6), PM_TYPE_U64, QUOTA_PRJ_INDOM, PM_SEM_DISCRETE, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* quota.project.space.soft */
    { NULL, 
      { PMDA_PMID(CLUSTER_QUOTA,7), PM_TYPE_U64, QUOTA_PRJ_INDOM, PM_SEM_DISCRETE, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* quota.project.space.used */
    { NULL, 
      { PMDA_PMID(CLUSTER_QUOTA,8), PM_TYPE_U64, QUOTA_PRJ_INDOM, PM_SEM_DISCRETE, 
      PMDA_PMUNITS(1,0,0,PM_SPACE_KBYTE,0,0) }, },

/* quota.project.space.time_left */
    { NULL, 
      { PMDA_PMID(CLUSTER_QUOTA,9), PM_TYPE_32, QUOTA_PRJ_INDOM, PM_SEM_DISCRETE, 
      PMDA_PMUNITS(0,1,0,0,PM_TIME_SEC,0) }, },

/* quota.project.files.hard */
    { NULL, 
      { PMDA_PMID(CLUSTER_QUOTA,10), PM_TYPE_U64, QUOTA_PRJ_INDOM, PM_SEM_DISCRETE, 
	PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* quota.project.files.soft */
    { NULL, 
      { PMDA_PMID(CLUSTER_QUOTA,11), PM_TYPE_U64, QUOTA_PRJ_INDOM, PM_SEM_DISCRETE, 
	PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* quota.project.files.used */
    { NULL, 
      { PMDA_PMID(CLUSTER_QUOTA,12), PM_TYPE_U64, QUOTA_PRJ_INDOM, PM_SEM_DISCRETE, 
	PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) } },

/* quota.project.files.time_left */
    { NULL, 
      { PMDA_PMID(CLUSTER_QUOTA,13), PM_TYPE_32, QUOTA_PRJ_INDOM, PM_SEM_DISCRETE, 
      PMDA_PMUNITS(0,1,0,0,PM_TIME_SEC,0) }, },

/*
 * sysfs_kernel cluster
 */
    /* sysfs.kernel.uevent_seqnum */
    { &sysfs_kernel.uevent_seqnum,
      { PMDA_PMID(CLUSTER_SYSFS_KERNEL,0), PM_TYPE_U64, PM_INDOM_NULL,
	PM_SEM_COUNTER, PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

/*
 * /proc/interrupts cluster
 */
    /* kernel.all.interrupts.errors */
    { &irq_err_count,
      { PMDA_PMID(CLUSTER_INTERRUPTS, 3), PM_TYPE_U32, PM_INDOM_NULL,
	PM_SEM_COUNTER, PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* kernel.percpu.interrupts.line[<N>] */
    { NULL, { PMDA_PMID(CLUSTER_INTERRUPT_LINES, 0), PM_TYPE_U32,
    CPU_INDOM, PM_SEM_COUNTER, PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

    /* kernel.percpu.interrupts.[<other>] */
    { NULL, { PMDA_PMID(CLUSTER_INTERRUPT_OTHER, 0), PM_TYPE_U32,
    CPU_INDOM, PM_SEM_COUNTER, PMDA_PMUNITS(0,0,1,0,0,PM_COUNT_ONE) }, },

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

int
refresh_cgroups(pmdaExt *pmda, __pmnsTree **tree)
{
    int changed;
    time_t rightnow;
    static time_t previoustime;
    static __pmnsTree *previoustree;

    if (tree) {
	if ((rightnow = time(NULL)) == previoustime) {
	    *tree = previoustree;
	    return 0;
	}
    }

    refresh_filesys(INDOM(FILESYS_INDOM), INDOM(QUOTA_PRJ_INDOM),
		    INDOM(TMPFS_INDOM), INDOM(CGROUP_MOUNTS_INDOM));
    refresh_cgroup_subsys(INDOM(CGROUP_SUBSYS_INDOM));
    changed = refresh_cgroup_groups(pmda, INDOM(CGROUP_MOUNTS_INDOM), tree);

    if (tree) {
	previoustime = rightnow;
	previoustree = *tree;
    }
    return changed;
}

static void
linux_refresh(pmdaExt *pmda, int *need_refresh)
{
    int need_refresh_mtab = 0;

    if (need_refresh[CLUSTER_PARTITIONS])
    	refresh_proc_partitions(INDOM(DISK_INDOM), INDOM(PARTITIONS_INDOM));

    if (need_refresh[CLUSTER_STAT])
    	refresh_proc_stat(&proc_cpuinfo, &proc_stat);

    if (need_refresh[CLUSTER_CPUINFO])
    	refresh_proc_cpuinfo(&proc_cpuinfo);

    if (need_refresh[CLUSTER_MEMINFO])
	refresh_proc_meminfo(&proc_meminfo);

    if (need_refresh[CLUSTER_NUMA_MEMINFO])
	refresh_numa_meminfo(&numa_meminfo);

    if (need_refresh[CLUSTER_LOADAVG])
	refresh_proc_loadavg(&proc_loadavg);

    if (need_refresh[CLUSTER_NET_DEV])
	refresh_proc_net_dev(INDOM(NET_DEV_INDOM));

    if (need_refresh[CLUSTER_NET_INET])
	refresh_net_dev_inet(INDOM(NET_INET_INDOM));

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
    else
    if (need_refresh[CLUSTER_FILESYS] ||
	need_refresh[CLUSTER_QUOTA] || need_refresh[CLUSTER_TMPFS])
	refresh_filesys(INDOM(FILESYS_INDOM), INDOM(QUOTA_PRJ_INDOM),
			INDOM(TMPFS_INDOM), INDOM(CGROUP_MOUNTS_INDOM));

    if (need_refresh[CLUSTER_INTERRUPTS] ||
	need_refresh[CLUSTER_INTERRUPT_LINES] ||
	need_refresh[CLUSTER_INTERRUPT_OTHER])
	need_refresh_mtab |= refresh_interrupt_values();

    if (need_refresh[CLUSTER_SWAPDEV])
	refresh_swapdev(INDOM(SWAPDEV_INDOM));

    if (need_refresh[CLUSTER_NET_NFS])
	refresh_proc_net_rpc(&proc_net_rpc);

    if (need_refresh[CLUSTER_NET_SOCKSTAT])
    	refresh_proc_net_sockstat(&proc_net_sockstat);

    if (need_refresh[CLUSTER_PID_STAT] || need_refresh[CLUSTER_PID_STATM] || 
	need_refresh[CLUSTER_PID_STATUS] || need_refresh[CLUSTER_PID_IO] ||
	need_refresh[CLUSTER_PID_SCHEDSTAT] || need_refresh[CLUSTER_PID_FD])
	refresh_proc_pid(&proc_pid);

    if (need_refresh[CLUSTER_KERNEL_UNAME])
    	uname(&kernel_uname);

    if (need_refresh[CLUSTER_PROC_RUNQ])
	refresh_proc_runq(&proc_runq);

    if (need_refresh[CLUSTER_NET_SNMP])
	refresh_proc_net_snmp(&proc_net_snmp);

    if (need_refresh[CLUSTER_SCSI])
	refresh_proc_scsi(&proc_scsi);

    if (need_refresh[CLUSTER_XFS] || need_refresh[CLUSTER_XFSBUF])
    	refresh_proc_fs_xfs(&proc_fs_xfs);

    if (need_refresh[CLUSTER_NET_TCP])
	refresh_proc_net_tcp(&proc_net_tcp);

    if (need_refresh[CLUSTER_SLAB])
	refresh_proc_slabinfo(&proc_slabinfo);

    if (need_refresh[CLUSTER_SEM_LIMITS])
        refresh_sem_limits(&sem_limits);

    if (need_refresh[CLUSTER_MSG_LIMITS])
        refresh_msg_limits(&msg_limits);

    if (need_refresh[CLUSTER_SHM_LIMITS])
        refresh_shm_limits(&shm_limits);

    if (need_refresh[CLUSTER_UPTIME])
        refresh_proc_uptime(&proc_uptime);

    if (need_refresh[CLUSTER_VFS])
    	refresh_proc_sys_fs(&proc_sys_fs);

    if (need_refresh[CLUSTER_VMSTAT])
    	refresh_proc_vmstat(&proc_vmstat);

    if (need_refresh[CLUSTER_SYSFS_KERNEL])
    	refresh_sysfs_kernel(&sysfs_kernel);

    if (need_refresh_mtab)
	linux_dynamic_metrictable(pmda);
}

static int
linux_instance(pmInDom indom, int inst, char *name, __pmInResult **result, pmdaExt *pmda)
{
    __pmInDom_int	*indomp = (__pmInDom_int *)&indom;
    int			need_refresh[NUM_CLUSTERS];
    char		newname[11];		/* see Note below */

    memset(need_refresh, 0, sizeof(need_refresh));
    switch (indomp->serial) {
    case DISK_INDOM:
    case PARTITIONS_INDOM:
	need_refresh[CLUSTER_PARTITIONS]++;
	break;
    case CPU_INDOM:
    	need_refresh[CLUSTER_STAT]++;
	break;
    case NODE_INDOM:
	need_refresh[CLUSTER_NUMA_MEMINFO]++;
	break;
    case LOADAVG_INDOM:
    	need_refresh[CLUSTER_LOADAVG]++;
	break;
    case NET_DEV_INDOM:
    	need_refresh[CLUSTER_NET_DEV]++;
	break;
    case FILESYS_INDOM:
    	need_refresh[CLUSTER_FILESYS]++;
	break;
    case TMPFS_INDOM:
    	need_refresh[CLUSTER_TMPFS]++;
	break;
    case QUOTA_PRJ_INDOM:
    	need_refresh[CLUSTER_QUOTA]++;
	break;
    case SWAPDEV_INDOM:
    	need_refresh[CLUSTER_SWAPDEV]++;
	break;
    case NFS_INDOM:
    case NFS3_INDOM:
    case NFS4_CLI_INDOM:
    case NFS4_SVR_INDOM:
    	need_refresh[CLUSTER_NET_NFS]++;
	break;
    case PROC_INDOM:
    	need_refresh[CLUSTER_PID_STAT]++;
    	need_refresh[CLUSTER_PID_STATM]++;
        need_refresh[CLUSTER_PID_STATUS]++;
        need_refresh[CLUSTER_PID_SCHEDSTAT]++;
        need_refresh[CLUSTER_PID_IO]++;
        need_refresh[CLUSTER_PID_FD]++;
	break;
    case SCSI_INDOM:
    	need_refresh[CLUSTER_SCSI]++;
	break;
    case SLAB_INDOM:
    	need_refresh[CLUSTER_SLAB]++;
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
	    if (!isdigit(*p))
	    	break;
	}
	if (*p == '\0') {
	    snprintf(newname, sizeof(newname), "%06d", atoi(name));
	    name = newname;
	}
    }

    linux_refresh(pmda, need_refresh);
    return pmdaInstance(indom, inst, name, result, pmda);
}

/*
 * callback provided to pmdaFetch
 */

static int
linux_fetchCallBack(pmdaMetric *mdesc, unsigned int inst, pmAtomValue *atom)
{
    __pmID_int		*idp = (__pmID_int *)&(mdesc->m_desc.pmid);
    int			i;
    int			sts;
    char		*f;
    long		sl;
    unsigned long	ul;
    struct filesys	*fs;
    proc_pid_entry_t	*entry;
    net_inet_t		*inetp;
    net_interface_t	*netip;

    if (mdesc->m_user != NULL) {
	/* 
	 * The metric value is extracted directly via the address specified
	 * in metrictab.  Note: not all metrics support this - those that
	 * don't have NULL for the m_user field in their respective
         * metrictab slot.
	 */
	if (idp->cluster == CLUSTER_VMSTAT) {
	    if (!_pm_have_proc_vmstat || *(__uint64_t *)mdesc->m_user == (__uint64_t)-1)
	    	return 0; /* no value available on this kernel */
	}
	else
	if (idp->cluster == CLUSTER_NET_NFS) {
	    /*
	     * check if rpc stats are available
	     */
	    if (idp->item >= 20 && idp->item <= 27 && proc_net_rpc.client.errcode != 0)
		/* no values available for client rpc/nfs - this is expected <= 2.0.36 */
	    	return 0;
	    else
	    if (idp->item >= 30 && idp->item <= 47 && proc_net_rpc.server.errcode != 0)
		/* no values available - expected without /proc/net/rpc/nfsd */
	    	return 0; /* no values available */
	    if (idp->item >= 51 && idp->item <= 57 && proc_net_rpc.server.errcode != 0)
		/* no values available - expected without /proc/net/rpc/nfsd */
	    	return 0; /* no values available */
	}
	else
	if ((idp->cluster == CLUSTER_XFS || idp->cluster == CLUSTER_XFSBUF) &&
	    proc_fs_xfs.errcode != 0) {
	    /* no values available for XFS metrics */
	    return 0;
	}
	else
	if (idp->cluster == CLUSTER_SYSFS_KERNEL) {
	    /* no values available for udev metrics */
	    if (idp->item == 0 && !sysfs_kernel.valid_uevent_seqnum) {
		return 0;
	    }
	}

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
    case CLUSTER_STAT:
	/*
	 * All metrics from /proc/stat
	 */
	switch (idp->item) {
	case 0: /* kernel.percpu.cpu.user */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.p_user[inst] / proc_stat.hz);
	    break;
	case 1: /* kernel.percpu.cpu.nice */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.p_nice[inst] / proc_stat.hz);
	    break;
	case 2: /* kernel.percpu.cpu.sys */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.p_sys[inst] / proc_stat.hz);
	    break;
	case 3: /* kernel.percpu.cpu.idle */
	    _pm_assign_utype(_pm_idletime_size, atom,
			1000 * (double)proc_stat.p_idle[inst] / proc_stat.hz);
	    break;
	case 30: /* kernel.percpu.cpu.wait.total */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.p_wait[inst] / proc_stat.hz);
	    break;
	case 31: /* kernel.percpu.cpu.intr */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * ((double)proc_stat.p_irq[inst] +
				(double)proc_stat.p_sirq[inst]) / proc_stat.hz);
	    break;
	case 56: /* kernel.percpu.cpu.irq.soft */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.p_sirq[inst] / proc_stat.hz);
	    break;
	case 57: /* kernel.percpu.cpu.irq.hard */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.p_irq[inst] / proc_stat.hz);
	    break;
	case 58: /* kernel.percpu.cpu.steal */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.p_steal[inst] / proc_stat.hz);
	    break;
	case 61: /* kernel.percpu.cpu.guest */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.p_guest[inst] / proc_stat.hz);
	    break;


	case 62: /* kernel.pernode.cpu.user */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.n_user[inst] / proc_stat.hz);
	    break;
	case 63: /* kernel.pernode.cpu.nice */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.n_nice[inst] / proc_stat.hz);
	    break;
	case 64: /* kernel.pernode.cpu.sys */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.n_sys[inst] / proc_stat.hz);
	    break;
	case 65: /* kernel.pernode.cpu.idle */
	    _pm_assign_utype(_pm_idletime_size, atom,
			1000 * (double)proc_stat.n_idle[inst] / proc_stat.hz);
	    break;
	case 69: /* kernel.pernode.cpu.wait.total */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.n_wait[inst] / proc_stat.hz);
	    break;
	case 66: /* kernel.pernode.cpu.intr */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * ((double)proc_stat.n_irq[inst] +
				(double)proc_stat.n_sirq[inst]) / proc_stat.hz);
	    break;
	case 70: /* kernel.pernode.cpu.irq.soft */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.n_sirq[inst] / proc_stat.hz);
	    break;
	case 71: /* kernel.pernode.cpu.irq.hard */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.n_irq[inst] / proc_stat.hz);
	    break;
	case 67: /* kernel.pernode.cpu.steal */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.n_steal[inst] / proc_stat.hz);
	    break;
	case 68: /* kernel.pernode.cpu.guest */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.n_guest[inst] / proc_stat.hz);
	    break;


	case 8: /* pagesin */
	    if (_pm_have_proc_vmstat)
		atom->ul = proc_vmstat.pswpin;
	    else
		atom->ul = proc_stat.swap[0];
	    break;
	case 9: /* pagesout */
	    if (_pm_have_proc_vmstat)
		atom->ul = proc_vmstat.pswpout;
	    else
		atom->ul = proc_stat.swap[1];
	    break;
	case 10: /* in */
	    if (_pm_have_proc_vmstat)
		return PM_ERR_APPVERSION; /* no swap operation counts in 2.6 */
	    else
		atom->ul = proc_stat.page[0];
	    break;
	case 11: /* out */
	    if (_pm_have_proc_vmstat)
		return PM_ERR_APPVERSION; /* no swap operation counts in 2.6 */
	    else
		atom->ul = proc_stat.page[1];
	    break;
	case 12: /* intr */
	    _pm_assign_utype(_pm_intr_size, atom, proc_stat.intr);
	    break;
	case 13: /* ctxt */
	    _pm_assign_utype(_pm_ctxt_size, atom, proc_stat.ctxt);
	    break;
	case 14: /* processes */
	    _pm_assign_ulong(atom, proc_stat.processes);
	    break;

	case 20: /* kernel.all.cpu.user */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.user / proc_stat.hz);
	    break;
	case 21: /* kernel.all.cpu.nice */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.nice / proc_stat.hz);
	    break;
	case 22: /* kernel.all.cpu.sys */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.sys / proc_stat.hz);
	    break;
	case 23: /* kernel.all.cpu.idle */
	    _pm_assign_utype(_pm_idletime_size, atom,
			1000 * (double)proc_stat.idle / proc_stat.hz);
	    break;
	case 34: /* kernel.all.cpu.intr */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * ((double)proc_stat.irq +
			      	(double)proc_stat.sirq) / proc_stat.hz);
	    break;
	case 35: /* kernel.all.cpu.wait.total */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.wait / proc_stat.hz);
	    break;
	case 53: /* kernel.all.cpu.irq.soft */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.sirq / proc_stat.hz);
	    break;
	case 54: /* kernel.all.cpu.irq.hard */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.irq / proc_stat.hz);
	    break;
	case 55: /* kernel.all.cpu.steal */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.steal / proc_stat.hz);
	    break;
	case 60: /* kernel.all.cpu.guest */
	    _pm_assign_utype(_pm_cputime_size, atom,
			1000 * (double)proc_stat.guest / proc_stat.hz);
	    break;

	case 32: /* hinv.ncpu */
	    atom->ul = indomtab[CPU_INDOM].it_numinst;
	    break;
	case 33: /* hinv.ndisk */
	    atom->ul = pmdaCacheOp(INDOM(DISK_INDOM), PMDA_CACHE_SIZE_ACTIVE);
	    break;

	case 48: /* kernel.all.hz */
	    atom->ul = proc_stat.hz;
	    break;

	default:
	    /*
	     * Disk metrics used to be fetched from /proc/stat (2.2 kernels)
	     * but have since moved to /proc/partitions (2.4 kernels) and
	     * /proc/diskstats (2.6 kernels). We preserve the cluster number
	     * (middle bits of a PMID) for backward compatibility.
	     *
	     * Note that proc_partitions_fetch() will return PM_ERR_PMID
	     * if we have tried to fetch an unknown metric.
	     */
	    return proc_partitions_fetch(mdesc, inst, atom);
	}
	break;

    case CLUSTER_UPTIME:  /* uptime */
	switch (idp->item) {
	case 0:
	    /*
	     * kernel.all.uptime (in seconds)
	     * contributed by "gilly" <gilly@exanet.com>
	     * modified by Mike Mason" <mmlnx@us.ibm.com>
	     */
	    atom->ul = proc_uptime.uptime;
	    break;
	case 1:
	    /*
	     * kernel.all.idletime (in seconds)
	     * contributed by "Mike Mason" <mmlnx@us.ibm.com>
	     */
	    atom->ul = proc_uptime.idletime;
	    break;
	default:
	    return PM_ERR_PMID;
	}
	break;

    case CLUSTER_MEMINFO: /* mem */

#define VALID_VALUE(x)		((x) != (uint64_t)-1)
#define VALUE_OR_ZERO(x)	(((x) == (uint64_t)-1) ? 0 : (x))

    	switch (idp->item) {
	case 0: /* mem.physmem (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.MemTotal))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.MemTotal >> 10;
	    break;
	case 1: /* mem.util.used (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.MemTotal) ||
	        !VALID_VALUE(proc_meminfo.MemFree))
		return 0; /* no values available */
	    atom->ull = (proc_meminfo.MemTotal - proc_meminfo.MemFree) >> 10;
	    break;
	case 2: /* mem.util.free (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.MemFree))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.MemFree >> 10;
	    break;
	case 3: /* mem.util.shared (in kbytes) */
	    /*
	     * If this metric is exported by the running kernel, it is always
	     * zero (deprecated). PCP exports it for compatibility with older
	     * PCP monitoring tools, e.g. pmgsys running on IRIX(TM).
	     */
	    if (!VALID_VALUE(proc_meminfo.MemShared))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.MemShared >> 10;
	    break;
	case 4: /* mem.util.bufmem (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.Buffers))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.Buffers >> 10;
	    break;
	case 5: /* mem.util.cached (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.Cached))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.Cached >> 10;
	    break;
	case 6: /* swap.length (in bytes) */
	    if (!VALID_VALUE(proc_meminfo.SwapTotal))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.SwapTotal;
	    break;
	case 7: /* swap.used (in bytes) */
	    if (!VALID_VALUE(proc_meminfo.SwapTotal) ||
	        !VALID_VALUE(proc_meminfo.SwapFree))
		return 0; /* no values available */
	    atom->ull = proc_meminfo.SwapTotal - proc_meminfo.SwapFree;
	    break;
	case 8: /* swap.free (in bytes) */
	    if (!VALID_VALUE(proc_meminfo.SwapFree))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.SwapFree;
	    break;
	case 9: /* hinv.physmem (in mbytes) */
	    if (!VALID_VALUE(proc_meminfo.MemTotal))
	    	return 0; /* no values available */
	    atom->ul = proc_meminfo.MemTotal >> 20;
	    break;
	case 10: /* mem.freemem (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.MemFree))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.MemFree >> 10;
	    break;
	case 11: /* hinv.pagesize (in bytes) */
	    atom->ul = _pm_system_pagesize;
	    break;
	case 12: /* mem.util.other (in kbytes) */
	    /* other = used - (cached+buffers) */
	    if (!VALID_VALUE(proc_meminfo.MemTotal) ||
	        !VALID_VALUE(proc_meminfo.MemFree) ||
	        !VALID_VALUE(proc_meminfo.Cached) ||
	        !VALID_VALUE(proc_meminfo.Buffers))
		return 0; /* no values available */
	    sl = (proc_meminfo.MemTotal -
		 proc_meminfo.MemFree -
		 proc_meminfo.Cached -
		 proc_meminfo.Buffers) >> 10;
	    atom->ull = sl >= 0 ? sl : 0;
	    break;
	case 13: /* mem.util.swapCached (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.SwapCached))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.SwapCached >> 10;
	    break;
	case 14: /* mem.util.active (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.Active))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.Active >> 10;
	    break;
	case 15: /* mem.util.inactive (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.Inactive))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.Inactive >> 10;
	    break;
	case 16: /* mem.util.highTotal (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.HighTotal))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.HighTotal >> 10;
	    break;
	case 17: /* mem.util.highFree (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.HighFree))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.HighFree >> 10;
	    break;
	case 18: /* mem.util.lowTotal (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.LowTotal))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.LowTotal >> 10;
	    break;
	case 19: /* mem.util.lowFree (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.LowFree))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.LowFree >> 10;
	    break;
	case 20: /* mem.util.swapTotal (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.SwapTotal))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.SwapTotal >> 10;
	    break;
	case 21: /* mem.util.swapFree (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.SwapFree))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.SwapFree >> 10;
	    break;
	case 22: /* mem.util.dirty (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.Dirty))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.Dirty >> 10;
	    break;
	case 23: /* mem.util.writeback (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.Writeback))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.Writeback >> 10;
	    break;
	case 24: /* mem.util.mapped (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.Mapped))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.Mapped >> 10;
	    break;
	case 25: /* mem.util.slab (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.Slab))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.Slab >> 10;
	    break;
	case 26: /* mem.util.committed_AS (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.Committed_AS))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.Committed_AS >> 10;
	    break;
	case 27: /* mem.util.pageTables (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.PageTables))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.PageTables >> 10;
	    break;
	case 28: /* mem.util.reverseMaps (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.ReverseMaps))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.ReverseMaps >> 10;
	    break;
	case 29: /* mem.util.clean_cache (in kbytes) */
	    /* clean=cached-(dirty+writeback) */
	    if (!VALID_VALUE(proc_meminfo.Cached) ||
	        !VALID_VALUE(proc_meminfo.Dirty) ||
	        !VALID_VALUE(proc_meminfo.Writeback))
	    	return 0; /* no values available */
	    sl = (proc_meminfo.Cached -
	    	 proc_meminfo.Dirty -
	    	 proc_meminfo.Writeback) >> 10;
	    atom->ull = sl >= 0 ? sl : 0;
	    break;
	case 30: /* mem.util.anonpages */
	   if (!VALID_VALUE(proc_meminfo.AnonPages))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.AnonPages >> 10;
	   break;
	case 31: /* mem.util.commitLimit (in kbytes) */
	    if (!VALID_VALUE(proc_meminfo.CommitLimit))
	    	return 0; /* no values available */
	    atom->ull = proc_meminfo.CommitLimit >> 10;
	    break;
	case 32: /* mem.util.bounce */
	   if (!VALID_VALUE(proc_meminfo.Bounce))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.Bounce >> 10;
	   break;
	case 33: /* mem.util.NFS_Unstable */
	   if (!VALID_VALUE(proc_meminfo.NFS_Unstable))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.NFS_Unstable >> 10;
	   break;
	case 34: /* mem.util.slabReclaimable */
	   if (!VALID_VALUE(proc_meminfo.SlabReclaimable))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.SlabReclaimable >> 10;
	   break;
	case 35: /* mem.util.slabUnreclaimable */
	   if (!VALID_VALUE(proc_meminfo.SlabUnreclaimable))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.SlabUnreclaimable >> 10;
	   break;
	case 36: /* mem.util.active_anon */
	   if (!VALID_VALUE(proc_meminfo.Active_anon))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.Active_anon >> 10;
	   break;
	case 37: /* mem.util.inactive_anon */
	   if (!VALID_VALUE(proc_meminfo.Inactive_anon))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.Inactive_anon >> 10;
	   break;
	case 38: /* mem.util.active_file */
	   if (!VALID_VALUE(proc_meminfo.Active_file))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.Active_file >> 10;
	   break;
	case 39: /* mem.util.inactive_file */
	   if (!VALID_VALUE(proc_meminfo.Inactive_file))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.Inactive_file >> 10;
	   break;
	case 40: /* mem.util.unevictable */
	   if (!VALID_VALUE(proc_meminfo.Unevictable))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.Unevictable >> 10;
	   break;
	case 41: /* mem.util.mlocked */
	   if (!VALID_VALUE(proc_meminfo.Mlocked))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.Mlocked >> 10;
	   break;
	case 42: /* mem.util.shmem */
	   if (!VALID_VALUE(proc_meminfo.Shmem))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.Shmem >> 10;
	   break;
	case 43: /* mem.util.kernelStack */
	   if (!VALID_VALUE(proc_meminfo.KernelStack))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.KernelStack >> 10;
	   break;
	case 44: /* mem.util.hugepagesTotal */
	   if (!VALID_VALUE(proc_meminfo.HugepagesTotal))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.HugepagesTotal;
	   break;
	case 45: /* mem.util.hugepagesFree */
	   if (!VALID_VALUE(proc_meminfo.HugepagesFree))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.HugepagesFree;
	   break;
	case 46: /* mem.util.hugepagesRsvd */
	   if (!VALID_VALUE(proc_meminfo.HugepagesRsvd))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.HugepagesRsvd;
	   break;
	case 47: /* mem.util.hugepagesSurp */
	   if (!VALID_VALUE(proc_meminfo.HugepagesSurp))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.HugepagesSurp;
	   break;
	case 48: /* mem.util.directMap4k */
	   if (!VALID_VALUE(proc_meminfo.directMap4k))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.directMap4k >> 10;
	   break;
	case 49: /* mem.util.directMap2M */
	   if (!VALID_VALUE(proc_meminfo.directMap2M))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.directMap2M >> 10;
	   break;
	case 50: /* mem.util.vmallocTotal */
	   if (!VALID_VALUE(proc_meminfo.VmallocTotal))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.VmallocTotal >> 10;
	   break;
	case 51: /* mem.util.vmallocUsed */
	   if (!VALID_VALUE(proc_meminfo.VmallocUsed))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.VmallocUsed >> 10;
	   break;
	case 52: /* mem.util.vmallocChunk */
	   if (!VALID_VALUE(proc_meminfo.VmallocChunk))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.VmallocChunk >> 10;
	   break;
	case 53: /* mem.util.mmap_copy */
	   if (!VALID_VALUE(proc_meminfo.MmapCopy))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.MmapCopy >> 10;
	   break;
	case 54: /* mem.util.quicklists */
	   if (!VALID_VALUE(proc_meminfo.Quicklists))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.Quicklists >> 10;
	   break;
	case 55: /* mem.util.corrupthardware */
	   if (!VALID_VALUE(proc_meminfo.HardwareCorrupted))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.HardwareCorrupted >> 10;
	   break;
	case 56: /* mem.util.anonhugepages */
	   if (!VALID_VALUE(proc_meminfo.AnonHugePages))
		return 0; /* no values available */
	   atom->ull = proc_meminfo.AnonHugePages >> 10;
	   break;
	default:
	    return PM_ERR_PMID;
	}
	break;

    case CLUSTER_LOADAVG: 
	switch(idp->item) {
	case 0:  /* kernel.all.load */
	    if (inst == 1)
	    	atom->f = proc_loadavg.loadavg[0];
	    else
	    if (inst == 5)
	    	atom->f = proc_loadavg.loadavg[1];
	    else
	    if (inst == 15)
	    	atom->f = proc_loadavg.loadavg[2];
	    else
	    	return PM_ERR_INST;
	    break;
	case 1: /* kernel.all.lastpid -- added by "Mike Mason" <mmlnx@us.ibm.com> */
		atom->ul = proc_loadavg.lastpid;
		break;
	case 2: /* kernel.all.runnable */
		atom->ul = proc_loadavg.runnable;
		break;
	case 3: /* kernel.all.nprocs */
		atom->ul = proc_loadavg.nprocs;
		break;
	default:
	    return PM_ERR_PMID;
	}
	break;

    case CLUSTER_NET_DEV: /* network.interface */
	sts = pmdaCacheLookup(INDOM(NET_DEV_INDOM), inst, NULL, (void **)&netip);
	if (sts < 0)
	    return sts;
	if (idp->item >= 0 && idp->item <= 15) {
	    /* network.interface.{in,out} */
	    atom->ull = netip->counters[idp->item];
	}
	else
	switch (idp->item) {
	case 16: /* network.interface.total.bytes */
	    atom->ull = netip->counters[0] + netip->counters[8];
	    break;
	case 17: /* network.interface.total.packets */
	    atom->ull = netip->counters[1] + netip->counters[9];
	    break;
	case 18: /* network.interface.total.errors */
	    atom->ull = netip->counters[2] + netip->counters[10];
	    break;
	case 19: /* network.interface.total.drops */
	    atom->ull = netip->counters[3] + netip->counters[11];
	    break;
	case 20: /* network.interface.total.mcasts */
	    /*
	     * NOTE: there is no network.interface.out.mcasts metric
	     * so this total only includes network.interface.in.mcasts
	     */
	    atom->ull = netip->counters[7];
	    break;
	case 21: /* network.interface.mtu */
	    if (!netip->ioc.mtu)
		return 0;
	    atom->ul = netip->ioc.mtu;
	    break;
	case 22: /* network.interface.speed */
	    if (!netip->ioc.speed)
		return 0;
	    atom->f = ((float)netip->ioc.speed * 1000000) / 8 / 1024 / 1024;
	    break;
	case 23: /* network.interface.baudrate */
	    if (!netip->ioc.speed)
		return 0;
	    atom->ul = ((long long)netip->ioc.speed * 1000000 / 8);
	    break;
	case 24: /* network.interface.duplex */
	    if (!netip->ioc.duplex)
		return 0;
	    atom->ul = netip->ioc.duplex;
	    break;
	case 25: /* network.interface.up */
	    atom->ul = netip->ioc.linkup;
	    break;
	case 26: /* network.interface.running */
	    atom->ul = netip->ioc.running;
	    break;
	default:
	    return PM_ERR_PMID;
	}
	break;

    case CLUSTER_NET_INET:
	sts = pmdaCacheLookup(INDOM(NET_INET_INDOM), inst, NULL, (void **)&inetp);
	if (sts < 0)
	    return sts;
	switch (idp->item) {
	case 0: /* network.interface.inet_addr */
	    if (!inetp->hasip)
		return 0;
	    if ((atom->cp = inet_ntoa(inetp->addr)) == NULL)
		return 0;
	    break;
	default:
	    return PM_ERR_PMID;
	}
	break;

    case CLUSTER_XFS:
	switch (idp->item) {
	case 79: /* xfs.control.reset */
	    atom->ul = 0;
	    break;
	default:
	    return PM_ERR_PMID;
	}
	break;

    case CLUSTER_FILESYS:
	if (idp->item == 0)
	    atom->ul = pmdaCacheOp(INDOM(FILESYS_INDOM), PMDA_CACHE_SIZE_ACTIVE);
	else {
	    struct statfs *sbuf;
	    __uint64_t ull, used;

	    sts = pmdaCacheLookup(INDOM(FILESYS_INDOM), inst, NULL, (void **)&fs);
	    if (sts < 0)
		return sts;
	    if (sts != PMDA_CACHE_ACTIVE)
	    	return PM_ERR_INST;

	    sbuf = &fs->stats;
	    if (!(fs->flags & FSF_FETCHED)) {
		if (statfs(fs->path, sbuf) < 0)
		    return -oserror();
		fs->flags |= FSF_FETCHED;
	    }

	    switch (idp->item) {
	    case 1: /* filesys.capacity */
	    	ull = (__uint64_t)sbuf->f_blocks;
	    	atom->ull = ull * sbuf->f_bsize / 1024;
		break;
	    case 2: /* filesys.used */
	    	used = (__uint64_t)(sbuf->f_blocks - sbuf->f_bfree);
	    	atom->ull = used * sbuf->f_bsize / 1024;
		break;
	    case 3: /* filesys.free */
	    	ull = (__uint64_t)sbuf->f_bfree;
	    	atom->ull = ull * sbuf->f_bsize / 1024;
		break;
	    case 4: /* filesys.maxfiles */
	    	atom->ul = sbuf->f_files;
		break;
	    case 5: /* filesys.usedfiles */
	    	atom->ul = sbuf->f_files - sbuf->f_ffree;
		break;
	    case 6: /* filesys.freefiles */
	    	atom->ul = sbuf->f_ffree;
		break;
	    case 7: /* filesys.mountdir */
	    	atom->cp = fs->path;
		break;
	    case 8: /* filesys.full */
		used = (__uint64_t)(sbuf->f_blocks - sbuf->f_bfree);
		ull = used + (__uint64_t)sbuf->f_bavail;
		atom->d = (100.0 * (double)used) / (double)ull;
		break;
	    case 9: /* filesys.blocksize -- added by Mike Mason <mmlnx@us.ibm.com> */
		atom->ul = sbuf->f_bsize;
		break;
	    case 10: /* filesys.avail -- added by Mike Mason <mmlnx@us.ibm.com> */
		ull = (__uint64_t)sbuf->f_bavail;
		atom->ull = ull * sbuf->f_bsize / 1024;
		break;
	    case 11: /* filesys.readonly */
	    	atom->ul = (scan_filesys_options(fs->options, "ro") != NULL);
		break;
	    default:
		return PM_ERR_PMID;
	    }
	}
	break;

    case CLUSTER_TMPFS: {
	    struct statfs *sbuf;
	    __uint64_t ull, used;

	    sts = pmdaCacheLookup(INDOM(TMPFS_INDOM), inst, NULL, (void **)&fs);
	    if (sts < 0)
		return sts;
	    if (sts != PMDA_CACHE_ACTIVE)
	    	return PM_ERR_INST;

	    sbuf = &fs->stats;
	    if (!(fs->flags & FSF_FETCHED)) {
		if (statfs(fs->path, sbuf) < 0)
		    return -oserror();
		fs->flags |= FSF_FETCHED;
	    }

	    switch (idp->item) {
	    case 1: /* tmpfs.capacity */
	    	ull = (__uint64_t)sbuf->f_blocks;
	    	atom->ull = ull * sbuf->f_bsize / 1024;
		break;
	    case 2: /* tmpfs.used */
	    	used = (__uint64_t)(sbuf->f_blocks - sbuf->f_bfree);
	    	atom->ull = used * sbuf->f_bsize / 1024;
		break;
	    case 3: /* tmpfs.free */
	    	ull = (__uint64_t)sbuf->f_bfree;
	    	atom->ull = ull * sbuf->f_bsize / 1024;
		break;
	    case 4: /* tmpfs.maxfiles */
	    	atom->ul = sbuf->f_files;
		break;
	    case 5: /* tmpfs.usedfiles */
	    	atom->ul = sbuf->f_files - sbuf->f_ffree;
		break;
	    case 6: /* tmpfs.freefiles */
	    	atom->ul = sbuf->f_ffree;
		break;
	    case 7: /* tmpfs.full */
		used = (__uint64_t)(sbuf->f_blocks - sbuf->f_bfree);
		ull = used + (__uint64_t)sbuf->f_bavail;
		atom->d = (100.0 * (double)used) / (double)ull;
		break;
	    default:
		return PM_ERR_PMID;
	    }
	}
	break;

    case CLUSTER_SWAPDEV: {
	struct swapdev *swap;

	sts = pmdaCacheLookup(INDOM(SWAPDEV_INDOM), inst, NULL, (void **)&swap);
	if (sts < 0)
	    return sts;
	if (sts != PMDA_CACHE_ACTIVE)
	    return PM_ERR_INST;

	switch (idp->item) {
	case 0: /* swapdev.free (kbytes) */
	    atom->ul = swap->size - swap->used;
	    break;
	case 1: /* swapdev.length (kbytes) */
	case 2: /* swapdev.maxswap (kbytes) */
	    atom->ul = swap->size;
	    break;
	case 3: /* swapdev.vlength (kbytes) */
	    atom->ul = 0;
	    break;
	case 4: /* swapdev.priority */
	    atom->l = swap->priority;
	    break;
	default:
	    return PM_ERR_PMID;
	}
	break;
    }

    case CLUSTER_NET_NFS:
	switch (idp->item) {
	case 1: /* nfs.client.calls */
	    if (proc_net_rpc.client.errcode != 0)
	    	return 0; /* no values available */
	    for (atom->ul=0, i=0; i < NR_RPC_COUNTERS; i++) {
		atom->ul += proc_net_rpc.client.reqcounts[i];
	    }
	    break;
	case 50: /* nfs.server.calls */
	    if (proc_net_rpc.server.errcode != 0)
	    	return 0; /* no values available */
	    for (atom->ul=0, i=0; i < NR_RPC_COUNTERS; i++) {
		atom->ul += proc_net_rpc.server.reqcounts[i];
	    }
	    break;
	case 4: /* nfs.client.reqs */
	    if (proc_net_rpc.client.errcode != 0)
	    	return 0; /* no values available */
	    if (inst >= 0 && inst < NR_RPC_COUNTERS)
		atom->ul = proc_net_rpc.client.reqcounts[inst];
	    else
	    	return PM_ERR_INST;
	    break;

	case 12: /* nfs.server.reqs */
	    if (proc_net_rpc.server.errcode != 0)
	    	return 0; /* no values available */
	    if (inst >= 0 && inst < NR_RPC_COUNTERS)
		atom->ul = proc_net_rpc.server.reqcounts[inst];
	    else
	    	return PM_ERR_INST;
	    break;

	case 60: /* nfs3.client.calls */
	    if (proc_net_rpc.client.errcode != 0)
	    	return 0; /* no values available */
	    for (atom->ul=0, i=0; i < NR_RPC3_COUNTERS; i++) {
		atom->ul += proc_net_rpc.client.reqcounts3[i];
	    }
	    break;

	case 62: /* nfs3.server.calls */
	    if (proc_net_rpc.server.errcode != 0)
	    	return 0; /* no values available */
	    for (atom->ul=0, i=0; i < NR_RPC3_COUNTERS; i++) {
		atom->ul += proc_net_rpc.server.reqcounts3[i];
	    }
	    break;

	case 61: /* nfs3.client.reqs */
	    if (proc_net_rpc.client.errcode != 0)
	    	return 0; /* no values available */
	    if (inst >= 0 && inst < NR_RPC3_COUNTERS)
		atom->ul = proc_net_rpc.client.reqcounts3[inst];
	    else
	    	return PM_ERR_INST;
	    break;

	case 63: /* nfs3.server.reqs */
	    if (proc_net_rpc.server.errcode != 0)
	    	return 0; /* no values available */
	    if (inst >= 0 && inst < NR_RPC3_COUNTERS)
		atom->ul = proc_net_rpc.server.reqcounts3[inst];
	    else
	    	return PM_ERR_INST;
	    break;

	case 64: /* nfs4.client.calls */
	    if (proc_net_rpc.client.errcode != 0)
	    	return 0; /* no values available */
	    for (atom->ul=0, i=0; i < NR_RPC4_CLI_COUNTERS; i++) {
		atom->ul += proc_net_rpc.client.reqcounts4[i];
	    }
	    break;

	case 66: /* nfs4.server.calls */
	    if (proc_net_rpc.server.errcode != 0)
	    	return 0; /* no values available */
	    for (atom->ul=0, i=0; i < NR_RPC4_SVR_COUNTERS; i++) {
		atom->ul += proc_net_rpc.server.reqcounts4[i];
	    }
	    break;

	case 65: /* nfs4.client.reqs */
	    if (proc_net_rpc.client.errcode != 0)
	    	return 0; /* no values available */
	    if (inst >= 0 && inst < NR_RPC4_CLI_COUNTERS)
		atom->ul = proc_net_rpc.client.reqcounts4[inst];
	    else
	    	return PM_ERR_INST;
	    break;

	case 67: /* nfs4.server.reqs */
	    if (proc_net_rpc.server.errcode != 0)
	    	return 0; /* no values available */
	    if (inst >= 0 && inst < NR_RPC4_SVR_COUNTERS)
		atom->ul = proc_net_rpc.server.reqcounts4[inst];
	    else
	    	return PM_ERR_INST;
	    break;

	/*
	 * Note: all other rpc metric values are extracted directly via the
	 * address specified in the metrictab (see above)
	 */
	default:
	    return PM_ERR_PMID;
	}
	break;

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
		_pm_assign_ulong(atom, 1000 * (double)ul / proc_stat.hz);
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

    case CLUSTER_SLAB:
	if (proc_slabinfo.ncaches == 0)
	    return 0; /* no values available */

	if (inst < 0 || inst >= proc_slabinfo.ncaches)
	    return PM_ERR_INST;

	switch(idp->item) {
	case 0:	/* mem.slabinfo.objects.active */
	    atom->ull = proc_slabinfo.caches[inst].num_active_objs;
	    break;
	case 1:	/* mem.slabinfo.objects.total */
	    atom->ull = proc_slabinfo.caches[inst].total_objs;
	    break;
	case 2:	/* mem.slabinfo.objects.size */
	    if (proc_slabinfo.caches[inst].seen < 11)	/* version 1.1 or later only */
		return 0;
	    atom->ul = proc_slabinfo.caches[inst].object_size;
	    break;
	case 3:	/* mem.slabinfo.slabs.active */
	    if (proc_slabinfo.caches[inst].seen < 11)	/* version 1.1 or later only */
		return 0;
	    atom->ul = proc_slabinfo.caches[inst].num_active_slabs;
	    break;
	case 4:	/* mem.slabinfo.slabs.total */
	    if (proc_slabinfo.caches[inst].seen == 11)	/* version 1.1 only */
		return 0;
	    atom->ul = proc_slabinfo.caches[inst].total_slabs;
	    break;
	case 5:	/* mem.slabinfo.slabs.pages_per_slab */
	    if (proc_slabinfo.caches[inst].seen < 11)	/* version 1.1 or later only */
		return 0;
	    atom->ul = proc_slabinfo.caches[inst].pages_per_slab;
	    break;
	case 6:	/* mem.slabinfo.slabs.objects_per_slab */
	    if (proc_slabinfo.caches[inst].seen != 20)	/* version 2.0 only */
		return 0;
	    atom->ul = proc_slabinfo.caches[inst].objects_per_slab;
	    break;
	case 7:	/* mem.slabinfo.slabs.total_size */
	    if (proc_slabinfo.caches[inst].seen < 11)	/* version 1.1 or later only */
		return 0;
	    atom->ull = proc_slabinfo.caches[inst].total_size;
	    break;
	default:
	    return PM_ERR_PMID;
	}
    	break;

    case CLUSTER_PARTITIONS:
	return proc_partitions_fetch(mdesc, inst, atom);

    case CLUSTER_SCSI:
	if (proc_scsi.nscsi == 0)
	    return 0; /* no values available */
	switch(idp->item) {
	case 0: /* hinv.map.scsi */
	    atom->cp = (char *)NULL;
	    for (i=0; i < proc_scsi.nscsi; i++) {
		if (proc_scsi.scsi[i].id == inst) {
		    atom->cp = proc_scsi.scsi[i].dev_name;
		    break;
		}
	    }
	    if (i == proc_scsi.nscsi)
	    	return PM_ERR_INST;
	    break;
	default:
	    return PM_ERR_PMID;
	}
    	break;

    case CLUSTER_KERNEL_UNAME:
	switch(idp->item) {
	case 5: /* pmda.uname */
	    sprintf(uname_string, "%s %s %s %s %s",
	    	kernel_uname.sysname, 
		kernel_uname.nodename,
		kernel_uname.release,
		kernel_uname.version,
		kernel_uname.machine);
	    atom->cp = uname_string;
	    break;

	case 6: /* pmda.version */
	    atom->cp = pmGetConfig("PCP_VERSION");
	    break;

	case 7:	/* kernel.uname.distro ... not from uname(2) */
	    atom->cp = get_distro_info();
	    break;

	default:
	    return PM_ERR_PMID;
	}
	break;

    case CLUSTER_CPUINFO:
	if (idp->item != 7 && /* hinv.machine is singular */
	    (inst < 0 || inst >= proc_cpuinfo.cpuindom->it_numinst))
	    return PM_ERR_INST;
	switch(idp->item) {
	case 0: /* hinv.cpu.clock */
	    atom->f = proc_cpuinfo.cpuinfo[inst].clock;
	    break;
	case 1: /* hinv.cpu.vendor */
	    if ((atom->cp = proc_cpuinfo.cpuinfo[inst].vendor) == (char *)NULL)
	    	atom->cp = "unknown";
	    break;
	case 2: /* hinv.cpu.model */
	    if ((atom->cp = proc_cpuinfo.cpuinfo[inst].model) == (char *)NULL)
	    	atom->cp = "unknown";
	    break;
	case 3: /* hinv.cpu.stepping */
	    if ((atom->cp = proc_cpuinfo.cpuinfo[inst].stepping) == (char *)NULL)
	    	atom->cp = "unknown";
	    break;
	case 4: /* hinv.cpu.cache */
	    atom->ul = proc_cpuinfo.cpuinfo[inst].cache;
	    break;
	case 5: /* hinv.cpu.bogomips */
	    atom->f = proc_cpuinfo.cpuinfo[inst].bogomips;
	    break;
	case 6: /* hinv.map.cpu_num */
	    atom->ul = proc_cpuinfo.cpuinfo[inst].cpu_num;
	    break;
	case 7: /* hinv.machine */
	    atom->cp = proc_cpuinfo.machine;
	    break;
	case 8: /* hinv.map.cpu_node */
	    atom->ul = proc_cpuinfo.cpuinfo[inst].node;
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

    /*
     * Cluster added by Mike Mason <mmlnx@us.ibm.com>
     */
    case CLUSTER_SEM_LIMITS:
	switch (idp->item) {
	case 0:	/* ipc.sem.max_semmap */
	    atom->ul = sem_limits.semmap;
	    break;
	case 1:	/* ipc.sem.max_semid */
	    atom->ul = sem_limits.semmni;
	    break;
	case 2:	/* ipc.sem.max_sem */
	    atom->ul = sem_limits.semmns;
	    break;
	case 3:	/* ipc.sem.num_undo */
	    atom->ul = sem_limits.semmnu;
	    break;
	case 4:	/* ipc.sem.max_perid */
	    atom->ul = sem_limits.semmsl;
	    break;
	case 5:	/* ipc.sem.max_ops */
	    atom->ul = sem_limits.semopm;
	    break;
	case 6:	/* ipc.sem.max_undoent */
	    atom->ul = sem_limits.semume;
	    break;
	case 7:	/* ipc.sem.sz_semundo */
	    atom->ul = sem_limits.semusz;
	    break;
	case 8:	/* ipc.sem.max_semval */
	    atom->ul = sem_limits.semvmx;
	    break;
	case 9:	/* ipc.sem.max_exit */
	    atom->ul = sem_limits.semaem;
	    break;
	default:
	    return PM_ERR_PMID;
	}
	break;

    /*
     * Cluster added by Mike Mason <mmlnx@us.ibm.com>
     */
    case CLUSTER_MSG_LIMITS:
	switch (idp->item) {
	case 0:	/* ipc.msg.sz_pool */
	    atom->ul = msg_limits.msgpool;
	    break;
	case 1:	/* ipc.msg.mapent */
	    atom->ul = msg_limits.msgmap;
	    break;
	case 2:	/* ipc.msg.max_msgsz */
	    atom->ul = msg_limits.msgmax;
	    break;
	case 3:	/* ipc.msg.max_defmsgq */
	    atom->ul = msg_limits.msgmnb;
	    break;
	case 4:	/* ipc.msg.max_msgqid */
	    atom->ul = msg_limits.msgmni;
	    break;
	case 5:	/* ipc.msg.sz_msgseg */
	    atom->ul = msg_limits.msgssz;
	    break;
	case 6:	/* ipc.msg.num_smsghdr */
	    atom->ul = msg_limits.msgtql;
	    break;
	case 7:	/* ipc.msg.max_seg */
	    atom->ul = (unsigned long) msg_limits.msgseg;
	    break;
	default:
	    return PM_ERR_PMID;
	}
	break;

    /*
     * Cluster added by Mike Mason <mmlnx@us.ibm.com>
     */
    case CLUSTER_SHM_LIMITS:
	switch (idp->item) {
	case 0:	/* ipc.shm.max_segsz */
	    atom->ul = shm_limits.shmmax;
	    break;
	case 1:	/* ipc.shm.min_segsz */
	    atom->ul = shm_limits.shmmin;
	    break;
	case 2:	/* ipc.shm.max_seg */
	    atom->ul = shm_limits.shmmni;
	    break;
	case 3:	/* ipc.shm.max_segproc */
	    atom->ul = shm_limits.shmseg;
	    break;
	case 4:	/* ipc.shm.max_shmsys */
	    atom->ul = shm_limits.shmall;
	    break;
	default:
	    return PM_ERR_PMID;
	}
	break;

    /*
     * Cluster added by Mike Mason <mmlnx@us.ibm.com>
     */
    case CLUSTER_NUSERS:
	{
	    /* count the number of users */
	    struct utmp *ut;
	    atom->ul = 0;
	    setutent();
	    while ((ut = getutent())) {
		    if ((ut->ut_type == USER_PROCESS) && (ut->ut_name[0] != '\0'))
			    atom->ul++;
	    }
	    endutent();
	}
	break;


    case CLUSTER_IB: /* deprecated: network.ib, use infiniband.* */
        return PM_ERR_APPVERSION;
	break;

    case CLUSTER_QUOTA:
	if (idp->item <= 5) {
	    sts = pmdaCacheLookup(INDOM(FILESYS_INDOM), inst, NULL,
					(void **)&fs);
	    if (sts < 0)
		return sts;
	    if (sts != PMDA_CACHE_ACTIVE)
		return PM_ERR_INST;
	    switch (idp->item) {
	    case 0:	/* quota.state.project.accounting */
		atom->ul = !!(fs->flags & FSF_QUOT_PROJ_ACC);
		break;
	    case 1:	/* quota.state.project.enforcement */
		atom->ul = !!(fs->flags & FSF_QUOT_PROJ_ENF);
		break;
	    default:
		return PM_ERR_PMID;
	    }
	}
	else if (idp->item <= 13) {
	    struct project *pp;
	    sts = pmdaCacheLookup(INDOM(QUOTA_PRJ_INDOM), inst, NULL,
					(void **)&pp);
	    if (sts < 0)
		return sts;
	    if (sts != PMDA_CACHE_ACTIVE)
		return PM_ERR_INST;
	    switch (idp->item) {
	    case 6:	/* quota.project.space.hard */
		atom->ull = pp->space_hard >> 1; /* BBs to KB */
		break;
	    case 7:	/* quota.project.space.soft */
		atom->ull = pp->space_soft >> 1; /* BBs to KB */
		break;
	    case 8:	/* quota.project.space.used */
		atom->ull = pp->space_used >> 1; /* BBs to KB */
		break;
	    case 9:	/* quota.project.space.time_left */
		atom->l = pp->space_time_left;
		break;
	    case 10:	/* quota.project.files.hard */
		atom->ull = pp->files_hard;
		break;
	    case 11:	/* quota.project.files.soft */
		atom->ull = pp->files_soft;
		break;
	    case 12:	/* quota.project.files.used */
		atom->ull = pp->files_used;
		break;
	    case 13:	/* quota.project.files.time_left */
		atom->l = pp->files_time_left;
		break;
	    default:
		return PM_ERR_PMID;
	    }
	}
	else
	    return PM_ERR_PMID;
	break;
    
    case CLUSTER_NUMA_MEMINFO:
	/* NUMA memory metrics from /sys/devices/system/node/nodeX */
	if (inst < 0 || inst >= numa_meminfo.node_indom->it_numinst)
	    return PM_ERR_INST;

	switch(idp->item) {
	case 0: /* mem.numa.util.total */
	    sts = linux_table_lookup("MemTotal:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;
	case 1: /* mem.numa.util.free */
	    sts = linux_table_lookup("MemFree:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 2: /* mem.numa.util.used */
	    sts = linux_table_lookup("MemUsed:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 3: /* mem.numa.util.active */
	    sts = linux_table_lookup("Active:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 4: /* mem.numa.util.inactive */
	    sts = linux_table_lookup("Inactive:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 5: /* mem.numa.util.active_anon */
	    sts = linux_table_lookup("Active(anon):", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 6: /* mem.numa.util.inactive_anon */
	    sts = linux_table_lookup("Inactive(anon):", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 7: /* mem.numa.util.active_file */
	    sts = linux_table_lookup("Active(file):", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 8: /* mem.numa.util.inactive_file */
	    sts = linux_table_lookup("Inactive(file):", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 9: /* mem.numa.util.highTotal */
	    sts = linux_table_lookup("HighTotal:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 10: /* mem.numa.util.highFree */
	    sts = linux_table_lookup("HighFree:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 11: /* mem.numa.util.lowTotal */
	    sts = linux_table_lookup("LowTotal:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 12: /* mem.numa.util.lowFree */
	    sts = linux_table_lookup("LowFree:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 13: /* mem.numa.util.unevictable */
	    sts = linux_table_lookup("Unevictable:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 14: /* mem.numa.util.mlocked */
	    sts = linux_table_lookup("Mlocked:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 15: /* mem.numa.util.dirty */
	    sts = linux_table_lookup("Dirty:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 16: /* mem.numa.util.writeback */
	    sts = linux_table_lookup("Writeback:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 17: /* mem.numa.util.filePages */
	    sts = linux_table_lookup("FilePages:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 18: /* mem.numa.util.mapped */
	    sts = linux_table_lookup("Mapped:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 19: /* mem.numa.util.anonPages */
	    sts = linux_table_lookup("AnonPages:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 20: /* mem.numa.util.shmem */
	    sts = linux_table_lookup("Shmem:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 21: /* mem.numa.util.kernelStack */
	    sts = linux_table_lookup("KernelStack:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 22: /* mem.numa.util.pageTables */
	    sts = linux_table_lookup("PageTables:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 23: /* mem.numa.util.NFS_Unstable */
	    sts = linux_table_lookup("NFS_Unstable:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 24: /* mem.numa.util.bounce */
	    sts = linux_table_lookup("Bounce:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 25: /* mem.numa.util.writebackTmp */
	    sts = linux_table_lookup("WritebackTmp:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 26: /* mem.numa.util.slab */
	    sts = linux_table_lookup("Slab:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 27: /* mem.numa.util.slabReclaimable */
	    sts = linux_table_lookup("SReclaimable:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 28: /* mem.numa.util.slabUnreclaimable */
	    sts = linux_table_lookup("SUnreclaim:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 29: /* mem.numa.util.hugepagesTotal */
	    sts = linux_table_lookup("HugePages_Total:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 30: /* mem.numa.util.hugepagesFree */
	    sts = linux_table_lookup("HugePages_Free:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 31: /* mem.numa.util.hugepagesSurp */
	    sts = linux_table_lookup("HugePages_Surp:", numa_meminfo.node_info[inst].meminfo,
		    &atom->ull);
	    break;

	case 32: /* mem.numa.alloc.hit */
	    sts = linux_table_lookup("numa_hit", numa_meminfo.node_info[inst].memstat,
		    &atom->ull);
	    break;

	case 33: /* mem.numa.alloc.miss */
	    sts = linux_table_lookup("numa_miss", numa_meminfo.node_info[inst].memstat,
		    &atom->ull);
	    break;

	case 34: /* mem.numa.alloc.foreign */
	    sts = linux_table_lookup("numa_foreign", numa_meminfo.node_info[inst].memstat,
		    &atom->ull);
	    break;

	case 35: /* mem.numa.alloc.interleave_hit */
	    sts = linux_table_lookup("interleave_hit", numa_meminfo.node_info[inst].memstat,
		    &atom->ull);
	    break;

	case 36: /* mem.numa.alloc.local_node */
	    sts = linux_table_lookup("local_node", numa_meminfo.node_info[inst].memstat,
		    &atom->ull);
	    break;

	case 37: /* mem.numa.alloc.other_node */
	    sts = linux_table_lookup("other_node", numa_meminfo.node_info[inst].memstat,
		    &atom->ull);
	    break;

	default:
	    return PM_ERR_PMID;
	}
	return sts;

    case CLUSTER_CGROUP_SUBSYS:
	switch (idp->item) {
	case 0:	/* cgroup.subsys.hierarchy */
	    sts = pmdaCacheLookup(INDOM(CGROUP_SUBSYS_INDOM), inst, NULL, (void **)&i);
	    if (sts < 0)
		return sts;
	    if (sts != PMDA_CACHE_ACTIVE)
	    	return PM_ERR_INST;
	    atom->ul = i;
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

    case CLUSTER_INTERRUPTS:
	switch (idp->item) {
	case 3:	/* kernel.all.interrupts.error */
	    atom->ul = irq_err_count;
	    break;
	default:
	    return PM_ERR_PMID;
	}
	break;

    case CLUSTER_INTERRUPT_LINES:
    case CLUSTER_INTERRUPT_OTHER:
	if (inst >= indomtab[CPU_INDOM].it_numinst)
	    return PM_ERR_INST;
	return interrupts_fetch(idp->cluster, idp->item, inst, atom);

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
linux_fetch(int numpmid, pmID pmidlist[], pmResult **resp, pmdaExt *pmda)
{
    int		i;
    int		need_refresh[NUM_CLUSTERS];

    memset(need_refresh, 0, sizeof(need_refresh));
    for (i=0; i < numpmid; i++) {
	__pmID_int *idp = (__pmID_int *)&(pmidlist[i]);
	if (idp->cluster >= 0 && idp->cluster < NUM_CLUSTERS) {
	    need_refresh[idp->cluster]++;

	    if (idp->cluster == CLUSTER_STAT && 
		need_refresh[CLUSTER_PARTITIONS] == 0 &&
		is_partitions_metric(pmidlist[i]))
		need_refresh[CLUSTER_PARTITIONS]++;

	    if (idp->cluster == CLUSTER_CPUINFO ||
	    	idp->cluster == CLUSTER_CPUACCT_GROUPS ||
		idp->cluster == CLUSTER_INTERRUPT_LINES ||
		idp->cluster == CLUSTER_INTERRUPT_OTHER ||
		idp->cluster == CLUSTER_INTERRUPTS)
		need_refresh[CLUSTER_STAT]++;
	}

	/* In 2.6 kernels, swap.{pagesin,pagesout,in,out} are in /proc/vmstat */
	if (_pm_have_proc_vmstat && idp->cluster == CLUSTER_STAT) {
	    if (idp->item >= 8 && idp->item <= 11)
	    	need_refresh[CLUSTER_VMSTAT]++;
	}
    }

    linux_refresh(pmda, need_refresh);
    return pmdaFetch(numpmid, pmidlist, resp, pmda);
}

static int
procfs_zero(const char *filename, pmValueSet *vsp)
{
    FILE	*fp;
    int		value;
    int		sts = 0;

    value = vsp->vlist[0].value.lval;
    if (value < 0)
	return PM_ERR_SIGN;

    fp = fopen(filename, "w");
    if (!fp) {
	sts = -oserror();
    } else {
	fprintf(fp, "%d\n", value);
	fclose(fp);
    }
    return sts;
}

static int
linux_store(pmResult *result, pmdaExt *pmda)
{
    int		i;
    int		sts = 0;
    pmValueSet	*vsp;
    __pmID_int	*pmidp;

    for (i = 0; i < result->numpmid && !sts; i++) {
	vsp = result->vset[i];
	pmidp = (__pmID_int *)&vsp->pmid;

	if (pmidp->cluster == CLUSTER_XFS && pmidp->item == 79) {
	    sts = procfs_zero("/proc/sys/fs/xfs/stats_clear", vsp);
	} 
	else if (pmidp->cluster == CLUSTER_IB && pmidp->item == IB_COUNTERS_ALL + 1) {
	    /* deprecated: code preserved so suitable error can be returned */
	    sts = PM_ERR_APPVERSION;
	} 
	else {
	    sts = PM_ERR_PERMISSION;
	}
    }
    return sts;
}

static int
linux_text(int ident, int type, char **buf, pmdaExt *pmda)
{
    if ((type & PM_TEXT_PMID) == PM_TEXT_PMID) {
	int sts = linux_dynamic_lookup_text(ident, type, buf, pmda);
	if (sts != -ENOENT)
	    return sts;
    }
    return pmdaText(ident, type, buf, pmda);
}

static int
linux_pmid(const char *name, pmID *pmid, pmdaExt *pmda)
{
    __pmnsTree *tree = linux_dynamic_lookup_name(pmda, name);
    return pmdaTreePMID(tree, name, pmid);
}

static int
linux_name(pmID pmid, char ***nameset, pmdaExt *pmda)
{
    __pmnsTree *tree = linux_dynamic_lookup_pmid(pmda, pmid);
    return pmdaTreeName(tree, pmid, nameset);
}

static int
linux_children(const char *name, int flag, char ***kids, int **sts, pmdaExt *pmda)
{
    __pmnsTree *tree = linux_dynamic_lookup_name(pmda, name);
    return pmdaTreeChildren(tree, name, flag, kids, sts);
}

int
linux_metrictable_size(void)
{
    return sizeof(linux_metrictab)/sizeof(linux_metrictab[0]);
}

/*
 * Initialise the agent (both daemon and DSO).
 */

void 
linux_init(pmdaInterface *dp)
{
    int		i, major, minor, point;
    __pmID_int	*idp;

    _pm_system_pagesize = getpagesize();
    if (_isDSO) {
	char helppath[MAXPATHLEN];
	int sep = __pmPathSeparator();
	snprintf(helppath, sizeof(helppath), "%s%c" "linux" "%c" "help",
		pmGetConfig("PCP_PMDAS_DIR"), sep, sep);
	pmdaDSO(dp, PMDA_INTERFACE_4, "linux DSO", helppath);
    }

    if (dp->status != 0)
	return;

    dp->version.four.instance = linux_instance;
    dp->version.four.store = linux_store;
    dp->version.four.fetch = linux_fetch;
    dp->version.four.text = linux_text;
    dp->version.four.pmid = linux_pmid;
    dp->version.four.name = linux_name;
    dp->version.four.children = linux_children;
    pmdaSetFetchCallBack(dp, linux_fetchCallBack);

    proc_pid.indom = &indomtab[PROC_INDOM];
    proc_stat.cpu_indom = proc_cpuinfo.cpuindom = &indomtab[CPU_INDOM];
    numa_meminfo.node_indom = proc_cpuinfo.node_indom = &indomtab[NODE_INDOM];
    proc_scsi.scsi_indom = &indomtab[SCSI_INDOM];
    proc_slabinfo.indom = &indomtab[SLAB_INDOM];

    /*
     * Figure out kernel version.  The precision of certain metrics
     * (e.g. percpu time counters) has changed over kernel versions.
     * See include/linux/kernel_stat.h for all the various flavours.
     */
    uname(&kernel_uname);
    _pm_ctxt_size = 8;
    _pm_intr_size = 8;
    _pm_cputime_size = 8;
    _pm_idletime_size = 8;
    if (sscanf(kernel_uname.release, "%d.%d.%d", &major, &minor, &point) == 3) {
	if (major < 2 || (major == 2 && minor <= 4)) {	/* 2.4 and earlier */
	    _pm_ctxt_size = 4;
	    _pm_intr_size = 4;
	    _pm_cputime_size = 4;
	    _pm_idletime_size = sizeof(unsigned long);
	}
	else if (major == 2 && minor == 6 &&
		 point >= 0 && point <= 4) {  /* 2.6.0->.4 */
	    _pm_cputime_size = 4;
	    _pm_idletime_size = 4;
	}
    }
    for (i = 0; i < sizeof(linux_metrictab)/sizeof(pmdaMetric); i++) {
	idp = (__pmID_int *)&(linux_metrictab[i].m_desc.pmid);
	if (idp->cluster == CLUSTER_STAT) {
	    switch (idp->item) {
	    case 0:	/* kernel.percpu.cpu.user */
	    case 1:	/* kernel.percpu.cpu.nice */
	    case 2:	/* kernel.percpu.cpu.sys */
	    case 20:	/* kernel.all.cpu.user */
	    case 21:	/* kernel.all.cpu.nice */
	    case 22:	/* kernel.all.cpu.sys */
	    case 30:	/* kernel.percpu.cpu.wait.total */
	    case 31:	/* kernel.percpu.cpu.intr */
	    case 34:	/* kernel.all.cpu.intr */
	    case 35:	/* kernel.all.cpu.wait.total */
	    case 53:	/* kernel.all.cpu.irq.soft */
	    case 54:	/* kernel.all.cpu.irq.hard */
	    case 55:	/* kernel.all.cpu.steal */
	    case 56:	/* kernel.percpu.cpu.irq.soft */
	    case 57:	/* kernel.percpu.cpu.irq.hard */
	    case 58:	/* kernel.percpu.cpu.steal */
	    case 60:	/* kernel.all.cpu.guest */
	    case 61:	/* kernel.percpu.cpu.guest */
	    case 62:	/* kernel.pernode.cpu.user */
	    case 63:	/* kernel.pernode.cpu.nice */
	    case 64:	/* kernel.pernode.cpu.sys */
	    case 69:	/* kernel.pernode.cpu.wait.total */
	    case 66:	/* kernel.pernode.cpu.intr */
	    case 70:	/* kernel.pernode.cpu.irq.soft */
	    case 71:	/* kernel.pernode.cpu.irq.hard */
	    case 67:	/* kernel.pernode.cpu.steal */
	    case 68:	/* kernel.pernode.cpu.guest */
		_pm_metric_type(linux_metrictab[i].m_desc.type, _pm_cputime_size);
		break;
	    case 3:	/* kernel.percpu.cpu.idle */
	    case 23:	/* kernel.all.cpu.idle */
	    case 65:	/* kernel.pernode.cpu.idle */
		_pm_metric_type(linux_metrictab[i].m_desc.type, _pm_idletime_size);
		break;
	    case 12:	/* kernel.all.intr */
		_pm_metric_type(linux_metrictab[i].m_desc.type, _pm_intr_size);
		break;
	    case 13:	/* kernel.all.pswitch */
		_pm_metric_type(linux_metrictab[i].m_desc.type, _pm_ctxt_size);
		break;
	    }
	}
	if (linux_metrictab[i].m_desc.type == PM_TYPE_NOSUPPORT)
	    fprintf(stderr, "Bad kernel metric descriptor type (%u.%u)\n",
			    idp->cluster, idp->item);
    }

    /* 
     * Read System.map and /proc/ksyms. Used to translate wait channel
     * addresses to symbol names. 
     * Added by Mike Mason <mmlnx@us.ibm.com>
     */
    read_ksym_sources(kernel_uname.release);

    interrupts_init();
    cgroup_init();

    pmdaInit(dp, indomtab, sizeof(indomtab)/sizeof(indomtab[0]), linux_metrictab,
             sizeof(linux_metrictab)/sizeof(linux_metrictab[0]));
}


static void
usage(void)
{
    fprintf(stderr, "Usage: %s [options]\n\n", pmProgname);
    fputs("Options:\n"
	  "  -d domain  use domain (numeric) for metrics domain of PMDA\n"
	  "  -l logfile write log into logfile rather than using default log name\n",
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

    _isDSO = 0;
    __pmSetProgname(argv[0]);

    snprintf(helppath, sizeof(helppath), "%s%c" "linux" "%c" "help",
		pmGetConfig("PCP_PMDAS_DIR"), sep, sep);
    pmdaDaemon(&dispatch, PMDA_INTERFACE_4, pmProgname, LINUX, "linux.log", helppath);

    if ((c = pmdaGetOpt(argc, argv, "D:d:l:?", &dispatch, &err)) != EOF)
    	err++;

    if (err)
    	usage();

    pmdaOpenLog(&dispatch);
    linux_init(&dispatch);
    pmdaConnect(&dispatch);
    pmdaMain(&dispatch);

    exit(0);
}
