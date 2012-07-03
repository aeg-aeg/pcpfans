/*
 * Copyright (c) 1995-2002 Silicon Graphics, Inc.  All Rights Reserved.
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
 */

#include "pmapi.h"
#include "impl.h"

/*
 * PDU for pmLookupText request (PDU_TEXT_REQ)
 */
typedef struct {
    __pmPDUHdr	hdr;
    int		ident;
    int		type;		/* one line or help, PMID or InDom */
} text_req_t;

int
__pmSendTextReq(int fd, int from, int ident, int type)
{
    text_req_t	*pp;
    int		sts;

    if ((pp = (text_req_t *)__pmFindPDUBuf(sizeof(text_req_t))) == NULL)
	return -oserror();
    pp->hdr.len = sizeof(text_req_t);
    pp->hdr.type = PDU_TEXT_REQ;
    pp->hdr.from = from;

    if (type & PM_TEXT_PMID)
	pp->ident = __htonpmID((pmID)ident);
    else if (type & PM_TEXT_INDOM)
	pp->ident = __htonpmInDom((pmInDom)ident);
    else
	pp->ident = __htonpmInDom((pmInDom)ident);

    pp->type = htonl(type);

    sts = __pmXmitPDU(fd, (__pmPDU *)pp);
    __pmUnpinPDUBuf(pp);
    return sts;
}

int
__pmDecodeTextReq(__pmPDU *pdubuf, int *ident, int *type)
{
    text_req_t	*pp;

    pp = (text_req_t *)pdubuf;
    *type = ntohl(pp->type);

    if ((*type) & PM_TEXT_PMID)
	*ident = __ntohpmID(pp->ident);
    else
    if ((*type) & PM_TEXT_INDOM)
	*ident = __ntohpmInDom(pp->ident);

    return 0;
}

/*
 * PDU for pmLookupText result (PDU_TEXT)
 */
typedef struct {
    __pmPDUHdr	hdr;
    int		ident;
    int		buflen;			/* no. of chars following */
    char	buffer[sizeof(int)];	/* desired text */
} text_t;

int
__pmSendText(int fd, int ctx, int ident, const char *buffer)
{
    text_t	*pp;
    size_t	need;
    int		sts;

    need = sizeof(text_t) - sizeof(pp->buffer) + PM_PDU_SIZE_BYTES(strlen(buffer));
    if ((pp = (text_t *)__pmFindPDUBuf((int)need)) == NULL)
	return -oserror();
    pp->hdr.len = (int)need;
    pp->hdr.type = PDU_TEXT;
    pp->hdr.from = ctx;
    /*
     * Note: ident argument must already be in network byte order.
     * The caller has to do this because the type of ident is not
     * part of the transmitted PDU_TEXT pdu; ident may be either
     * a pmID or pmInDom, and so the caller must use either
     * __htonpmID or __htonpmInDom (respectfully).
     */
    pp->ident = ident;

    pp->buflen = (int)strlen(buffer);
    memcpy((void *)pp->buffer, (void *)buffer, pp->buflen);
    pp->buflen = htonl(pp->buflen);

    sts = __pmXmitPDU(fd, (__pmPDU *)pp);
    __pmUnpinPDUBuf(pp);
    return sts;
}

int
__pmDecodeText(__pmPDU *pdubuf, int *ident, char **buffer)
{
    text_t	*pp;
    int		buflen;

    pp = (text_t *)pdubuf;
    /*
     * Note: ident argument is returned in network byte order.
     * The caller has to convert it to host byte order because
     * the type of ident is not part of the transmitted PDU_TEXT
     * pdu (ident may be either a pmID or a pmInDom. The caller
     * must use either __ntohpmID() or __ntohpmInDom(), respectfully.
     */
    *ident = pp->ident;
    buflen = ntohl(pp->buflen);
    if ((*buffer = (char *)malloc(buflen+1)) == NULL)
	return -oserror();
    strncpy(*buffer, pp->buffer, buflen);
    (*buffer)[buflen] = '\0';
    return 0;
}
