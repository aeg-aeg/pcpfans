/*
 * Copyright (c) 1995,2004 Silicon Graphics, Inc.  All Rights Reserved.
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
#include "oldpmapi.h"
#include <ctype.h>

/*
 * PDU for general error reporting (PDU_ERROR)
 */
typedef struct {
    __pmPDUHdr	hdr;
    int		code;		/* error code */
} p_error_t;

/*
 * and the extended variant, with a second datum word
 */
typedef struct {
    __pmPDUHdr	hdr;
    int		code;		/* error code */
    int		datum;		/* additional information */
} x_error_t;

int
__pmSendError(int fd, int from, int code)
{
    p_error_t	*pp;
    int		sts;

    if ((pp = (p_error_t *)__pmFindPDUBuf(sizeof(p_error_t))) == NULL)
	return -oserror();
    pp->hdr.len = sizeof(p_error_t);
    pp->hdr.type = PDU_ERROR;
    pp->hdr.from = from;

    pp->code = code;

#ifdef PCP_DEBUG
    if (pmDebug & DBG_TRACE_CONTEXT)
	fprintf(stderr,
		"__pmSendError: sending error PDU (code=%d, toversion=%d)\n",
		pp->code, __pmVersionIPC(fd));
#endif

    pp->code = htonl(pp->code);

    sts = __pmXmitPDU(fd, (__pmPDU *)pp);
    __pmUnpinPDUBuf(pp);
    return sts;
}

int
__pmSendXtendError(int fd, int from, int code, int datum)
{
    x_error_t	*pp;
    int		sts;

    if ((pp = (x_error_t *)__pmFindPDUBuf(sizeof(x_error_t))) == NULL)
	return -oserror();
    pp->hdr.len = sizeof(x_error_t);
    pp->hdr.type = PDU_ERROR;
    pp->hdr.from = from;

    /*
     * It is ALWAYS a PCP 1.x error code here ... this was required
     * to support migration from the V1 to V2 protocols when a V2 pmcd
     * (who is the sole user of this routine) supported connections
     * from both V1 and V2 PMAPI clients ... for the same reason we
     * cannot retire this translation, even when the V1 protocols are
     * no longer supported in all other IPC cases.
     *
     * For most common cases, code is 0 so it makes no difference.
     */
    pp->code = htonl(XLATE_ERR_2TO1(code));

    pp->datum = datum; /* NOTE: caller must swab this */

    sts = __pmXmitPDU(fd, (__pmPDU *)pp);
    __pmUnpinPDUBuf(pp);
    return sts;
}

int
__pmDecodeError(__pmPDU *pdubuf, int *code)
{
    p_error_t	*pp;

    pp = (p_error_t *)pdubuf;
    *code = ntohl(pp->code);
#ifdef PCP_DEBUG
    if (pmDebug & DBG_TRACE_CONTEXT)
	fprintf(stderr,
		"__pmDecodeError: got error PDU (code=%d, fromversion=%d)\n",
		*code, __pmLastVersionIPC());
#endif
    return 0;
}

int
__pmDecodeXtendError(__pmPDU *pdubuf, int *code, int *datum)
{
    x_error_t	*pp = (x_error_t *)pdubuf;
    int		sts;

    /*
     * It is ALWAYS a PCP 1.x error code here ... see note above
     * in __pmSendXtendError()
     */
    *code = XLATE_ERR_1TO2((int)ntohl(pp->code));

    if (pp->hdr.len == sizeof(x_error_t)) {
	/* really version 2 extended error PDU */
	sts = PDU_VERSION2;
	*datum = pp->datum; /* NOTE: caller must swab this */
    }
    else {
	sts = PM_ERR_IPC;
    }
#ifdef PCP_DEBUG
    if (pmDebug & DBG_TRACE_CONTEXT)
	fprintf(stderr, "__pmDecodeXtendError: "
		"got error PDU (code=%d, datum=%d, version=%d)\n",
		*code, *datum, sts);
#endif

    return sts;
}
