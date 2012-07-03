/*
 * Copyright (c) 2008-2011 Aconex.  All Rights Reserved.
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

#include "local.h"
#include <dirent.h>
#include <search.h>
#include <sys/stat.h>
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

static timers_t *timers;
static int ntimers;
static files_t *files;
static int nfiles;

char *
local_strdup_suffix(const char *string, const char *suffix)
{
    size_t length = strlen(string) + strlen(suffix) + 1;
    char *result = malloc(length);

    if (!result)
	return result;
    sprintf(result, "%s%s", string, suffix);
    return result;
}

char *
local_strdup_prefix(const char *prefix, const char *string)
{
    size_t length = strlen(prefix) + strlen(string) + 1;
    char *result = malloc(length);

    if (!result)
	return result;
    sprintf(result, "%s%s", prefix, string);
    return result;
}

int
local_user(const char *username)
{
#ifdef HAVE_GETPWNAM
    /* lose root privileges if we have them */
    struct passwd *pw;

    if ((pw = getpwnam(username)) == 0) {
	__pmNotifyErr(LOG_WARNING,
			"cannot find the user %s to switch to\n", username);
	return -1;
    }
    if (setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0) {
	__pmNotifyErr(LOG_WARNING,
			"cannot switch to uid/gid of user %s\n", username);
	return -1;
    }
    return 0;
#else
    __pmNotifyErr(LOG_WARNING, "cannot switch to user %s\n", username);
    return -1;
#endif
}

int
local_timer(double timeout, scalar_t *callback, int cookie)
{
    int size = sizeof(*timers) * (ntimers + 1);
    delta_t delta;

    delta.tv_sec = (time_t)timeout;
    delta.tv_usec = (long)((timeout - (double)delta.tv_sec) * 1000000.0);

    if ((timers = realloc(timers, size)) == NULL)
	__pmNoMem("timers resize", size, PM_FATAL_ERR);
    timers[ntimers].id = -1;	/* not yet registered */
    timers[ntimers].delta = delta;
    timers[ntimers].cookie = cookie;
    timers[ntimers].callback = callback;
    return ntimers++;
}

int
local_timer_get_cookie(int id)
{
    int i;

    for (i = 0; i < ntimers; i++)
	if (timers[i].id == id)
	    return timers[i].cookie;
    return -1;
}

scalar_t *
local_timer_get_callback(int id)
{
    int i;

    for (i = 0; i < ntimers; i++)
	if (timers[i].id == id)
	    return timers[i].callback;
    return NULL;
}

static int
local_file(int type, int fd, scalar_t *callback, int cookie)
{
    int size = sizeof(*files) * (nfiles + 1);

    if ((files = realloc(files, size)) == NULL)
	__pmNoMem("files resize", size, PM_FATAL_ERR);
    files[nfiles].type = type;
    files[nfiles].fd = fd;
    files[nfiles].cookie = cookie;
    files[nfiles].callback = callback;
    return nfiles++;
}

int
local_pipe(char *pipe, scalar_t *callback, int cookie)
{
    FILE *fp = popen(pipe, "r");
    int me;

#if defined(HAVE_SIGPIPE)
    signal(SIGPIPE, SIG_IGN);
#endif
    if (!fp) {
	__pmNotifyErr(LOG_ERR, "popen failed (%s): %s", pipe, osstrerror());
	exit(1);
    }
    me = local_file(FILE_PIPE, fileno(fp), callback, cookie);
    files[me].me.pipe.file = fp;
    return fileno(fp);
}

int
local_tail(char *file, scalar_t *callback, int cookie)
{
    int fd = open(file, O_RDONLY | O_NDELAY);
    struct stat stats;
    int me;

    if (fd < 0) {
	__pmNotifyErr(LOG_ERR, "open failed (%s): %s", file, osstrerror());
	exit(1);
    }
    if (fstat(fd, &stats) < 0) {
	__pmNotifyErr(LOG_ERR, "fstat failed (%s): %s", file, osstrerror());
	exit(1);
    }
    lseek(fd, 0L, SEEK_END);
    me = local_file(FILE_TAIL, fd, callback, cookie);
    files[me].me.tail.path = strdup(file);
    files[me].me.tail.dev = stats.st_dev;
    files[me].me.tail.ino = stats.st_ino;
    return me;
}

int
local_sock(char *host, int port, scalar_t *callback, int cookie)
{
    struct sockaddr_in myaddr;
    struct hostent *servinfo;
    int me, fd;

    if ((servinfo = gethostbyname(host)) == NULL) {
	__pmNotifyErr(LOG_ERR, "gethostbyname (%s): %s", host, netstrerror());
	exit(1);
    }
    if ((fd = __pmCreateSocket()) < 0) {
	__pmNotifyErr(LOG_ERR, "socket (%s): %s", host, netstrerror());
	exit(1);
    }
    memset(&myaddr, 0, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    memcpy(&myaddr.sin_addr, servinfo->h_addr, servinfo->h_length);
    myaddr.sin_port = htons(port);
    if (connect(fd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
	__pmNotifyErr(LOG_ERR, "connect (%s): %s", host, netstrerror());
	exit(1);
    }
    me = local_file(FILE_SOCK, fd, callback, cookie);
    files[me].me.sock.host = strdup(host);
    files[me].me.sock.port = port;
    return me;
}

static char *
local_filetype(int type)
{
    if (type == FILE_SOCK)
	return "socket connection";
    if (type == FILE_PIPE)
	return "command pipe";
    if (type == FILE_TAIL)
	return "tailed file";
    return NULL;
}

int
local_files_get_descriptor(int id)
{
    if (id < 0 || id >= nfiles)
	return -1;
    return files[id].fd;
}

void
local_atexit(void)
{
    while (ntimers > 0) {
	--ntimers;
	__pmAFunregister(timers[ntimers].id);
    }
    if (timers) {
	free(timers);
	timers = NULL;
    }
    while (nfiles > 0) {
	--nfiles;
	if (files[nfiles].type == FILE_PIPE)
	    pclose(files[nfiles].me.pipe.file);
	if (files[nfiles].type == FILE_TAIL) {
	    close(files[nfiles].fd);
	    if (files[nfiles].me.tail.path)
		free(files[nfiles].me.tail.path);
	    files[nfiles].me.tail.path = NULL;
	}
	if (files[nfiles].type == FILE_SOCK) {
	    __pmCloseSocket(files[nfiles].fd);
	    if (files[nfiles].me.sock.host)
		free(files[nfiles].me.sock.host);
	    files[nfiles].me.sock.host = NULL;
	}
    }
    if (files) {
	free(files);
	files = NULL;
    }
    /* take out any children we created */
#ifdef HAVE_SIGNAL
    signal(SIGTERM, SIG_IGN);
#endif
    __pmProcessTerminate((pid_t)0, 0);
}

static void
local_log_rotated(files_t *file)
{
    struct stat stats;

    if (stat(file->me.tail.path, &stats) < 0)
	return;
    if (stats.st_ino == file->me.tail.ino && stats.st_dev == file->me.tail.dev)
	return;

    close(file->fd);
    file->fd = open(file->me.tail.path, O_RDONLY | O_NDELAY);
    if (file->fd < 0) {
	__pmNotifyErr(LOG_ERR, "open failed after log rotate (%s): %s",
			file->me.tail.path, osstrerror());
	return;
    }
    files->me.tail.dev = stats.st_dev;
    files->me.tail.ino = stats.st_ino;
}

static void
local_reconnector(files_t *file)
{
    struct sockaddr_in myaddr;
    struct hostent *servinfo;
    int fd;

    if (file->fd >= 0)		/* reconnect-needed flag */
	return;
    if ((servinfo = gethostbyname(file->me.sock.host)) == NULL)
	return;
    if ((fd = __pmCreateSocket()) < 0)
	return;
    memset(&myaddr, 0, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    memcpy(&myaddr.sin_addr, servinfo->h_addr, servinfo->h_length);
    myaddr.sin_port = htons(files->me.sock.port);
    if (connect(fd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
	close(fd);
	return;
    }
    files->fd = fd;
}

static void
local_connection(files_t *file)
{
    if (file->type == FILE_TAIL)
	local_log_rotated(file);
    else if (file->type == FILE_TAIL)
	local_reconnector(file);
}

void
local_pmdaMain(pmdaInterface *self)
{
    static char buffer[4096];
    int pmcdfd, nready, nfds, i, j, count, fd, maxfd = -1;
    fd_set fds, readyfds;
    ssize_t bytes;
    size_t offset;
    char *s, *p;

    if ((pmcdfd = __pmdaInFd(self)) < 0)
	exit(1);

    for (i = 0; i < ntimers; i++)
	timers[i].id = __pmAFregister(&timers[i].delta, &timers[i].cookie,
					timer_callback);

    /* custom PMDA main loop */
    for (count = 0; ; count++) {
	struct timeval timeout = { 1, 0 };

	FD_ZERO(&fds);
	FD_SET(pmcdfd, &fds);
	for (i = 0; i < nfiles; i++) {
	    if (files[i].type == FILE_TAIL)
		continue;
	    fd = files[i].fd;
	    FD_SET(fd, &fds);
	    if (fd > maxfd)
		maxfd = fd;
	}
	nfds = ((pmcdfd > maxfd) ? pmcdfd : maxfd) + 1;

	memcpy(&readyfds, &fds, sizeof(readyfds));
	nready = select(nfds, &readyfds, NULL, NULL, &timeout);
	if (nready < 0) {
	    if (neterror() != EINTR) {
		__pmNotifyErr(LOG_ERR, "select failed: %s\n",
				netstrerror());
		exit(1);
	    }
	    continue;
	}

	__pmAFblock();

	if (FD_ISSET(pmcdfd, &readyfds)) {
	    if (__pmdaMainPDU(self) < 0) {
		__pmAFunblock();
		exit(1);
	    }
	}

	for (i = 0; i < nfiles; i++) {
	    fd = files[i].fd;
	    /* check for log rotation or host reconnection needed */
	    if ((count % 10) == 0)	/* but only once every 10 */
		local_connection(&files[i]);
	    if (files[i].type != FILE_TAIL && !(FD_ISSET(fd, &readyfds)))
		continue;
	    offset = 0;
multiread:
	    bytes = read(fd, buffer + offset, sizeof(buffer)-1 - offset);
	    if (bytes < 0) {
		if ((files[i].type == FILE_TAIL) &&
		    (oserror() == EINTR) ||
		    (oserror() == EAGAIN) ||
		    (oserror() == EWOULDBLOCK))
		    continue;
		if (files[i].type == FILE_SOCK) {
		    close(files[i].fd);
		    files[i].fd = -1;
		    continue;
		}
		__pmNotifyErr(LOG_ERR, "Data read error on %s: %s\n",
				local_filetype(files[i].type), osstrerror());
		exit(1);
	    }
	    if (bytes == 0) {
		if (files[i].type == FILE_TAIL)
		    continue;
		__pmNotifyErr(LOG_ERR, "No data to read - %s may be closed\n",
				local_filetype(files[i].type));
		exit(1);
	    }
	    buffer[sizeof(buffer)-1] = '\0';
	    for (s = p = buffer, j = 0;
		 *s != '\0' && j < sizeof(buffer)-1;
		 s++, j++) {
		if (*s != '\n')
		    continue;
		*s = '\0';
		/*__pmNotifyErr(LOG_INFO, "Input callback: %s\n", p);*/
		input_callback(files[i].callback, files[i].cookie, p);
		p = s + 1;
	    }
	    if (files[i].type == FILE_TAIL) {
		/* did we just do a full buffer read? */
		if (p == buffer) {
		    __pmNotifyErr(LOG_ERR, "Ignoring long line: \"%s\"\n", p);
		} else if (j == sizeof(buffer) - 1) {
		    offset = sizeof(buffer)-1 - (p - buffer);
		    memmove(buffer, p, offset); 
		    goto multiread;	/* read rest of line */
		}
	    }
	}

	__pmAFunblock();
    }
}
