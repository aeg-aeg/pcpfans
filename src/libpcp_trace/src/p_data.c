/*
 * Copyright (c) 1997-2002 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * Assumptions:
 *	1. "double" is 64-bits
 *	2. "double" is IEEE format and needs endian conversion (like a
 *	   64-bit integer, but no other format conversion
 */

#include "pmapi.h"
#include "impl.h"
#include "trace.h"
#include "trace_dev.h"

/*
 * PDU for all trace data updates (TRACE_PDU_DATA)
 *
 * note "taglen" includes the null-byte terminator
 */
typedef struct {
    __pmTracePDUHdr		hdr;
    struct {
#ifdef HAVE_BITFIELDS_LTOR
	unsigned int	version  : 8;
	unsigned int	taglen   : 8;
	unsigned int	tagtype  : 8;
	unsigned int	protocol : 1;
	unsigned int	pad      : 7;
#else
	unsigned int	pad      : 7;
	unsigned int	protocol : 1;
	unsigned int	tagtype  : 8;
	unsigned int	taglen   : 8;
	unsigned int	version  : 8;
#endif
    }				bits;
    /*
     * avoid struct padding traps!
     *
     * what really follows is a double (data) and then the tag
     */
} tracedata_t;

int
__pmtracesenddata(int fd, char *tag, int taglen, int tagtype, double data)
{
    tracedata_t	*pp = NULL;
    size_t	need = 0;
    char	*cp;
    int		*ip;

    if (taglen <= 0)
	return PMTRACE_ERR_IPC;
    else if (__pmstate & PMTRACE_STATE_NOAGENT) {
	fprintf(stderr, "__pmtracesenddata: sending data (skipped)\n");
	return 0;
    }

    /*
     * pad to the next __pmTracePDU boundary
     */
    need = sizeof(tracedata_t) + sizeof(double) + sizeof(__pmTracePDU)*((taglen - 1 + sizeof(__pmTracePDU))/sizeof(__pmTracePDU));

    if ((pp = (tracedata_t *)__pmtracefindPDUbuf((int)need)) == NULL)
	return -oserror();
    pp->hdr.len = (int)need;
    pp->hdr.type = TRACE_PDU_DATA;
    pp->bits.taglen = taglen;
    pp->bits.tagtype = tagtype;
    pp->bits.version = TRACE_PDU_VERSION;
    if (__pmtraceprotocol(TRACE_PROTOCOL_QUERY) == TRACE_PROTOCOL_SYNC)
	pp->bits.protocol = 1;
    else
	pp->bits.protocol = 0;
    pp->bits.pad = 0;
    ip = (int *)&pp->bits;
    *ip = htonl(*ip);

    cp = (char *)pp;
    cp += sizeof(tracedata_t);
    memcpy((void *)cp, (void *)&data, sizeof(double));
    __htonll((char *)cp);	/* send in network byte order */
    cp += sizeof(double);
    strcpy(cp, tag);
#ifdef PCP_DEBUG
    if ((taglen % sizeof(__pmTracePDU)) != 0) {
	/* for Purify */
	int	pad;
	char	*padp = cp + taglen;
	for (pad = sizeof(__pmTracePDU) - 1; pad >= (taglen % sizeof(__pmTracePDU)); pad--)
	    *padp++ = '~';	/* buffer end */
    }
#endif

#ifdef PMTRACE_DEBUG
    if (__pmstate & PMTRACE_STATE_PDU)
	fprintf(stderr, "__pmtracesenddata(tag=\"%s\", data=%f)\n", tag, data);
#endif

    return __pmtracexmitPDU(fd, (__pmTracePDU *)pp);
}

int
__pmtracedecodedata(__pmTracePDU *pdubuf, char **tag, int *taglen,
				int *tagtype, int *protocol, double *data)
{
    tracedata_t	*pp;
    int		*ip;
    char	*cp;

    if (pdubuf == NULL)
	return PMTRACE_ERR_IPC;

    pp = (tracedata_t *)pdubuf;
    ip = (int *)&pp->bits;
    *ip = ntohl(*ip);
    if (pp->bits.taglen == 0)
	return PMTRACE_ERR_IPC;
    if (pp->bits.version != TRACE_PDU_VERSION)
	return PMTRACE_ERR_VERSION;
    *taglen = pp->bits.taglen;
    *tagtype = pp->bits.tagtype;
    *protocol = pp->bits.protocol;

    cp = (char *)pp;
    cp += sizeof(tracedata_t);
    memcpy((void *)data, (void *)cp, sizeof(double));
    __ntohll((char *)data);	/* receive in network byte order */
    cp += sizeof(double);
    *tag = cp;

#ifdef PMTRACE_DEBUG
    if (__pmstate & PMTRACE_STATE_PDU)
	fprintf(stderr, "__pmtracedecodedata -> tag=\"%s\" data=%f\n", *tag, *data);
#endif
    return 0;
}
