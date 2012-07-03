/*
 * Copyright (c) 2000,2004 Silicon Graphics, Inc.  All Rights Reserved.
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
 * PDU for __pmControlLogging request (PDU_LOG_CONTROL)
 */

typedef struct {
    pmID		v_pmid;
    int			v_numval;	/* no. of vlist els to follow */
    __pmValue_PDU	v_list[1];	/* one or more */
} vlist_t;

typedef struct {
    __pmPDUHdr		c_hdr;
    int			c_control;	/* mandatory or advisory */
    int			c_state;	/* off, maybe or on */
    int			c_delta;	/* requested logging interval (msec) */
    int			c_numpmid;	/* no. of vlist_ts to follow */
    __pmPDU		c_data[1];	/* one or more */
} control_req_t;

int
__pmSendLogControl(int fd, const pmResult *request, int control, int state, int delta)
{
    pmValueSet		*vsp;
    int			i;
    int			j;
    control_req_t	*pp;
    int			need;
    vlist_t		*vp;
    int			sts;

#ifdef PCP_DEBUG
    if (pmDebug & DBG_TRACE_PDU)
	__pmDumpResult(stderr, request);
#endif

    /* advisory+maybe logging and retrospective logging (delta < 0) are not
     *permitted
     */
    if (delta < 0 ||
	(control == PM_LOG_ADVISORY && state == PM_LOG_MAYBE))
	return -EINVAL;

    /* PDU header, control, state and count of metrics */
    need = sizeof(control_req_t) - sizeof(pp->c_data);
    for (i = 0; i < request->numpmid; i++) {
	/* plus PMID and count of values */
	if (request->vset[i]->numval > 0)
	    need += sizeof(vlist_t) + (request->vset[i]->numval - 1)*sizeof(__pmValue_PDU);
	else
	    need += sizeof(vlist_t) - sizeof(__pmValue_PDU);
    }
    if ((pp = (control_req_t *)__pmFindPDUBuf(need)) == NULL)
	return -oserror();
    pp->c_hdr.len = need;
    pp->c_hdr.type = PDU_LOG_CONTROL;
    pp->c_hdr.from = FROM_ANON;		/* context does not matter here */
    pp->c_control = htonl(control);
    pp->c_state = htonl(state);
    pp->c_delta = htonl(delta);
    pp->c_numpmid = htonl(request->numpmid);
    vp = (vlist_t *)pp->c_data;
    for (i = 0; i < request->numpmid; i++) {
	vsp = request->vset[i];
	vp->v_pmid = __htonpmID(vsp->pmid);
	vp->v_numval = htonl(vsp->numval);
	/*
	 * Note: spec says only PM_VAL_INSITU can be used ... we don't
	 * check vsp->valfmt -- this is OK, because the "value" is never
	 * looked at!
	 */
	for (j = 0; j < vsp->numval; j++) {
	    vp->v_list[j].inst = htonl(vsp->vlist[j].inst);
	    vp->v_list[j].value.lval = htonl(vsp->vlist[j].value.lval);
	}
	if (vsp->numval > 0)
	    vp = (vlist_t *)((__psint_t)vp + sizeof(*vp) + (vsp->numval-1)*sizeof(__pmValue_PDU));
	else
	    vp = (vlist_t *)((__psint_t)vp + sizeof(*vp) - sizeof(__pmValue_PDU));
    }
    
    sts = __pmXmitPDU(fd, (__pmPDU *)pp);
    __pmUnpinPDUBuf(pp);
    return sts;
}

int
__pmDecodeLogControl(const __pmPDU *pdubuf, pmResult **request, int *control, int *state, int *delta)
{
    int			sts;
    int			i;
    int			j;
    const control_req_t	*pp;
    int			numpmid;
    size_t		need;
    pmResult		*req;
    pmValueSet		*vsp;
    vlist_t		*vp;

    pp = (const control_req_t *)pdubuf;
    *control = ntohl(pp->c_control);
    *state = ntohl(pp->c_state);
    *delta = ntohl(pp->c_delta);
    numpmid = ntohl(pp->c_numpmid);
    vp = (vlist_t *)pp->c_data;

    need = sizeof(pmResult) + (numpmid - 1) * sizeof(pmValueSet *);
    if ((req = (pmResult *)malloc(need)) == NULL) {
	__pmNoMem("__pmDecodeLogControl.req", need, PM_RECOV_ERR);
	return -oserror();
    }
    req->numpmid = numpmid;

    for (i = 0; i < numpmid; i++) {
	int nv = (int)ntohl(vp->v_numval);
	if (nv > 0)
	    need = sizeof(pmValueSet) + (nv - 1)*sizeof(pmValue);
	else
	    need = sizeof(pmValueSet) - sizeof(pmValue);
	if ((vsp = (pmValueSet *)malloc(need)) == NULL) {
	    __pmNoMem("__pmDecodeLogControl.vsp", need, PM_RECOV_ERR);
	    sts = -oserror();
	    i--;
	    while (i)
		free(req->vset[i--]);
	    free(req);
	    return sts;
	}
	req->vset[i] = vsp;
	vsp->pmid = __ntohpmID(vp->v_pmid);
	vsp->numval = nv;
	vsp->valfmt  = PM_VAL_INSITU;
	for (j = 0; j < vsp->numval; j++) {
	    vsp->vlist[j].inst = ntohl(vp->v_list[j].inst);
	    vsp->vlist[j].value.lval = ntohl(vp->v_list[j].value.lval);
	}
	if (nv > 0)
	    vp = (vlist_t *)((__psint_t)vp + sizeof(*vp) + (nv - 1)*sizeof(__pmValue_PDU));
	else
	    vp = (vlist_t *)((__psint_t)vp + sizeof(*vp) - sizeof(__pmValue_PDU));
    }

    *request = req;
    return 0;
}
