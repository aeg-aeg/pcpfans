/*
 * Linux proc/<pid>/{stat,statm,status,...} Clusters
 *
 * Copyright (c) 2013 Red Hat.
 * Copyright (c) 2000,2004,2006 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (c) 2010 Aconex.  All Rights Reserved.
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
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include "proc_pid.h"
#include "indom.h"

static proc_pid_list_t pids;

static int
compare_pid(const void *pa, const void *pb)
{
    int a = *(int *)pa;
    int b = *(int *)pb;
    return a - b;
}

static void
pidlist_append_pid(int pid)
{
    if (pids.count >= pids.size) {
	pids.size += 64;
	if (!(pids.pids = (int *)realloc(pids.pids, pids.size * sizeof(int)))) {
	    perror("pidlist_append: out of memory");
	    pids.size = pids.count = 0;
	    return;	/* soldier on bravely */
	}
    }
    pids.pids[pids.count++] = pid;
}

static void
pidlist_append(const char *pidname)
{
    pidlist_append_pid(atoi(pidname));
}

static void
tasklist_append(const char *pid)
{
    DIR *taskdirp;
    struct dirent *tdp;
    char taskpath[1024];

    sprintf(taskpath, "%s/proc/%s/task", proc_statspath, pid);
    if ((taskdirp = opendir(taskpath)) != NULL) {
	while ((tdp = readdir(taskdirp)) != NULL) {
	    if (!isdigit((int)tdp->d_name[0]) || strcmp(pid, tdp->d_name) == 0)
		continue;
	    pidlist_append(tdp->d_name);
	}
	closedir(taskdirp);
    }
#if PCP_DEBUG
    else {
	if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
	    char ebuf[1024];
	    fprintf(stderr, "tasklist_append: opendir(\"%s\") failed: %s\n", taskpath, pmErrStr_r(-oserror(), ebuf, sizeof(ebuf)));
	}
    }
#endif
}

static int
refresh_cgroup_pidlist(int want_threads, const char *cgroup)
{
    char path[MAXPATHLEN];
    FILE *fp;
    int pid;

    /*
     * We're running in cgroups mode where a subset of the processes is
     * going to be returned based on the cgroup specified earlier via a
     * store into the proc.control.{all,perclient}.cgroups metric.
     *
     * Use the "cgroup.procs" or "tasks" file depending on want_threads.
     * Note that both these files are already sorted, ascending numeric.
     */
    if (want_threads)
	snprintf(path, sizeof(path), "%s%s/tasks", proc_statspath, cgroup);
    else
	snprintf(path, sizeof(path), "%s%s/cgroup.procs", proc_statspath, cgroup);

    if ((fp = fopen(path, "r")) != NULL) {
	while (fscanf(fp, "%d\n", &pid) == 1)
	    pidlist_append_pid(pid);
	fclose(fp);
    }
#if PCP_DEBUG
    else {
	if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
	    char ebuf[1024];
	    fprintf(stderr, "refresh_cgroup_pidlist: fopen(\"%s\", \"r\") failed: %s\n", path, pmErrStr_r(-oserror(), ebuf, sizeof(ebuf)));
	}
    }
#endif
    return 0;
}

static int
refresh_global_pidlist(int want_threads)
{
    DIR *dirp;
    struct dirent *dp;
    char path[MAXPATHLEN];

    snprintf(path, sizeof(path), "%s/proc", proc_statspath);
    if ((dirp = opendir(path)) == NULL) {
#if PCP_DEBUG
	if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
	    char ebuf[1024];
	    fprintf(stderr, "refresh_global_pidlist: opendir(\"%s\") failed: %s\n", path, pmErrStr_r(-oserror(), ebuf, sizeof(ebuf)));
	}
#endif
	return -oserror();
    }

    /* note: readdir on /proc ignores threads */
    while ((dp = readdir(dirp)) != NULL) {
	if (isdigit((int)dp->d_name[0])) {
	    pidlist_append(dp->d_name);
	    if (want_threads)
		tasklist_append(dp->d_name);
	}
    }
    closedir(dirp);

    qsort(pids.pids, pids.count, sizeof(int), compare_pid);
    return 0;
}

static void
refresh_proc_pidlist(proc_pid_t *proc_pid)
{
    int i;
    int fd;
    char *p;
    char buf[MAXPATHLEN];
    __pmHashNode *node, *next, *prev;
    proc_pid_entry_t *ep;
    pmdaIndom *indomp = proc_pid->indom;

    if (indomp->it_numinst < pids.count)
	indomp->it_set = (pmdaInstid *)realloc(indomp->it_set,
						pids.count * sizeof(pmdaInstid));
    indomp->it_numinst = pids.count;

    /*
     * invalidate all entries so we can harvest pids that have exited
     */
    for (i=0; i < proc_pid->pidhash.hsize; i++) {
	for (node=proc_pid->pidhash.hash[i]; node != NULL; node = node->next) {
	    ep = (proc_pid_entry_t *)node->data;
	    ep->flags = 0;
	}
    }

    /*
     * walk pid list and add new pids to the hash table,
     * marking entries valid as we go ...
     */
    for (i=0; i < pids.count; i++) {
	node = __pmHashSearch(pids.pids[i], &proc_pid->pidhash);
	if (node == NULL) {
	    int k = 0;

	    ep = (proc_pid_entry_t *)malloc(sizeof(proc_pid_entry_t));
	    memset(ep, 0, sizeof(proc_pid_entry_t));

	    ep->id = pids.pids[i];

	    snprintf(buf, sizeof(buf), "%s/proc/%d/cmdline", proc_statspath, pids.pids[i]);
	    if ((fd = open(buf, O_RDONLY)) >= 0) {
		sprintf(buf, "%06d ", pids.pids[i]);
		if ((k = read(fd, buf+7, sizeof(buf)-8)) > 0) {
		    p = buf + k +7;
		    *p-- = '\0';
		    /* Skip trailing nils, i.e. don't replace them */
		    while (buf+7 < p) {
			if (*p-- != '\0') {
				break;
			}
		    }
		    /* Remove NULL terminators from cmdline string array */
		    /* Suggested by Mike Mason <mmlnx@us.ibm.com> */
		    while (buf+7 < p) {
			if (*p == '\0') *p = ' ';
			p--;
		    }
		}
		close(fd);
	    }
#if PCP_DEBUG
	    else {
		if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
		    char ebuf[1024];
		    fprintf(stderr, "refresh_proc_pidlist: open(\"%s\", O_RDONLY) failed: %s\n", buf, pmErrStr_r(-oserror(), ebuf, sizeof(ebuf)));
		}
	    }
#endif
	    if (k == 0) {
		/*
		 * If a process is swapped out, /proc/<pid>/cmdline
		 * returns an empty string so we have to get it
		 * from /proc/<pid>/status or /proc/<pid>/stat
		 */
		sprintf(buf, "%s/proc/%d/status", proc_statspath, pids.pids[i]);
		if ((fd = open(buf, O_RDONLY)) >= 0) {
		    /* We engage in a bit of a hanky-panky here:
		     * the string should look like "123456 (name)",
		     * we get it from /proc/XX/status as "Name:   name\n...",
		     * to fit the 6 digits of PID and opening parenthesis, 
	             * save 2 bytes at the start of the buffer. 
                     * And don't forget to leave 2 bytes for the trailing 
		     * parenthesis and the nil. Here is
		     * an example of what we're trying to achieve:
		     * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+
		     * |  |  | N| a| m| e| :|\t| i| n| i| t|\n| S|...
		     * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+
		     * | 0| 0| 0| 0| 0| 1|  | (| i| n| i| t| )|\0|...
		     * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+ */
		    if ((k = read(fd, buf+2, sizeof(buf)-4)) > 0) {
			int bc;

			if ((p = strchr(buf+2, '\n')) == NULL)
			    p = buf+k;
			p[0] = ')'; 
			p[1] = '\0';
			bc = sprintf(buf, "%06d ", pids.pids[i]); 
			buf[bc] = '(';
		    }
		    close(fd);
		}
#if PCP_DEBUG
		else {
		    if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
			char ebuf[1024];
			fprintf(stderr, "refresh_proc_pidlist: open(\"%s\", O_RDONLY) failed: %s\n", buf, pmErrStr_r(-oserror(), ebuf, sizeof(ebuf)));
		    }
		}
#endif
	    }

	    if (k <= 0) {
		/* hmm .. must be exiting */
	    	sprintf(buf, "%06d <exiting>", pids.pids[i]);
	    }

	    ep->name = strdup(buf);

	    __pmHashAdd(pids.pids[i], (void *)ep, &proc_pid->pidhash);
	    // fprintf(stderr, "## ADDED \"%s\" to hash table\n", buf);
	}
	else
	    ep = (proc_pid_entry_t *)node->data;
	
	/* mark pid as still existing */
	ep->flags |= PROC_PID_FLAG_VALID;

	/* refresh the indom pointer */
	indomp->it_set[i].i_inst = ep->id;
	indomp->it_set[i].i_name = ep->name;
    }

    /* 
     * harvest exited pids from the pid hash table
     */
    for (i=0; i < proc_pid->pidhash.hsize; i++) {
	for (prev=NULL, node=proc_pid->pidhash.hash[i]; node != NULL;) {
	    next = node->next;
	    ep = (proc_pid_entry_t *)node->data;
	    // fprintf(stderr, "CHECKING key=%d node=" PRINTF_P_PFX "%p prev=" PRINTF_P_PFX "%p next=" PRINTF_P_PFX "%p ep=" PRINTF_P_PFX "%p valid=%d\n",
	    	// ep->id, node, prev, node->next, ep, ep->valid);
	    if (!(ep->flags & PROC_PID_FLAG_VALID)) {
	        // fprintf(stderr, "DELETED key=%d name=\"%s\"\n", ep->id, ep->name);
		if (ep->name != NULL)
		    free(ep->name);
		if (ep->stat_buf != NULL)
		    free(ep->stat_buf);
		if (ep->status_buf != NULL)
		    free(ep->status_buf);
		if (ep->statm_buf != NULL)
		    free(ep->statm_buf);
		if (ep->maps_buf != NULL)
		    free(ep->maps_buf);
		if (ep->schedstat_buf != NULL)
		    free(ep->schedstat_buf);
		if (ep->io_buf != NULL)
		    free(ep->io_buf);
		if (ep->wchan_buf != NULL)
		    free(ep->wchan_buf);

	    	if (prev == NULL)
		    proc_pid->pidhash.hash[i] = node->next;
		else
		    prev->next = node->next;
		free(ep);
		free(node);
	    }
	    else {
	    	prev = node;
	    }
	    if ((node = next) == NULL)
	    	break;
	}
    }
}

int
refresh_proc_pid(proc_pid_t *proc_pid, int threads, const char *cgroups)
{
    int sts;

    pids.count = 0;
    pids.threads = threads;

    sts = (cgroups && cgroups[0] != '\0') ?
		refresh_cgroup_pidlist(threads, cgroups) :
		refresh_global_pidlist(threads);
    if (sts < 0)
	return sts;

#if PCP_DEBUG
    if (pmDebug & DBG_TRACE_LIBPMDA)
	fprintf(stderr,
		"refresh_proc_pid: %d pids (threads=%d, cgroups=\"%s\")\n",
		sts, threads, cgroups ? cgroups : "");
#endif

    refresh_proc_pidlist(proc_pid);
    return 0;
}


/*
 * Open a proc file, taking into account that we may want thread info
 * rather than process information.
 *
 * We make (ab)use of some obscure Linux procfs mechanisms here!
 * Even though readdir(/proc) does not contain tasks, we can still open
 * taskid directory files; on top of that, the tasks sub-directory in a
 * task group has all (peer) tasks in that group, even for "children".
 */
static int
proc_open(const char *base, proc_pid_entry_t *ep)
{
    int fd;
    char buf[128];

    if (pids.threads) {
	sprintf(buf, "%s/proc/%d/task/%d/%s", proc_statspath, ep->id, ep->id, base);
	if ((fd = open(buf, O_RDONLY)) >= 0) {
	    return fd;
	}
#if PCP_DEBUG
	else {
	    if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
		char ebuf[1024];
		fprintf(stderr, "proc_open: open(\"%s\", O_RDONLY) failed: %s\n", buf, pmErrStr_r(-oserror(), ebuf, sizeof(ebuf)));
	    }
	}
#endif
	/* fallback to /proc path if task path open fails */
    }
    sprintf(buf, "%s/proc/%d/%s", proc_statspath, ep->id, base);
    fd =  open(buf, O_RDONLY);
#if PCP_DEBUG
    if (fd < 0) {
	if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
	    char ebuf[1024];
	    fprintf(stderr, "proc_open: open(\"%s\", O_RDONLY) failed: %s\n", buf, pmErrStr_r(-oserror(), ebuf, sizeof(ebuf)));
	}
    }
#endif
    return fd;
}

static DIR *
proc_opendir(const char *base, proc_pid_entry_t *ep)
{
    DIR *dir;
    char buf[128];

    if (pids.threads) {
	sprintf(buf, "%s/proc/%d/task/%d/%s", proc_statspath, ep->id, ep->id, base);
	if ((dir = opendir(buf)) != NULL) {
	    return dir;
	}
#if PCP_DEBUG
	else {
	    if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
		char ebuf[1024];
		fprintf(stderr, "proc_opendir: opendir(\"%s\") failed: %s\n", buf, pmErrStr_r(-oserror(), ebuf, sizeof(ebuf)));
	    }
	}
#endif
	/* fallback to /proc path if task path opendir fails */
    }
    sprintf(buf, "%s/proc/%d/%s", proc_statspath, ep->id, base);
    dir = opendir(buf);
#if PCP_DEBUG
    if (dir == NULL) {
	if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
	    char ebuf[1024];
	    fprintf(stderr, "proc_opendir: opendir(\"%s\") failed: %s\n", buf, pmErrStr_r(-oserror(), ebuf, sizeof(ebuf)));
	}
    }
#endif
    return dir;
}

/*
 * error mapping for fetch routines ...
 * EACCESS, EINVAL => no values (don't disclose anything else)
 * ENOENT => PM_ERR_APPVERSION
 */
static int
maperr(void)
{
    int		sts = -oserror();

    if (sts == -EACCES || sts == -EINVAL) sts = 0;
    else if (sts == -ENOENT) sts = PM_ERR_APPVERSION;
    
    return sts;
}

/*
 * fetch a proc/<pid>/stat entry for pid
 */
proc_pid_entry_t *
fetch_proc_pid_stat(int id, proc_pid_t *proc_pid, int *sts)
{
    int fd;
    int n;
    __pmHashNode *node = __pmHashSearch(id, &proc_pid->pidhash);
    proc_pid_entry_t *ep;
    char buf[1024];

    *sts = 0;
    if (node == NULL) {
#if PCP_DEBUG
	if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
	    char ibuf[1024];
	    fprintf(stderr, "fetch_proc_pid_stat: __pmHashSearch(%d, hash[%s]) -> NULL\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)));
	}
#endif
    	return NULL;
    }
    ep = (proc_pid_entry_t *)node->data;

    if (!(ep->flags & PROC_PID_FLAG_STAT_FETCHED)) {
	if ((fd = proc_open("stat", ep)) < 0)
	    *sts = maperr();
	else if ((n = read(fd, buf, sizeof(buf))) < 0) {
	    *sts = maperr();
#if PCP_DEBUG
	    if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
		char ibuf[1024];
		char ebuf[1024];
		fprintf(stderr, "fetch_proc_pid_stat: read failed: id=%d, indom=%s, sts=%s\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)), pmErrStr_r(-oserror(), ebuf, sizeof(ebuf)));
	    }
#endif
	}
	else {
	    if (n == 0) {
		/* eh? */
		*sts = -ENODATA;
#if PCP_DEBUG
		if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
		    char ibuf[1024];
		    fprintf(stderr, "fetch_proc_pid_stat: read EOF?: id=%d, indom=%s\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)));
		}
#endif
	    }
	    else {
		if (ep->stat_buflen <= n) {
		    ep->stat_buflen = n;
		    ep->stat_buf = (char *)realloc(ep->stat_buf, n);
		}
		memcpy(ep->stat_buf, buf, n);
		ep->stat_buf[n-1] = '\0';
	    }
	}
	if (fd >= 0)
		close(fd);
	ep->flags |= PROC_PID_FLAG_STAT_FETCHED;
    }

    if (!(ep->flags & PROC_PID_FLAG_WCHAN_FETCHED)) {
	if ((fd = proc_open("wchan", ep)) < 0) {
	    /* ignore failure here, backwards compat */
	    ;
	}
	else {
	    if ((n = read(fd, buf, sizeof(buf)-1)) < 0) {
		*sts = maperr();
#if PCP_DEBUG
		if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
		    char ibuf[1024];
		    char ebuf[1024];
		    fprintf(stderr, "fetch_proc_pid_stat: read \"wchan\" failed: id=%d, indom=%s, sts=%s\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)), pmErrStr_r(*sts, ebuf, sizeof(ebuf)));
		}
#endif
	    }
	    else {
		if (n == 0) {
		    /* wchan is empty, nothing to add here */
		    ;
		}
		else {
		    n++;	/* no terminating null (from kernel) */
		    if (ep->wchan_buflen <= n) {
			ep->wchan_buflen = n;
			ep->wchan_buf = (char *)realloc(ep->wchan_buf, n);
		    }
		    memcpy(ep->wchan_buf, buf, n-1);
		    ep->wchan_buf[n-1] = '\0';
		}
	    }
	}
	if (fd >= 0)
	    close(fd);
	ep->flags |= PROC_PID_FLAG_WCHAN_FETCHED;
    }

    if (*sts < 0)
    	return NULL;
    return ep;
}

/*
 * fetch a proc/<pid>/status entry for pid
 * Added by Mike Mason <mmlnx@us.ibm.com>
 */
proc_pid_entry_t *
fetch_proc_pid_status(int id, proc_pid_t *proc_pid, int *sts)
{
    __pmHashNode *node = __pmHashSearch(id, &proc_pid->pidhash);
    proc_pid_entry_t *ep;

    *sts = 0;
    if (node == NULL) {
#if PCP_DEBUG
	if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
	    char ibuf[1024];
	    fprintf(stderr, "fetch_proc_pid_status: __pmHashSearch(%d, hash[%s]) -> NULL\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)));
	}
#endif
	return NULL;
    }
    ep = (proc_pid_entry_t *)node->data;

    if (!(ep->flags & PROC_PID_FLAG_STATUS_FETCHED)) {
	int	fd;
	int	n;
	char	buf[1024];
	char	*curline;

	if ((fd = proc_open("status", ep)) < 0)
	    *sts = maperr();
	else if ((n = read(fd, buf, sizeof(buf))) < 0) {
	    *sts = maperr();
#if PCP_DEBUG
	    if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
		char ibuf[1024];
		char ebuf[1024];
		fprintf(stderr, "fetch_proc_pid_status: read failed: id=%d, indom=%s, sts=%s\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)), pmErrStr_r(-oserror(), ebuf, sizeof(ebuf)));
	    }
#endif
	}
	else {
	    if (n == 0) {
		*sts = -ENODATA;
#if PCP_DEBUG
		if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
		    char ibuf[1024];
		    fprintf(stderr, "fetch_proc_pid_status: read EOF?: id=%d, indom=%s\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)));
		}
#endif
	    }
	    else {
		if (ep->status_buflen < n) {
		    ep->status_buflen = n;
		    ep->status_buf = (char *)realloc(ep->status_buf, n);
		}

		if (ep->status_buf == NULL) {
		    *sts = -ENODATA;
#if PCP_DEBUG
		    if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
			char ibuf[1024];
			fprintf(stderr, "fetch_proc_pid_status: read EOF?: id=%d, indom=%s\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)));
		    }
#endif
		}
		else {
		    memcpy(ep->status_buf, buf, n);
		    ep->status_buf[n-1] = '\0';
		}
	    }
	}

	if (*sts == 0) {
	    /* assign pointers to individual lines in buffer */
	    curline = ep->status_buf;
	    /*
	     * Expecting something like ...
	     *
	     * Name:	bash
	     * State:	S (sleeping)
	     * Tgid:	21374
	     * Pid:	21374
	     * PPid:	21373
	     * TracerPid:	0
	     * Uid:	1000	1000	1000	1000
	     * Gid:	1000	1000	1000	1000
	     * FDSize:	256
	     * Groups:	24 25 27 29 30 44 46 105 110 112 1000 
	     * VmPeak:	   22388 kB
	     * VmSize:	   22324 kB
	     * VmLck:	       0 kB
	     * VmPin:	       0 kB
	     * VmHWM:	    5200 kB
	     * VmRSS:	    5200 kB
	     * VmData:	    3280 kB
	     * VmStk:	     136 kB
	     * VmExe:	     916 kB
	     * VmLib:	    2024 kB
	     * VmPTE:	      60 kB
	     * VmSwap:	       0 kB
	     * Threads:	1
	     * SigQ:	0/47779
	     * SigPnd:	0000000000000000
	     * ShdPnd:	0000000000000000
	     * SigBlk:	0000000000010000
	     * SigIgn:	0000000000384004
	     * SigCgt:	000000004b813efb
	     * CapInh:	0000000000000000
	     * CapPrm:	0000000000000000
	     * CapEff:	0000000000000000
	     * CapBnd:	ffffffffffffffff
	     * Cpus_allowed:	3
	     * Cpus_allowed_list:	0-1
	     * Mems_allowed:	00000000,00000001
	     * Mems_allowed_list:	0
	     * voluntary_ctxt_switches:	225
	     * nonvoluntary_ctxt_switches:	56
	     */
	    while (curline) {
		/* TODO VmPeak: VmPin: VmHWM: VmPTE: ... */
		/* small optimization ... peek at first character */
		switch (*curline) {
		    case 'U':
			if (strncmp(curline, "Uid:", 4) == 0)
			    ep->status_lines.uid = strsep(&curline, "\n");
			else
			    goto nomatch;
			break;
		    case 'G':
			if (strncmp(curline, "Gid:", 4) == 0)
			    ep->status_lines.gid = strsep(&curline, "\n");
			else
			    goto nomatch;
			break;
		    case 'V':
			if (strncmp(curline, "VmSize:", 7) == 0)
			    ep->status_lines.vmsize = strsep(&curline, "\n");
			else if (strncmp(curline, "VmLck:", 6) == 0)
			    ep->status_lines.vmlck = strsep(&curline, "\n");
			else if (strncmp(curline, "VmRSS:", 6) == 0)
			    ep->status_lines.vmrss = strsep(&curline, "\n");
			else if (strncmp(curline, "VmData:", 7) == 0)
			    ep->status_lines.vmdata = strsep(&curline, "\n");
			else if (strncmp(curline, "VmStk:", 6) == 0)
			    ep->status_lines.vmstk = strsep(&curline, "\n");
			else if (strncmp(curline, "VmExe:", 6) == 0)
			    ep->status_lines.vmexe = strsep(&curline, "\n");
			else if (strncmp(curline, "VmLib:", 6) == 0)
			    ep->status_lines.vmlib = strsep(&curline, "\n");
			else if (strncmp(curline, "VmSwap:", 7) == 0)
			    ep->status_lines.vmswap = strsep(&curline, "\n");
			else
			    goto nomatch;
			break;
		    case 'T':
			if (strncmp(curline, "Threads:", 8) == 0)
			    ep->status_lines.threads = strsep(&curline, "\n");
			else
			    goto nomatch;
			break;
		    case 'S':
			if (strncmp(curline, "SigPnd:", 7) == 0)
			    ep->status_lines.sigpnd = strsep(&curline, "\n");
			else if (strncmp(curline, "SigBlk:", 7) == 0)
			    ep->status_lines.sigblk = strsep(&curline, "\n");
			else if (strncmp(curline, "SigIgn:", 7) == 0)
			    ep->status_lines.sigign = strsep(&curline, "\n");
			else if (strncmp(curline, "SigCgt:", 7) == 0)
			    ep->status_lines.sigcgt = strsep(&curline, "\n");
			else
			    goto nomatch;
			break;
		    case 'v':
			if (strncmp(curline, "voluntary_ctxt_switches:", 24) == 0)
			    ep->status_lines.vctxsw = strsep(&curline, "\n");
			else
			    goto nomatch;
			break;
		    case 'n':
			if (strncmp(curline, "nonvoluntary_ctxt_switches:", 27) == 0)
			    ep->status_lines.nvctxsw = strsep(&curline, "\n");
			else
			    goto nomatch;
			break;
		    default:
nomatch:
#if PCP_DEBUG
			if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
			    char	*p;
			    fprintf(stderr, "fetch_proc_pid_status: skip ");
			    for (p = curline; *p && *p != '\n'; p++)
				fputc(*p, stderr);
			    fputc('\n', stderr);
			}
#endif
			curline = index(curline, '\n');
			if (curline != NULL) curline++;
		}
	    }
	    ep->flags |= PROC_PID_FLAG_STATUS_FETCHED;
	}
	if (fd >= 0)
	    close(fd);
    }

    return (*sts < 0) ? NULL : ep;
}

/*
 * fetch a proc/<pid>/statm entry for pid
 */
proc_pid_entry_t *
fetch_proc_pid_statm(int id, proc_pid_t *proc_pid, int *sts)
{
    __pmHashNode *node = __pmHashSearch(id, &proc_pid->pidhash);
    proc_pid_entry_t *ep;

    *sts = 0;
    if (node == NULL) {
#if PCP_DEBUG
	if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
	    char ibuf[1024];
	    fprintf(stderr, "fetch_proc_pid_statm: __pmHashSearch(%d, hash[%s]) -> NULL\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)));
	}
#endif
    	return NULL;
    }
    ep = (proc_pid_entry_t *)node->data;

    if (!(ep->flags & PROC_PID_FLAG_STATM_FETCHED)) {
	char buf[1024];
	int fd, n;

	if ((fd = proc_open("statm", ep)) < 0)
	    *sts = maperr();
	else if ((n = read(fd, buf, sizeof(buf))) < 0) {
	    *sts = maperr();
#if PCP_DEBUG
	    if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
		char ibuf[1024];
		char ebuf[1024];
		fprintf(stderr, "fetch_proc_pid_statm: read failed: id=%d, indom=%s, sts=%s\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)), pmErrStr_r(-oserror(), ebuf, sizeof(ebuf)));
	    }
#endif
	}
	else {
	    if (n == 0) {
		/* eh? */
		*sts = -ENODATA;
#if PCP_DEBUG
		if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
		    char ibuf[1024];
		    fprintf(stderr, "fetch_proc_pid_statm: read EOF?: id=%d, indom=%s\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)));
		}
#endif
	    }
	    else {
		if (ep->statm_buflen <= n) {
		    ep->statm_buflen = n;
		    ep->statm_buf = (char *)realloc(ep->statm_buf, n);
		}
		memcpy(ep->statm_buf, buf, n);
		ep->statm_buf[n-1] = '\0';
	    }
	}

	if (fd >= 0)
	    close(fd);
	ep->flags |= PROC_PID_FLAG_STATM_FETCHED;
    }

    return (*sts < 0) ? NULL : ep;
}


/*
 * fetch a proc/<pid>/maps entry for pid
 * WARNING: This can be very large!  Only ask for it if you really need it.
 * Added by Mike Mason <mmlnx@us.ibm.com>
 */
proc_pid_entry_t *
fetch_proc_pid_maps(int id, proc_pid_t *proc_pid, int *sts)
{
    __pmHashNode *node = __pmHashSearch(id, &proc_pid->pidhash);
    proc_pid_entry_t *ep;
    char *maps_bufptr = NULL;

    *sts = 0;
    if (node == NULL) {
#if PCP_DEBUG
	if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
	    char ibuf[1024];
	    fprintf(stderr, "fetch_proc_pid_maps: __pmHashSearch(%d, hash[%s]) -> NULL\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)));
	}
#endif
	return NULL;
    }
    ep = (proc_pid_entry_t *)node->data;

    if (!(ep->flags & PROC_PID_FLAG_MAPS_FETCHED)) {
	int fd;

	if ((fd = proc_open("maps", ep)) < 0)
	    *sts = maperr();
	else {
	    char buf[1024];
	    int n, len = 0;

	    while ((n = read(fd, buf, sizeof(buf))) > 0) {
		len += n;
		if (ep->maps_buflen <= len) {
		    ep->maps_buflen = len + 1;
		    ep->maps_buf = (char *)realloc(ep->maps_buf, ep->maps_buflen);
		}
		maps_bufptr = ep->maps_buf + len - n;
		memcpy(maps_bufptr, buf, n);
	    }
	    ep->flags |= PROC_PID_FLAG_MAPS_FETCHED;
	    /* If there are no maps, make maps_buf point to a zero length string. */
	    if (ep->maps_buflen == 0) {
		ep->maps_buf = (char *)malloc(1);
		ep->maps_buflen = 1;
	    }
	    ep->maps_buf[ep->maps_buflen - 1] = '\0';
	    close(fd);
	}
    }

    return (*sts < 0) ? NULL : ep;
}

/*
 * fetch a proc/<pid>/schedstat entry for pid
 */
proc_pid_entry_t *
fetch_proc_pid_schedstat(int id, proc_pid_t *proc_pid, int *sts)
{
    __pmHashNode *node = __pmHashSearch(id, &proc_pid->pidhash);
    proc_pid_entry_t *ep;

    *sts = 0;
    if (node == NULL) {
#if PCP_DEBUG
	if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
	    char ibuf[1024];
	    fprintf(stderr, "fetch_proc_pid_schedstat: __pmHashSearch(%d, hash[%s]) -> NULL\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)));
	}
#endif
    	return NULL;
    }
    ep = (proc_pid_entry_t *)node->data;

    if (!(ep->flags & PROC_PID_FLAG_SCHEDSTAT_FETCHED)) {
	int fd, n;
	char buf[1024];

	if ((fd = proc_open("schedstat", ep)) < 0)
	    *sts = maperr();
	else if ((n = read(fd, buf, sizeof(buf))) < 0) {
	    *sts = maperr();
#if PCP_DEBUG
	    if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
		char ibuf[1024];
		char ebuf[1024];
		fprintf(stderr, "fetch_proc_pid_schedstat: read failed: id=%d, indom=%s, sts=%s\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)), pmErrStr_r(-oserror(), ebuf, sizeof(ebuf)));
	    }
#endif
	}
	else {
	    if (n == 0) {
		/* eh? */
		*sts = -ENODATA;
#if PCP_DEBUG
		if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
		    char ibuf[1024];
		    fprintf(stderr, "fetch_proc_pid_schedstat: read EOF?: id=%d, indom=%s\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)));
		}
#endif
	    }
	    else {
		if (ep->schedstat_buflen <= n) {
		    ep->schedstat_buflen = n;
		    ep->schedstat_buf = (char *)realloc(ep->schedstat_buf, n);
		}
		memcpy(ep->schedstat_buf, buf, n);
		ep->schedstat_buf[n-1] = '\0';
	    }
	}

	if (fd >= 0) {
	    close(fd);
	}
	ep->flags |= PROC_PID_FLAG_SCHEDSTAT_FETCHED;
    }

    return (*sts < 0) ? NULL : ep;
}

/*
 * fetch a proc/<pid>/io entry for pid
 *
 * Depends on kernel built with CONFIG_TASK_IO_ACCOUNTING=y
 * which means the following must also be set:
 * CONFIG_TASKSTATS=y
 * CONFIG_TASK_DELAY_ACCT=y
 * CONFIG_TASK_XACCT=y
 */
proc_pid_entry_t *
fetch_proc_pid_io(int id, proc_pid_t *proc_pid, int *sts)
{
    __pmHashNode *node = __pmHashSearch(id, &proc_pid->pidhash);
    proc_pid_entry_t *ep;

    *sts = 0;
    if (node == NULL) {
#if PCP_DEBUG
	if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
	    char ibuf[1024];
	    fprintf(stderr, "fetch_proc_pid_io: __pmHashSearch(%d, hash[%s]) -> NULL\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)));
	}
#endif
	return NULL;
    }
    ep = (proc_pid_entry_t *)node->data;

    if (!(ep->flags & PROC_PID_FLAG_IO_FETCHED)) {
	int	fd, n;
	char	buf[1024];
	char	*curline;

	if ((fd = proc_open("io", ep)) < 0)
	    *sts = maperr();
	else if ((n = read(fd, buf, sizeof(buf))) < 0) {
	    *sts = maperr();
#if PCP_DEBUG
	    if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
		char ibuf[1024];
		char ebuf[1024];
		fprintf(stderr, "fetch_proc_pid_io: read failed: id=%d, indom=%s, sts=%s\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)), pmErrStr_r(-oserror(), ebuf, sizeof(ebuf)));
	    }
#endif
	}
	else {
	    if (n == 0) {
		*sts = -ENODATA;
#if PCP_DEBUG
		if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
		    char ibuf[1024];
		    fprintf(stderr, "fetch_proc_pid_io: read EOF?: id=%d, indom=%s\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)));
		}
#endif
	    }
	    else {
		if (ep->io_buflen < n) {
		    ep->io_buflen = n;
		    ep->io_buf = (char *)realloc(ep->io_buf, n);
		}

		if (ep->io_buf == NULL) {
		    *sts = -ENODATA;
#if PCP_DEBUG
		    if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
			char ibuf[1024];
			fprintf(stderr, "fetch_proc_pid_io: read EOF?: id=%d, indom=%s\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)));
		    }
#endif
	    }
		else {
		    memcpy(ep->io_buf, buf, n);
		    ep->io_buf[n-1] = '\0';
		}
	    }
	}

	if (*sts == 0) {
	    /* assign pointers to individual lines in buffer */
	    curline = ep->io_buf;
	    /*
	     * expecting 
	     * rchar: 714415843
	     * wchar: 101078796
	     * syscr: 780339
	     * syscw: 493583
	     * read_bytes: 209099776
	     * write_bytes: 118263808
	     * cancelled_write_bytes: 102301696
	    */
	    while (curline) {
		if (strncmp(curline, "rchar:", 6) == 0)
		    ep->io_lines.rchar = strsep(&curline, "\n");
		else if (strncmp(curline, "wchar:", 6) == 0)
		    ep->io_lines.wchar = strsep(&curline, "\n");
		else if (strncmp(curline, "syscr:", 6) == 0)
		    ep->io_lines.syscr = strsep(&curline, "\n");
		else if (strncmp(curline, "syscw:", 6) == 0)
		    ep->io_lines.syscw = strsep(&curline, "\n");
		else if (strncmp(curline, "read_bytes:", 11) == 0)
		    ep->io_lines.readb = strsep(&curline, "\n");
		else if (strncmp(curline, "write_bytes:", 12) == 0)
		    ep->io_lines.writeb = strsep(&curline, "\n");
		else if (strncmp(curline, "cancelled_write_bytes:", 22) == 0)
		    ep->io_lines.cancel = strsep(&curline, "\n");
		else {
#if PCP_DEBUG
		    if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
			char	*p;
			fprintf(stderr, "fetch_proc_pid_io: skip ");
			for (p = curline; *p && *p != '\n'; p++)
			    fputc(*p, stderr);
			fputc('\n', stderr);
		    }
#endif
		    curline = index(curline, '\n');
		    if (curline != NULL) curline++;
		}
	    }
	    ep->flags |= PROC_PID_FLAG_IO_FETCHED;
	}
	if (fd >= 0)
	    close(fd);
    }

    return (*sts < 0) ? NULL : ep;
}

/*
 * fetch a proc/<pid>/fd entry for pid
 */
proc_pid_entry_t *
fetch_proc_pid_fd(int id, proc_pid_t *proc_pid, int *sts)
{
    __pmHashNode *node = __pmHashSearch(id, &proc_pid->pidhash);
    proc_pid_entry_t *ep;

    if (node == NULL) {
#if PCP_DEBUG
	if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
	    char ibuf[1024];
	    fprintf(stderr, "fetch_proc_pid_fd: __pmHashSearch(%d, hash[%s]) -> NULL\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)));
	}
#endif
	return NULL;
    }
    ep = (proc_pid_entry_t *)node->data;

    if (!(ep->flags & PROC_PID_FLAG_FD_FETCHED)) {
	uint32_t de_count = 0;
	DIR	*dir = proc_opendir("fd", ep);

	if (dir == NULL) {
	    *sts = maperr();
	    return NULL;
	}
	while (readdir(dir) != NULL) {
	    de_count++;
	}
	closedir(dir);
	ep->fd_count = de_count - 2; /* subtract cwd and parent entries */
	ep->flags |= PROC_PID_FLAG_FD_FETCHED;
    }

    return ep;
}

/*
 * From the kernel format for a single process cgroup set:
 *     2:cpu:/
 *     1:cpuset:/
 *
 * Produce the same one-line format string that "ps" uses:
 *     "cpu:/;cpuset:/"
 */
static void
proc_cgroup_reformat(char *buf, int len, char *fmt)
{
    char *target = fmt, *p, *s = NULL;

    *target = '\0';
    for (p = buf; p - buf < len; p++) {
	if (*p == '\0')
	    break;
        if (*p == ':' && !s)	/* position "s" at start */
            s = p + 1;
        if (*p != '\n' || !s)	/* find end of this line */
            continue;
        if (target != fmt)      /* not the first cgroup? */
            strncat(target, ";", 2);
	/* have a complete cgroup line now, copy it over */
        strncat(target, s, (p - s));
        target += (p - s);
        s = NULL;		/* reset it for new line */
    }
}

/*
 * fetch a proc/<pid>/cgroup entry for pid
 */
proc_pid_entry_t *
fetch_proc_pid_cgroup(int id, proc_pid_t *proc_pid, int *sts)
{
    __pmHashNode *node = __pmHashSearch(id, &proc_pid->pidhash);
    proc_pid_entry_t *ep;

    *sts = 0;
    if (node == NULL) {
#if PCP_DEBUG
	if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
	    char ibuf[1024];
	    fprintf(stderr, "fetch_proc_pid_cgroup: __pmHashSearch(%d, hash[%s]) -> NULL\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)));
	}
#endif
	return NULL;
    }
    ep = (proc_pid_entry_t *)node->data;

    if (!(ep->flags & PROC_PID_FLAG_CGROUP_FETCHED)) {
	char	buf[1024];
	char	fmt[1024];
	int	n, fd;

	if ((fd = proc_open("cgroup", ep)) < 0)
	    *sts = maperr();
	else if ((n = read(fd, buf, sizeof(buf))) < 0) {
	    *sts = maperr();
#if PCP_DEBUG
	    if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
		char ibuf[1024];
		char ebuf[1024];
		fprintf(stderr, "fetch_proc_pid_cgroup: read failed: id=%d, indom=%s, sts=%s\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)), pmErrStr_r(-oserror(), ebuf, sizeof(ebuf)));
	    }
#endif
	}
	else {
	    if (n == 0) {
		*sts = -ENODATA;
#if PCP_DEBUG
		if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
		    char ibuf[1024];
		    fprintf(stderr, "fetch_proc_pid_cgroup: read EOF?: id=%d, indom=%s\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)));
		}
#endif
	    }
	    else {
		/* reformat the buffer to match "ps" output format, then hash */
		proc_cgroup_reformat(&buf[0], n, &fmt[0]);
		ep->cgroup_id = proc_strings_insert(fmt);
	    }
	}
	if (fd >= 0)
	    close(fd);
	ep->flags |= PROC_PID_FLAG_CGROUP_FETCHED;
    }

    return (*sts < 0) ? NULL : ep;
}

/*
 * fetch a proc/<pid>/attr/current entry for pid
 */
proc_pid_entry_t *
fetch_proc_pid_label(int id, proc_pid_t *proc_pid, int *sts)
{
    __pmHashNode *node = __pmHashSearch(id, &proc_pid->pidhash);
    proc_pid_entry_t *ep;

    *sts = 0;
    if (node == NULL) {
#if PCP_DEBUG
	if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
	    char ibuf[1024];
	    fprintf(stderr, "fetch_proc_pid_label: __pmHashSearch(%d, hash[%s]) -> NULL\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)));
	}
#endif
	return NULL;
    }
    ep = (proc_pid_entry_t *)node->data;

    if (!(ep->flags & PROC_PID_FLAG_LABEL_FETCHED)) {
	char	buf[1024];
	int	n, fd;

	if ((fd = proc_open("attr/current", ep)) < 0)
	    *sts = maperr();
	else if ((n = read(fd, buf, sizeof(buf))) < 0) {
	    *sts = maperr();
#if PCP_DEBUG
	    if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
		char ibuf[1024];
		char ebuf[1024];
		fprintf(stderr, "fetch_proc_pid_label: read failed: id=%d, indom=%s, sts=%s\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)), pmErrStr_r(-oserror(), ebuf, sizeof(ebuf)));
	    }
#endif
	}
	else {
	    if (n == 0) {
		*sts = -ENODATA;
#if PCP_DEBUG
		if ((pmDebug & (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) == (DBG_TRACE_LIBPMDA|DBG_TRACE_DESPERATE)) {
		    char ibuf[1024];
		    fprintf(stderr, "fetch_proc_pid_label: read EOF?: id=%d, indom=%s\n", id, pmInDomStr_r(proc_pid->indom->it_indom, ibuf, sizeof(ibuf)));
		}
#endif
	    } else {
		/* buffer matches "ps" output format, direct hash */
		buf[sizeof(buf)-1] = '\0';
		ep->label_id = proc_strings_insert(buf);
	    }
	}
	if (fd >= 0)
	    close(fd);
	ep->flags |= PROC_PID_FLAG_LABEL_FETCHED;
    }

    return (*sts < 0) ? NULL : ep;
}

/*
 * Extract the ith (space separated) field from a char buffer.
 * The first field starts at zero. 
 * BEWARE: return copy is in a static buffer.
 */
char *
_pm_getfield(char *buf, int field)
{
    static int retbuflen = 0;
    static char *retbuf = NULL;
    char *p;
    int i;

    if (buf == NULL)
    	return NULL;

    for (p=buf, i=0; i < field; i++) {
	/* skip to the next space */
    	for (; *p && !isspace((int)*p); p++) {;}

	/* skip to the next word */
    	for (; *p && isspace((int)*p); p++) {;}
    }

    /* return a null terminated copy of the field */
    for (i=0; ; i++) {
	if (isspace((int)p[i]) || p[i] == '\0' || p[i] == '\n')
	    break;
    }

    if (i >= retbuflen) {
	retbuflen = i+4;
	retbuf = (char *)realloc(retbuf, retbuflen);
    }
    memcpy(retbuf, p, i);
    retbuf[i] = '\0';

    return retbuf;
}
