/*
 * Metric metadata support for pmlogrewrite
 *
 * Copyright (c) 2011 Ken McDonell.  All Rights Reserved.
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
#include "logger.h"
#include <assert.h>

/*
 * Find or create a new metricspec_t
 */
metricspec_t *
start_metric(pmID pmid)
{
    metricspec_t	*mp;
    int			sts;

#if PCP_DEBUG
    if ((pmDebug & (DBG_TRACE_APPL0 | DBG_TRACE_APPL1)) == (DBG_TRACE_APPL0 | DBG_TRACE_APPL1))
	fprintf(stderr, "start_metric(%s)", pmIDStr(pmid));
#endif

    for (mp = metric_root; mp != NULL; mp = mp->m_next) {
	if (pmid == mp->old_desc.pmid) {
#if PCP_DEBUG
	    if ((pmDebug & (DBG_TRACE_APPL0 | DBG_TRACE_APPL1)) == (DBG_TRACE_APPL0 | DBG_TRACE_APPL1))
		fprintf(stderr, " -> %s\n", mp->old_name);
#endif
	    break;
	}
    }
    if (mp == NULL) {
	char	*name;
	pmDesc	desc;

	sts = pmNameID(pmid, &name);
	if (sts < 0) {
	    if (wflag) {
		snprintf(mess, sizeof(mess), "Metric %s pmNameID: %s", pmIDStr(pmid), pmErrStr(sts));
		yywarn(mess);
	    }
	    return NULL;
	}
	sts = pmLookupDesc(pmid, &desc);
	if (sts < 0) {
	    if (wflag) {
		snprintf(mess, sizeof(mess), "Metric %s: pmLookupDesc: %s", pmIDStr(pmid), pmErrStr(sts));
		yywarn(mess);
	    }
	    return NULL;
	}

	mp = (metricspec_t *)malloc(sizeof(metricspec_t));
	if (mp == NULL) {
	    fprintf(stderr, "metricspec malloc(%d) failed: %s\n", (int)sizeof(metricspec_t), strerror(errno));
	    abandon();
	    /*NOTREACHED*/
	}
	mp->m_next = metric_root;
	metric_root = mp;
	mp->output = OUTPUT_ALL;
	mp->one_inst = 0;
	mp->one_name = NULL;
	mp->old_name = name;
	mp->old_desc = desc;
	mp->new_desc = mp->old_desc;
	mp->flags = 0;
	mp->ip = NULL;
#if PCP_DEBUG
	if ((pmDebug & (DBG_TRACE_APPL0 | DBG_TRACE_APPL1)) == (DBG_TRACE_APPL0 | DBG_TRACE_APPL1))
	    fprintf(stderr, " -> %s [new entry]\n", mp->old_name);
#endif
    }

    return mp;
}

typedef struct {
    __pmLogHdr	hdr;
    pmDesc	desc;
    int		numnames;
    char	strbuf[1];
} desc_t;

/*
 * reverse the logic of __pmLogPutDesc()
 */
static void
_pmUnpackDesc(__pmPDU *pdubuf, pmDesc *desc, int *numnames, char ***names)
{
    desc_t	*pp;
    int		i;
    char	*p;
    int		slen;

    pp = (desc_t *)pdubuf;
    desc->type = ntohl(pp->desc.type);
    desc->sem = ntohl(pp->desc.sem);
    desc->indom = __ntohpmInDom(pp->desc.indom);
    desc->units = __ntohpmUnits(pp->desc.units);
    desc->pmid = __ntohpmID(pp->desc.pmid);
    *numnames = ntohl(pp->numnames);
    *names = (char **)malloc(*numnames * sizeof(*names[1]));
    if (*names == NULL) {
	fprintf(stderr, "_pmUnpackDesc malloc(%d) failed: %s\n", (int)(*numnames * sizeof(*names[1])), strerror(errno));
	abandon();
	/*NOTREACHED*/
    }

    p = pp->strbuf;
    for (i = 0; i < *numnames; i++) {
	memcpy(&slen, p, LENSIZE);
	slen = ntohl(slen);
	p += LENSIZE;
	(*names)[i] = malloc(slen+1);
	if ((*names)[i] == NULL) {
	    fprintf(stderr, "_pmUnpackDesc malloc(%d) failed: %s\n", slen+1, strerror(errno));
	    abandon();
	    /*NOTREACHED*/
	}
	strncpy((*names)[i], p, slen);
	(*names)[i][slen] = '\0';
	p += slen;
    }

    return;
}

/*
 * rewrite pmDesc from metadata, returns
 * -1	delete this pmDesc
 *  0	no change
 *  1	changed
 */
void
do_desc(void)
{
    metricspec_t	*mp;
    pmDesc		desc;
    int			i;
    int			sts;
    int			numnames;
    char		**names;
    long		out_offset;

    out_offset = ftell(outarch.logctl.l_mdfp);
    _pmUnpackDesc(inarch.metarec, &desc, &numnames, &names);

    for (mp = metric_root; mp != NULL; mp = mp->m_next) {
	if (desc.pmid != mp->old_desc.pmid || mp->flags == 0)
	    continue;
	if (mp->flags & METRIC_DELETE) {
#if PCP_DEBUG
	    if (pmDebug & DBG_TRACE_APPL1)
		fprintf(stderr, "Delete: pmDesc for %s\n", pmIDStr(desc.pmid));
#endif
	    goto done;
	}
#if PCP_DEBUG
	if (pmDebug & DBG_TRACE_APPL1)
	    fprintf(stderr, "Rewrite: pmDesc for %s\n", pmIDStr(desc.pmid));
#endif
	if (mp->flags & METRIC_CHANGE_PMID)
	    desc.pmid = mp->new_desc.pmid;
	if (mp->flags & METRIC_CHANGE_NAME) {
	    for (i = 0; i < numnames; i++) {
		if (strcmp(names[i], mp->old_name) == 0) {
		    free(names[i]);
		    names[i] = strdup(mp->new_name);
		    if (names[i] == NULL) {
			fprintf(stderr, "do_desc strdup(%s) failed: %s\n", mp->new_name, strerror(errno));
			abandon();
			/*NOTREACHED*/
		    }
		    break;
		}
	    }
	    if (i == numnames) {
		fprintf(stderr, "%s: Botch: old name %s not found in list of %d names for pmid %s ...",
			pmProgname, mp->old_name, numnames, pmIDStr(mp->old_desc.pmid));
		for (i = 0; i < numnames; i++) {
		    if (i > 0) fputc(',', stderr);
		    fprintf(stderr, " %s", names[i]);
		}
		fputc('\n', stderr);
		abandon();
		/*NOTREACHED*/
	    }
	}
	if (mp->flags & METRIC_CHANGE_TYPE)
	    desc.type = mp->new_desc.type;
	if (mp->flags & METRIC_CHANGE_INDOM)
	    desc.indom = mp->new_desc.indom;
	if (mp->flags & METRIC_CHANGE_SEM)
	    desc.sem = mp->new_desc.sem;
	if (mp->flags & METRIC_CHANGE_UNITS)
	    desc.units = mp->new_desc.units;	/* struct assignment */
	break;
    }
    if ((sts = __pmLogPutDesc(&outarch.logctl, &desc, numnames, names)) < 0) {
	fprintf(stderr, "%s: Error: __pmLogPutDesc: %s (%s): %s\n",
		pmProgname, names[0], pmIDStr(desc.pmid), pmErrStr(sts));
	abandon();
	/*NOTREACHED*/
    }
#if PCP_DEBUG
    if (pmDebug & DBG_TRACE_APPL0)
	fprintf(stderr, "Metadata: write PMID %s @ offset=%ld\n", pmIDStr(desc.pmid), out_offset);
#endif

done:
    for (i = 0; i < numnames; i++)
	free(names[i]);
    free(names);
    return;
}
