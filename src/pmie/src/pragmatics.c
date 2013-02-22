/*
 * pragmatics.c - inference engine pragmatics analysis
 * 
 * Copyright (c) 1995-2003 Silicon Graphics, Inc.  All Rights Reserved.
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
 * 
 * The analysis of how to organize the fetching of metrics (pragmatics),
 * and any other parts of the inference engine sensitive to details of
 * the PMAPI access are kept in this source file.
 */

#include <math.h>
#include <ctype.h>
#include "pmapi.h"
#include "impl.h"
#include "dstruct.h"
#include "eval.h"
#include "pragmatics.h"
#if defined(HAVE_IEEEFP_H)
#include <ieeefp.h>
#endif

extern char	*clientid;

int	dfltConn;	/* default context type */

/* for initialization of pmUnits struct */
pmUnits noUnits    = {0, 0, 0, 0, 0, 0};
pmUnits countUnits = {0, 0, 1, 0, 0, 0};

char *
findsource(char *host)
{
    static char	buf[MAXPATHLEN+MAXHOSTNAMELEN+30];

    if (archives) {
	Archive	*a = archives;
	while (a) {	/* find archive for host */
	    if (strcmp(host, a->hname) == 0)
		break;
	    a = a->next;
	}
	if (a)
	    snprintf(buf, sizeof(buf), "archive %s (host %s)", a->fname, host);
	else
	    snprintf(buf, sizeof(buf), "host %s in unknown archive!", host);
    }
    else
	snprintf(buf, sizeof(buf), "host %s", host);

    return buf;
}

/***********************************************************************
 * PMAPI context creation & destruction
 ***********************************************************************/

int	/* > 0: context handle,  -1: retry later */
newContext(char *host)
{
    Archive	*a;
    int		sts = -1;

    if (archives) {
	a = archives;
	while (a) {	/* find archive for host */
	    if (strcmp(host, a->hname) == 0)
		break;
	    a = a->next;
	}
	if (a) {	/* archive found */
	    if ((sts = pmNewContext(PM_CONTEXT_ARCHIVE, a->fname)) < 0) {
		fprintf(stderr, "%s: cannot open archive %s\n",
			pmProgname, a->fname);
		fprintf(stderr, "pmNewContext: %s\n", pmErrStr(sts));
		exit(1);
	    }
	}
	else {		/* no archive for host */
	    fprintf(stderr, "%s: no archive for host %s\n", pmProgname, host);
	    exit(1);
	}
    }
    else if ((sts = pmNewContext(PM_CONTEXT_HOST, host)) < 0) {
	if (host_state_changed(host, STATE_FAILINIT) == 1) {
	    if (sts == -ECONNREFUSED)
		fprintf(stderr, "%s: warning - pmcd "
			"on host %s does not respond\n",
			pmProgname, host);
	    else if (sts == PM_ERR_PERMISSION)
		fprintf(stderr, "%s: warning - host %s does not "
			"permit delivery of metrics to the local host\n",
			pmProgname, host);
	    else if (sts == PM_ERR_CONNLIMIT)
		fprintf(stderr, "%s: warning - pmcd "
			"on host %s has exceeded its connection limit\n",
			pmProgname, host);
	    else
		fprintf(stderr, "%s: warning - host %s is unreachable\n", 
			pmProgname, host);
	}
	sts = -1;
    }
    else if (clientid != NULL)
	/* register client id with pmcd */
	__pmSetClientId(clientid);
    return sts;
}


/***********************************************************************
 * instance profile
 ***********************************************************************/

/* equality (not symmetric) of instance names */
static int
eqinst(char *i1, char *i2)
{
    int		n1 = (int) strlen(i1);
    int		n2 = (int) strlen(i2);

    /* test equality of first word */
    if ((strncmp(i1, i2, n1) == 0) && ((n1 == n2) || isspace((int)i2[n1])))
	return 1;

    do {	/* skip over first word */
	i2++;
	n2--;
	if (n2 < n1)
	    return 0;
    } while (! isspace((int)*i2));

    do {	/* skip over spaces */
	i2++;
	n2--;
	if (n2 < n1)
	    return 0;
    } while (isspace((int)*i2));

    /* test equality of second word */
    if ((strncmp(i1, i2, n1) == 0) && ((n1 == n2) || isspace((int)i2[n1])))
	return 1;
    return 0;
}


/***********************************************************************
 * task queue
 ***********************************************************************/

/* find Task for new rule */
static Task *
findTask(RealTime delta)
{
    Task	*t, *u;
    int		n = 0;

    t = taskq;
    if (t) {
	while (t->next) {	/* find last task in queue */
	    t = t->next;
	    n++;
	}

	/* last task in queue has same delta */
	if (t->delta == delta)
	    return t;
    }

    u = newTask(delta, n);	/* create new Task */
    if (t) {
	t->next = u;
	u->prev = t;
    }
    else
	taskq = u;
    return u;
}


/***********************************************************************
 * wait list
 ***********************************************************************/

/* put Metric onto wait list */
void
waitMetric(Metric *m)
{
    Host *h = m->host;

    m->next = h->waits;
    m->prev = NULL;
    if (h->waits) h->waits->prev = m;
    h->waits = m;
}

/* remove Metric from wait list */
void
unwaitMetric(Metric *m)
{
    if (m->prev) m->prev->next = m->next;
    else m->host->waits = m->next;
    if (m->next) m->next->prev = m->prev;
}


/***********************************************************************
 * fetch list
 ***********************************************************************/

/* find Host for Metric */
static Host *
findHost(Task *t, Metric *m)
{
    Host	*h;

    h = t->hosts;
    while (h) {			/* look for existing host */
	if (h->name == m->hname)
	    return h;
	h = h->next;
    }

    h = newHost(t, m->hname);	/* add new host */
    if (t->hosts) {
	h->next = t->hosts;
	t->hosts->prev = h;
    }
    t->hosts = h;
    return h;
}

/* helper function for Extended Time Base */
static void
getDoubleAsXTB(double *realtime, int *ival, int *mode)
{
#define SECS_IN_24_DAYS 2073600.0

    if (*realtime > SECS_IN_24_DAYS) {
        *ival = (int)*realtime;
	*mode = (*mode & 0x0000ffff) | PM_XTB_SET(PM_TIME_SEC);
    }
    else {
	*ival = (int)(*realtime * 1000.0);
	*mode = (*mode & 0x0000ffff) | PM_XTB_SET(PM_TIME_MSEC);
    }
}


/* find Fetch bundle for Metric */
static Fetch *
findFetch(Host *h, Metric *m)
{
    Fetch	    *f;
    int		    sts;
    int		    i;
    int		    n;
    pmID	    pmid = m->desc.pmid;
    pmID	    *p;
    struct timeval  tv;

    /* find existing Fetch bundle */
    f = h->fetches;

    /* create new Fetch bundle */
    if (! f) {
	f = newFetch(h);
	if ((f->handle = newContext(symName(h->name))) < 0) {
	    free(f);
	    h->down = 1;
	    return NULL;
	}
	if (archives) {
	    int tmp_ival;
	    int tmp_mode = PM_MODE_INTERP;
	    getDoubleAsXTB(&h->task->delta, &tmp_ival, &tmp_mode);

	    tv.tv_sec = (time_t)start;
	    tv.tv_usec = (int)((start - tv.tv_sec) * 1000000.0);
	    if ((sts = pmSetMode(tmp_mode, &tv, tmp_ival)) < 0) {
		fprintf(stderr, "%s: pmSetMode failed: %s\n", pmProgname,
			pmErrStr(sts));
		exit(1);
	    }
#if PCP_DEBUG
	    if (pmDebug & DBG_TRACE_APPL1) {
		fprintf(stderr, "findFetch: fetch=0x%p host=0x%p delta=%.6f handle=%d\n", f, h, h->task->delta, f->handle);
	    }
#endif
	}
	f->next = NULL;
	f->prev = NULL;
	h->fetches = f;
    }

    /* look for existing pmid */
    p = f->pmids;
    n = f->npmids;
    for (i = 0; i < n; i++) {
	if (*p == pmid) break;
	p++;
    }

    /* add new pmid */
    if (i == n) {
	p = f->pmids;
	p = ralloc(p, (n+1) * sizeof(pmID));
	p[n] = pmid;
	f->npmids = n + 1;
	f->pmids = p;
    }

    return f;
}


/* find Profile for Metric */
static Profile *
findProfile(Fetch *f, Metric *m)
{
    Profile	*p;
    int		sts;

    /* find existing Profile */
    p = f->profiles;
    while (p) {
	if (p->indom == m->desc.indom) {
	    m->next = p->metrics;
	    if (p->metrics) p->metrics->prev = m;
	    p->metrics = m;
	    break;
	}
	p = p->next;
    }

    /* create new Profile */
    if (p == NULL) {
	m->next = NULL;
	p = newProfile(f, m->desc.indom);
	p->next = f->profiles;
	if (f->profiles) f->profiles->prev = p;
	f->profiles = p;
	p->metrics = m;
    }

    /* add instances required by Metric to Profile */
    if ((sts = pmUseContext(f->handle)) < 0) {
	fprintf(stderr, "%s: pmUseContext failed: %s\n", pmProgname,
		pmErrStr(sts));
	exit(1);
    }

    /*
     * If any rule requires all instances, then ignore restricted
     * instance lists from all other rules
     */
    if (m->specinst == 0 && p->need_all == 0) {
	sts = pmDelProfile(p->indom, 0, (int *)0);
	if (sts < 0) {
	    fprintf(stderr, "%s: pmDelProfile failed: %s\n", pmProgname,
		    pmErrStr(sts));
	    exit(1);
	}
	sts = pmAddProfile(p->indom, 0, (int *)0);
	p->need_all = 1;
    }
    else if (m->specinst > 0 && p->need_all == 0)
	sts = pmAddProfile(p->indom, m->m_idom, m->iids);
    else
	sts = 0;

    if (sts < 0) {
	fprintf(stderr, "%s: pmAddProfile failed: %s\n", pmProgname,
		pmErrStr(sts));
	exit(1);
    }

    m->profile = p;
    return p;
}


/* organize fetch bundling for given expression */
static void
bundle(Task *t, Expr *x)
{
    Metric	*m;
    Host	*h;
    int		i;

    if (x->op == CND_FETCH) {
	m = x->metrics;
	for (i = 0; i < x->hdom; i++) {
	    h = findHost(t, m);
	    m->host = h;
	    if (m->conv)	/* initialized Metric */
		bundleMetric(h, m);
	    else		/* uninitialized Metric */
		waitMetric(m);
	    m++;
	}
#if PCP_DEBUG
	if (pmDebug & DBG_TRACE_APPL1) {
 	    fprintf(stderr, "bundle: task " PRINTF_P_PFX "%p nth=%d prev=" PRINTF_P_PFX "%p next=" PRINTF_P_PFX "%p delta=%.3f nrules=%d\n",
 		t, t->nth, t->prev, t->next, t->delta, t->nrules+1);
	    __dumpExpr(1, x);
	    m = x->metrics;
	    for (i = 0; i < x->hdom; i++) {
		__dumpMetric(2, m);
		m++;
	    }
	}
#endif
    }
    else {
	if (x->arg1) {
	    bundle(t, x->arg1);
	    if (x->arg2)
		bundle(t, x->arg2);
	}
    }
}


/***********************************************************************
 * secret agent mode support
 ***********************************************************************/

/* send pmDescriptor for the given Expr as a binary PDU */
static void
sendDesc(Expr *x, pmValueSet *vset)
{
    pmDesc	d;

    d.pmid = vset->pmid;
    d.indom = PM_INDOM_NULL;
    if (x->sem != SEM_UNKNOWN) {
	d.type = PM_TYPE_DOUBLE;
	d.sem = x->sem;
	d.units = x->units;
    }
    else {
	d.type = PM_TYPE_NOSUPPORT;
	d.sem = PM_SEM_INSTANT;
	d.units = noUnits;
    }
    __pmSendDesc(STDOUT_FILENO, pmWhichContext(), &d);
}


/***********************************************************************
 * exported functions
 ***********************************************************************/

/* initialize access to archive */
int
initArchive(Archive *a)
{
    pmLogLabel	    label;
    struct timeval  tv;
    int		    sts;
    int		    handle;
    int		    n;
    Archive	    *b;

    /* setup temorary context for the archive */
    if ((sts = pmNewContext(PM_CONTEXT_ARCHIVE, a->fname)) < 0) {
	fprintf(stderr, "%s: cannot open archive %s\n"
		"pmNewContext failed: %s\n",
		pmProgname, a->fname, pmErrStr(sts));
	return 0;
    }
    handle = sts;

    /* get the goodies from archive label */
    if ((sts = pmGetArchiveLabel(&label)) < 0) {
	fprintf(stderr, "%s: cannot read label from archive %s\n"
			"pmGetArchiveLabel failed: %s\n", 
			pmProgname, a->fname, pmErrStr(sts));
	pmDestroyContext(handle);
	return 0;
    }
    n = (int) strlen(label.ll_hostname);
    a->hname = (char *) alloc(n + 1);
    strcpy(a->hname, label.ll_hostname);
    a->first = realize(label.ll_start);
    if ((sts = pmGetArchiveEnd(&tv)) < 0) {
	fprintf(stderr, "%s: archive %s is corrupted\n"
		"pmGetArchiveEnd failed: %s\n",
		pmProgname, a->fname, pmErrStr(sts));
	pmDestroyContext(handle);
	return 0;
    }
    a->last = realize(tv);

    /* check for duplicate host */
    b = archives;
    while (b) {
	if (strcmp(a->hname, b->hname) == 0) {
	    fprintf(stderr, "%s: Error: archive %s not legal - archive %s is already open "
		    "for host %s\n", pmProgname, a->fname, b->fname, b->hname);
	    pmDestroyContext(handle);
	    return 0;
	}
	b = b->next;
    }

    /* put archive record on the archives list */
    a->next = archives;
    archives = a;

    /* update first and last available data points */
    if (first == -1 || a->first < first)
	first = a->first;
    if (a->last > last)
	last = a->last;

    pmDestroyContext(handle);
    return 1;
}


/* initialize timezone */
void
zoneInit(void)
{
    int		sts;
    int		handle = -1;
    Archive	*a;

    if (timeZone) {			/* TZ from timezone string */
	if ((sts = pmNewZone(timeZone)) < 0)
	    fprintf(stderr, "%s: cannot set timezone to %s\n"
		    "pmNewZone failed: %s\n", pmProgname, timeZone,
		    pmErrStr(sts));
    }
    else if (! archives && hostZone) {	/* TZ from live host */
	if ((handle = pmNewContext(PM_CONTEXT_HOST, dfltHost)) < 0)
	    fprintf(stderr, "%s: cannot set timezone from %s\n"
		    "pmNewContext failed: %s\n", pmProgname,
		    findsource(dfltHost), pmErrStr(handle));
	else if ((sts = pmNewContextZone()) < 0)
	    fprintf(stderr, "%s: cannot set timezone from %s\n"
		    "pmNewContextZone failed: %s\n", pmProgname,
		    findsource(dfltHost), pmErrStr(sts));
	else
	    fprintf(stdout, "%s: timezone set to local timezone of host %s\n",
		    pmProgname, dfltHost);
	if (handle >= 0)
	    pmDestroyContext(handle);
    }
    else if (hostZone) {		/* TZ from an archive */
	a = archives;
	while (a) {
	    if (strcmp(dfltHost, a->hname) == 0)
		break;
	    a = a->next;
	}
	if (! a)
	    fprintf(stderr, "%s: no archive supplied for host %s\n",
		    pmProgname, dfltHost);
	else if ((handle = pmNewContext(PM_CONTEXT_ARCHIVE, a->fname)) < 0)
	    fprintf(stderr, "%s: cannot set timezone from %s\npmNewContext failed: %s\n",
		    pmProgname, findsource(dfltHost), pmErrStr(handle));
	else if ((sts = pmNewContextZone()) < 0)
	    fprintf(stderr, "%s: cannot set timezone from %s\n"
		    "pmNewContextZone failed: %s\n",
		    pmProgname, findsource(dfltHost), pmErrStr(sts));
	else
	    fprintf(stdout, "%s: timezone set to local timezone of host %s\n",
		    pmProgname, dfltHost);
	if (handle >= 0)
	    pmDestroyContext(handle);
    }
}


/* convert to canonical units */
pmUnits
canon(pmUnits in)
{
    static pmUnits	out;

    out = in;
    out.scaleSpace = PM_SPACE_BYTE;
    out.scaleTime = PM_TIME_SEC;
    out.scaleCount = 0;
    return out;
}

#ifndef HAVE_POW
/*
 * We have not found a platform yet that needs this, but this implementation
 * comes from http://www.netlib.org/fdlibm/e_pow.c
 *
 * ====================================================
 * Copyright (C) 2003 by Sun Microsystems, Inc. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice 
 * is preserved.
 * ====================================================
 */

/* Sometimes it's necessary to define __LITTLE_ENDIAN explicitly
 *    but these catch some common cases. */

#if defined(i386) || defined(i486) || \
	defined(intel) || defined(x86) || defined(i86pc) || \
	defined(__alpha) || defined(__osf__)
#define __LITTLE_ENDIAN
#endif

#ifdef __LITTLE_ENDIAN
#define __HI(x) *(1+(int*)&x)
#define __LO(x) *(int*)&x
#define __HIp(x) *(1+(int*)x)
#define __LOp(x) *(int*)x
#else
#define __HI(x) *(int*)&x
#define __LO(x) *(1+(int*)&x)
#define __HIp(x) *(int*)x
#define __LOp(x) *(1+(int*)x)
#endif


/* _pow(x,y) return x**y
 *
 *		      n
 * Method:  Let x =  2   * (1+f)
 *	1. Compute and return log2(x) in two pieces:
 *		log2(x) = w1 + w2,
 *	   where w1 has 53-24 = 29 bit trailing zeros.
 *	2. Perform y*log2(x) = n+y' by simulating muti-precision 
 *	   arithmetic, where |y'|<=0.5.
 *	3. Return x**y = 2**n*exp(y'*log2)
 *
 * Special cases:
 *	1.  (anything) ** 0  is 1
 *	2.  (anything) ** 1  is itself
 *	3.  (anything) ** NAN is NAN
 *	4.  NAN ** (anything except 0) is NAN
 *	5.  +-(|x| > 1) **  +INF is +INF
 *	6.  +-(|x| > 1) **  -INF is +0
 *	7.  +-(|x| < 1) **  +INF is +0
 *	8.  +-(|x| < 1) **  -INF is +INF
 *	9.  +-1         ** +-INF is NAN
 *	10. +0 ** (+anything except 0, NAN)               is +0
 *	11. -0 ** (+anything except 0, NAN, odd integer)  is +0
 *	12. +0 ** (-anything except 0, NAN)               is +INF
 *	13. -0 ** (-anything except 0, NAN, odd integer)  is +INF
 *	14. -0 ** (odd integer) = -( +0 ** (odd integer) )
 *	15. +INF ** (+anything except 0,NAN) is +INF
 *	16. +INF ** (-anything except 0,NAN) is +0
 *	17. -INF ** (anything)  = -0 ** (-anything)
 *	18. (-anything) ** (integer) is (-1)**(integer)*(+anything**integer)
 *	19. (-anything except 0 and inf) ** (non-integer) is NAN
 *
 * Accuracy:
 *	pow(x,y) returns x**y nearly rounded. In particular
 *			pow(integer,integer)
 *	always returns the correct integer provided it is 
 *	representable.
 *
 * Constants :
 * The hexadecimal values are the intended ones for the following 
 * constants. The decimal values may be used, provided that the 
 * compiler will convert from decimal to binary accurately enough 
 * to produce the hexadecimal values shown.
 */

//#include "fdlibm.h"

static const double 
bp[] = {1.0, 1.5,},
dp_h[] = { 0.0, 5.84962487220764160156e-01,}, /* 0x3FE2B803, 0x40000000 */
dp_l[] = { 0.0, 1.35003920212974897128e-08,}, /* 0x3E4CFDEB, 0x43CFD006 */
zero    =  0.0,
one	=  1.0,
two	=  2.0,
two53	=  9007199254740992.0,	/* 0x43400000, 0x00000000 */
huge	=  1.0e300,
tiny    =  1.0e-300,
	/* poly coefs for (3/2)*(log(x)-2s-2/3*s**3 */
L1  =  5.99999999999994648725e-01, /* 0x3FE33333, 0x33333303 */
L2  =  4.28571428578550184252e-01, /* 0x3FDB6DB6, 0xDB6FABFF */
L3  =  3.33333329818377432918e-01, /* 0x3FD55555, 0x518F264D */
L4  =  2.72728123808534006489e-01, /* 0x3FD17460, 0xA91D4101 */
L5  =  2.30660745775561754067e-01, /* 0x3FCD864A, 0x93C9DB65 */
L6  =  2.06975017800338417784e-01, /* 0x3FCA7E28, 0x4A454EEF */
P1   =  1.66666666666666019037e-01, /* 0x3FC55555, 0x5555553E */
P2   = -2.77777777770155933842e-03, /* 0xBF66C16C, 0x16BEBD93 */
P3   =  6.61375632143793436117e-05, /* 0x3F11566A, 0xAF25DE2C */
P4   = -1.65339022054652515390e-06, /* 0xBEBBBD41, 0xC5D26BF1 */
P5   =  4.13813679705723846039e-08, /* 0x3E663769, 0x72BEA4D0 */
lg2  =  6.93147180559945286227e-01, /* 0x3FE62E42, 0xFEFA39EF */
lg2_h  =  6.93147182464599609375e-01, /* 0x3FE62E43, 0x00000000 */
lg2_l  = -1.90465429995776804525e-09, /* 0xBE205C61, 0x0CA86C39 */
ovt =  8.0085662595372944372e-0017, /* -(1024-log2(ovfl+.5ulp)) */
cp    =  9.61796693925975554329e-01, /* 0x3FEEC709, 0xDC3A03FD =2/(3ln2) */
cp_h  =  9.61796700954437255859e-01, /* 0x3FEEC709, 0xE0000000 =(float)cp */
cp_l  = -7.02846165095275826516e-09, /* 0xBE3E2FE0, 0x145B01F5 =tail of cp_h*/
ivln2    =  1.44269504088896338700e+00, /* 0x3FF71547, 0x652B82FE =1/ln2 */
ivln2_h  =  1.44269502162933349609e+00, /* 0x3FF71547, 0x60000000 =24b 1/ln2*/
ivln2_l  =  1.92596299112661746887e-08; /* 0x3E54AE0B, 0xF85DDF44 =1/ln2 tail*/

double
pow(double x, double y)
{
	double z,ax,z_h,z_l,p_h,p_l;
	double y1,t1,t2,r,s,t,u,v,w;
	int i,j,k,yisint,n;
	int hx,hy,ix,iy;
	unsigned lx,ly;

	hx = __HI(x); lx = __LO(x);
	hy = __HI(y); ly = __LO(y);
	ix = hx&0x7fffffff;  iy = hy&0x7fffffff;

    /* y==zero: x**0 = 1 */
	if((iy|ly)==0) return one; 	

    /* +-NaN return x+y */
	if(ix > 0x7ff00000 || ((ix==0x7ff00000)&&(lx!=0)) ||
	   iy > 0x7ff00000 || ((iy==0x7ff00000)&&(ly!=0))) 
		return x+y;	

    /* determine if y is an odd int when x < 0
     * yisint = 0	... y is not an integer
     * yisint = 1	... y is an odd int
     * yisint = 2	... y is an even int
     */
	yisint  = 0;
	if(hx<0) {	
	    if(iy>=0x43400000) yisint = 2; /* even integer y */
	    else if(iy>=0x3ff00000) {
		k = (iy>>20)-0x3ff;	   /* exponent */
		if(k>20) {
		    j = ly>>(52-k);
		    if((j<<(52-k))==ly) yisint = 2-(j&1);
		} else if(ly==0) {
		    j = iy>>(20-k);
		    if((j<<(20-k))==iy) yisint = 2-(j&1);
		}
	    }		
	} 

    /* special value of y */
	if(ly==0) { 	
	    if (iy==0x7ff00000) {	/* y is +-inf */
	        if(((ix-0x3ff00000)|lx)==0)
		    return  y - y;	/* inf**+-1 is NaN */
	        else if (ix >= 0x3ff00000)/* (|x|>1)**+-inf = inf,0 */
		    return (hy>=0)? y: zero;
	        else			/* (|x|<1)**-,+inf = inf,0 */
		    return (hy<0)?-y: zero;
	    } 
	    if(iy==0x3ff00000) {	/* y is  +-1 */
		if(hy<0) return one/x; else return x;
	    }
	    if(hy==0x40000000) return x*x; /* y is  2 */
	    if(hy==0x3fe00000) {	/* y is  0.5 */
		if(hx>=0)	/* x >= +0 */
		return sqrt(x);	
	    }
	}

	ax   = fabs(x);
    /* special value of x */
	if(lx==0) {
	    if(ix==0x7ff00000||ix==0||ix==0x3ff00000){
		z = ax;			/*x is +-0,+-inf,+-1*/
		if(hy<0) z = one/z;	/* z = (1/|x|) */
		if(hx<0) {
		    if(((ix-0x3ff00000)|yisint)==0) {
			z = (z-z)/(z-z); /* (-1)**non-int is NaN */
		    } else if(yisint==1) 
			z = -z;		/* (x<0)**odd = -(|x|**odd) */
		}
		return z;
	    }
	}
    
	n = (hx>>31)+1;

    /* (x<0)**(non-int) is NaN */
	if((n|yisint)==0) return (x-x)/(x-x);

	s = one; /* s (sign of result -ve**odd) = -1 else = 1 */
	if((n|(yisint-1))==0) s = -one;/* (-ve)**(odd int) */

    /* |y| is huge */
	if(iy>0x41e00000) { /* if |y| > 2**31 */
	    if(iy>0x43f00000){	/* if |y| > 2**64, must o/uflow */
		if(ix<=0x3fefffff) return (hy<0)? huge*huge:tiny*tiny;
		if(ix>=0x3ff00000) return (hy>0)? huge*huge:tiny*tiny;
	    }
	/* over/underflow if x is not close to one */
	    if(ix<0x3fefffff) return (hy<0)? s*huge*huge:s*tiny*tiny;
	    if(ix>0x3ff00000) return (hy>0)? s*huge*huge:s*tiny*tiny;
	/* now |1-x| is tiny <= 2**-20, suffice to compute 
 	   log(x) by x-x^2/2+x^3/3-x^4/4 */
	    t = ax-one;		/* t has 20 trailing zeros */
	    w = (t*t)*(0.5-t*(0.3333333333333333333333-t*0.25));
	    u = ivln2_h*t;	/* ivln2_h has 21 sig. bits */
	    v = t*ivln2_l-w*ivln2;
	    t1 = u+v;
	    __LO(t1) = 0;
	    t2 = v-(t1-u);
	} else {
	    double ss,s2,s_h,s_l,t_h,t_l;
	    n = 0;
	/* take care subnormal number */
	    if(ix<0x00100000)
		{ax *= two53; n -= 53; ix = __HI(ax); }
	    n  += ((ix)>>20)-0x3ff;
	    j  = ix&0x000fffff;
	/* determine interval */
	    ix = j|0x3ff00000;		/* normalize ix */
	    if(j<=0x3988E) k=0;		/* |x|<sqrt(3/2) */
	    else if(j<0xBB67A) k=1;	/* |x|<sqrt(3)   */
	    else {k=0;n+=1;ix -= 0x00100000;}
	    __HI(ax) = ix;

	/* compute ss = s_h+s_l = (x-1)/(x+1) or (x-1.5)/(x+1.5) */
	    u = ax-bp[k];		/* bp[0]=1.0, bp[1]=1.5 */
	    v = one/(ax+bp[k]);
	    ss = u*v;
	    s_h = ss;
	    __LO(s_h) = 0;
	/* t_h=ax+bp[k] High */
	    t_h = zero;
	    __HI(t_h)=((ix>>1)|0x20000000)+0x00080000+(k<<18); 
	    t_l = ax - (t_h-bp[k]);
	    s_l = v*((u-s_h*t_h)-s_h*t_l);
	/* compute log(ax) */
	    s2 = ss*ss;
	    r = s2*s2*(L1+s2*(L2+s2*(L3+s2*(L4+s2*(L5+s2*L6)))));
	    r += s_l*(s_h+ss);
	    s2  = s_h*s_h;
	    t_h = 3.0+s2+r;
	    __LO(t_h) = 0;
	    t_l = r-((t_h-3.0)-s2);
	/* u+v = ss*(1+...) */
	    u = s_h*t_h;
	    v = s_l*t_h+t_l*ss;
	/* 2/(3log2)*(ss+...) */
	    p_h = u+v;
	    __LO(p_h) = 0;
	    p_l = v-(p_h-u);
	    z_h = cp_h*p_h;		/* cp_h+cp_l = 2/(3*log2) */
	    z_l = cp_l*p_h+p_l*cp+dp_l[k];
	/* log2(ax) = (ss+..)*2/(3*log2) = n + dp_h + z_h + z_l */
	    t = (double)n;
	    t1 = (((z_h+z_l)+dp_h[k])+t);
	    __LO(t1) = 0;
	    t2 = z_l-(((t1-t)-dp_h[k])-z_h);
	}

    /* split up y into y1+y2 and compute (y1+y2)*(t1+t2) */
	y1  = y;
	__LO(y1) = 0;
	p_l = (y-y1)*t1+y*t2;
	p_h = y1*t1;
	z = p_l+p_h;
	j = __HI(z);
	i = __LO(z);
	if (j>=0x40900000) {				/* z >= 1024 */
	    if(((j-0x40900000)|i)!=0)			/* if z > 1024 */
		return s*huge*huge;			/* overflow */
	    else {
		if(p_l+ovt>z-p_h) return s*huge*huge;	/* overflow */
	    }
	} else if((j&0x7fffffff)>=0x4090cc00 ) {	/* z <= -1075 */
	    if(((j-0xc090cc00)|i)!=0) 		/* z < -1075 */
		return s*tiny*tiny;		/* underflow */
	    else {
		if(p_l<=z-p_h) return s*tiny*tiny;	/* underflow */
	    }
	}
    /*
     * compute 2**(p_h+p_l)
     */
	i = j&0x7fffffff;
	k = (i>>20)-0x3ff;
	n = 0;
	if(i>0x3fe00000) {		/* if |z| > 0.5, set n = [z+0.5] */
	    n = j+(0x00100000>>(k+1));
	    k = ((n&0x7fffffff)>>20)-0x3ff;	/* new k for n */
	    t = zero;
	    __HI(t) = (n&~(0x000fffff>>k));
	    n = ((n&0x000fffff)|0x00100000)>>(20-k);
	    if(j<0) n = -n;
	    p_h -= t;
	} 
	t = p_l+p_h;
	__LO(t) = 0;
	u = t*lg2_h;
	v = (p_l-(t-p_h))*lg2+t*lg2_l;
	z = u+v;
	w = v-(z-u);
	t  = z*z;
	t1  = z - t*(P1+t*(P2+t*(P3+t*(P4+t*P5))));
	r  = (z*t1)/(t1-two)-(w+z*w);
	z  = one-(r-z);
	j  = __HI(z);
	j += (n<<20);
	if((j>>20)<=0) z = scalbn(z,n);	/* subnormal output */
	else __HI(z) += (n<<20);
	return s*z;
}
#endif


/* scale factor to canonical pmUnits */
double
scale(pmUnits in)
{
    double	f;

    /* scale space to Mbyte */
    f = pow(1024, in.dimSpace * (in.scaleSpace - PM_SPACE_BYTE));

    /* scale time to seconds  */
    if (in.scaleTime > PM_TIME_SEC)
	f *= pow(60, in.dimTime * (in.scaleTime - PM_TIME_SEC));
    else
	f *= pow(1000, in.dimTime * (in.scaleTime - PM_TIME_SEC));

    /* scale events to millions of events */
    f *= pow(10, in.dimCount * in.scaleCount);

    return f;
}


/* initialize Metric */
int      /* 1: ok, 0: try again later, -1: fail */
initMetric(Metric *m)
{
    char	*hname = symName(m->hname);
    char	*mname = symName(m->mname);
    char	**inames;
    int		*iids;
    int		handle;
    int		ret = 1;
    int		sts;
    int		i, j;

    /* set up temporary context */
    if ((handle = newContext(hname)) < 0)
	return 0;

    host_state_changed(hname, STATE_RECONN);

    if ((sts = pmLookupName(1, &mname, &m->desc.pmid)) < 0) {
	fprintf(stderr, "%s: metric %s not in namespace for %s\n"
		"pmLookupName failed: %s\n",
		pmProgname, mname, findsource(hname), pmErrStr(sts));
	ret = 0;
	goto end;
    }

    /* fill in performance metric descriptor */
    if ((sts = pmLookupDesc(m->desc.pmid, &m->desc)) < 0) {
	fprintf(stderr, "%s: metric %s not currently available from %s\n"
		"pmLookupDesc failed: %s\n",
		pmProgname, mname, findsource(hname), pmErrStr(sts));
	ret = 0;
	goto end;
    }

    if (m->desc.type == PM_TYPE_STRING ||
	m->desc.type == PM_TYPE_AGGREGATE ||
	m->desc.type == PM_TYPE_AGGREGATE_STATIC ||
	m->desc.type == PM_TYPE_EVENT ||
	m->desc.type == PM_TYPE_UNKNOWN) {
	fprintf(stderr, "%s: metric %s has non-numeric type\n", pmProgname, mname);
	ret = -1;
    }
    else if (m->desc.indom == PM_INDOM_NULL) {
	if (m->specinst != 0) {
	    fprintf(stderr, "%s: metric %s has no instances\n", pmProgname, mname);
	    ret = -1;
	}
	else
	    m->m_idom = 1;
    }
    else {
	/* metric has instances, get full instance profile */
	if (archives) {
	    if ((sts = pmGetInDomArchive(m->desc.indom, &iids, &inames)) < 0) {
		fprintf(stderr, "Metric %s from %s - instance domain not "
			"available in archive\npmGetInDomArchive failed: %s\n",
			mname, findsource(hname), pmErrStr(sts));
		ret = -1;
	    }
	}
	else if ((sts = pmGetInDom(m->desc.indom, &iids, &inames)) < 0) {
	    fprintf(stderr, "Instance domain for metric %s from %s not (currently) available\n"
		    "pmGetIndom failed: %s\n", mname, findsource(hname), pmErrStr(sts));
	    ret = 0;
	}

	if (ret == 1) {	/* got instance profile */
	    if (m->specinst == 0) {
		/* all instances */
		m->iids = iids;
		m->m_idom = sts;
		m->inames = alloc(m->m_idom*sizeof(char *));
		for (i = 0; i < m->m_idom; i++) {
		    m->inames[i] = sdup(inames[i]);
		}
	    }
	    else {
		/* selected instances only */
		m->m_idom = 0;
		for (i = 0; i < m->specinst; i++) {
		    /* look for first matching instance name */
		    for (j = 0; j < sts; j++) {
			if (eqinst(m->inames[i], inames[j])) {
			    m->iids[i] = iids[j];
			    m->m_idom++;
			    break;
			}
		    }
		    if (j == sts) {
			__pmNotifyErr(LOG_ERR, "metric %s from %s does not "
				"(currently) have instance \"%s\"\n",
				mname, findsource(hname), m->inames[i]);
			m->iids[i] = PM_IN_NULL;
			ret = 0;
		    }
		}
		if (sts > 0) {
		    /*
		     * pmGetInDom or pmGetInDomArchive returned some
		     * instances above
		     */
		    free(iids);
		}

		/* 
		 * if specinst != m_idom, then some not found ... move these
		 * to the end of the list
		 */
		for (j = m->specinst-1; j >= 0; j--) {
		    if (m->iids[j] != PM_IN_NULL)
			break;
		}
		for (i = 0; i < j; i++) {
		    if (m->iids[i] == PM_IN_NULL) {
			/* need to swap */
			char	*tp;
			tp = m->inames[i];
			m->inames[i] = m->inames[j];
			m->iids[i] = m->iids[j];
			m->inames[j] = tp;
			m->iids[j] = PM_IN_NULL;
			j--;
		    }
		}
	    }

#if PCP_DEBUG
	    if (pmDebug & DBG_TRACE_APPL1) {
		int	numinst;
		fprintf(stderr, "initMetric: %s from %s: instance domain specinst=%d\n",
			mname, hname, m->specinst);
		if (m->m_idom < 1) fprintf(stderr, "  %d instances!\n", m->m_idom);
		numinst =  m->specinst == 0 ? m->m_idom : m->specinst;
		for (i = 0; i < numinst; i++) {
		    fprintf(stderr, "  indom[%d]", i);
		    if (m->iids[i] == PM_IN_NULL) 
			fprintf(stderr, " ?missing");
		    else
			fprintf(stderr, " %d", m->iids[i]);
		    fprintf(stderr, " \"%s\"\n", m->inames[i]);
		}
	    }
#endif
	    if (sts > 0) {
		/*
		 * pmGetInDom or pmGetInDomArchive returned some instances
		 * above
		 */
		free(inames);
	    }
	}
    }

    if (ret == 1) {
	/* compute conversion factor into canonical units
	   - non-zero conversion factor flags initialized metric */
	m->conv = scale(m->desc.units);

	/* automatic rate computation */
	if (m->desc.sem == PM_SEM_COUNTER) {
	    m->vals = (double *) ralloc(m->vals, m->m_idom * sizeof(double));
	    for (j = 0; j < m->m_idom; j++)
		m->vals[j] = 0;
	}
    }

end:
    /* destroy temporary context */
    pmDestroyContext(handle);

    /* retry not meaningful for archives */
    if (archives && (ret == 0))
	ret = -1;

    return ret;
}


/* reinitialize Metric - only for live host */
int      /* 1: ok, 0: try again later, -1: fail */
reinitMetric(Metric *m)
{
    char	*hname = symName(m->hname);
    char	*mname = symName(m->mname);
    char	**inames;
    int		*iids;
    int		handle;
    int		ret = 1;
    int		sts;
    int		i, j;

    /* set up temporary context */
    if ((handle = newContext(hname)) < 0)
	return 0;

    host_state_changed(hname, STATE_RECONN);

    if ((sts = pmLookupName(1, &mname, &m->desc.pmid)) < 0) {
	ret = 0;
	goto end;
    }

    /* fill in performance metric descriptor */
    if ((sts = pmLookupDesc(m->desc.pmid, &m->desc)) < 0) {
	ret = 0;
        goto end;
    }

    if (m->desc.type == PM_TYPE_STRING ||
	m->desc.type == PM_TYPE_AGGREGATE ||
	m->desc.type == PM_TYPE_AGGREGATE_STATIC ||
	m->desc.type == PM_TYPE_EVENT ||
	m->desc.type == PM_TYPE_UNKNOWN) {
	fprintf(stderr, "%s: metric %s has non-numeric type\n", pmProgname, mname);
	ret = -1;
    }
    else if (m->desc.indom == PM_INDOM_NULL) {
	if (m->specinst != 0) {
	    fprintf(stderr, "%s: metric %s has no instances\n", pmProgname, mname);
	    ret = -1;
	}
	else
	    m->m_idom = 1;
    }
    else {
	if ((sts = pmGetInDom(m->desc.indom, &iids, &inames)) < 0) { /* full profile */
	    ret = 0;
	}
	else {
	    if (m->specinst == 0) {
		/* all instances */
		m->iids = iids;
		m->m_idom = sts;
		m->inames = alloc(m->m_idom*sizeof(char *));
		for (i = 0; i < m->m_idom; i++) {
		    m->inames[i] = sdup(inames[i]);
		}
	    }
	    else {
		/* explicit instance profile */
		m->m_idom = 0;
		for (i = 0; i < m->specinst; i++) {
		    /* look for first matching instance name */
		    for (j = 0; j < sts; j++) {
			if (eqinst(m->inames[i], inames[j])) {
			    m->iids[i] = iids[j];
			    m->m_idom++;
			    break;
			}
		    }
		    if (j == sts) {
			m->iids[i] = PM_IN_NULL;
			ret = 0;
		    }
		}
		if (sts > 0) {
		    /*
		     * pmGetInDom or pmGetInDomArchive returned some
		     * instances above
		     */
		    free(iids);
		}

		/* 
		 * if specinst != m_idom, then some not found ... move these
		 * to the end of the list
		 */
		for (j = m->specinst-1; j >= 0; j--) {
		    if (m->iids[j] != PM_IN_NULL)
			break;
		}
		for (i = 0; i < j; i++) {
		    if (m->iids[i] == PM_IN_NULL) {
			/* need to swap */
			char	*tp;
			tp = m->inames[i];
			m->inames[i] = m->inames[j];
			m->iids[i] = m->iids[j];
			m->inames[j] = tp;
			m->iids[j] = PM_IN_NULL;
			j--;
		    }
		}
	    }

#if PCP_DEBUG
	    if (pmDebug & DBG_TRACE_APPL1) {
		int	numinst;
		fprintf(stderr, "reinitMetric: %s from %s: instance domain specinst=%d\n",
			mname, hname, m->specinst);
		if (m->m_idom < 1) fprintf(stderr, "  %d instances!\n", m->m_idom);
		if (m->specinst == 0) numinst = m->m_idom;
		else numinst = m->specinst;
		for (i = 0; i < numinst; i++) {
		    fprintf(stderr, "  indom[%d]", i);
		    if (m->iids[i] == PM_IN_NULL) 
			fprintf(stderr, " ?missing");
		    else
			fprintf(stderr, " %d", m->iids[i]);
		    fprintf(stderr, " \"%s\"\n", m->inames[i]);
		}
	    }
#endif
	    if (sts > 0) {
		/*
		 * pmGetInDom or pmGetInDomArchive returned some instances
		 * above
		 */
		free(inames);
	    }
	}
    }

    if (ret == 1) {
	/* compute conversion factor into canonical units
	   - non-zero conversion factor flags initialized metric */
	m->conv = scale(m->desc.units);

	/* automatic rate computation */
	if (m->desc.sem == PM_SEM_COUNTER) {
	    m->vals = (double *) ralloc(m->vals, m->m_idom * sizeof(double));
	    for (j = 0; j < m->m_idom; j++)
		m->vals[j] = 0;
	}
    }

    if (ret >= 0) {
	/*
	 * re-shape, starting here are working up the expression until
	 * we reach the top of the tree or the designated metrics
	 * associated with the node are not the same
	 */
	Expr	*x = m->expr;
	while (x) {
	    /*
	     * only re-shape expressions that may have set values
	     */
	    if (x->op == CND_FETCH ||
		x->op == CND_NEG || x->op == CND_ADD || x->op == CND_SUB ||
		x->op == CND_MUL || x->op == CND_DIV ||
		x->op == CND_SUM_HOST || x->op == CND_SUM_INST ||
		x->op == CND_SUM_TIME ||
		x->op == CND_AVG_HOST || x->op == CND_AVG_INST ||
		x->op == CND_AVG_TIME ||
		x->op == CND_MAX_HOST || x->op == CND_MAX_INST ||
		x->op == CND_MAX_TIME ||
		x->op == CND_MIN_HOST || x->op == CND_MIN_INST ||
		x->op == CND_MIN_TIME ||
		x->op == CND_EQ || x->op == CND_NEQ ||
		x->op == CND_LT || x->op == CND_LTE ||
		x->op == CND_GT || x->op == CND_GTE ||
		x->op == CND_NOT || x->op == CND_AND || x->op == CND_OR ||
		x->op == CND_RISE || x->op == CND_FALL ||
		x->op == CND_MATCH || x->op == CND_NOMATCH) {
		instFetchExpr(x);
		findEval(x);
#if PCP_DEBUG
		if (pmDebug & DBG_TRACE_APPL1) {
		    fprintf(stderr, "reinitMetric: re-shaped ...\n");
		    dumpExpr(x);
		}
#endif
	    }
	    if (x->parent) {
		x = x->parent;
		if (x->metrics == m)
		    continue;
	    }
	    break;
	}
    }

end:
    /* destroy temporary context */
    pmDestroyContext(handle);

    return ret;
}


/* put initialised Metric onto fetch list */
void
bundleMetric(Host *h, Metric *m)
{
    Fetch	*f = findFetch(h, m);
    if (f == NULL) {
	/*
	 * creating new fetch bundle and pmNewContext failed ...
	 * not much choice here
	 */
	waitMetric(m);
    }
    else
	/* usual case */
	findProfile(findFetch(h, m), m);
}


/* reconnect attempt to host */
int
reconnect(Host *h)
{
    Fetch	*f;

    f = h->fetches;
    while (f) {
	if (pmReconnectContext(f->handle) < 0)
	    return 0;
	if (clientid != NULL)
	    /* re-register client id with pmcd */
	    __pmSetClientId(clientid);
	f = f->next;
    }
    return 1;
}


/* pragmatics analysis */
void
pragmatics(Symbol rule, RealTime delta)
{
    Expr	*x = symValue(rule);
    Task	*t;

    if (x->op != NOP) {
	t = findTask(delta);
	bundle(t, x);
	t->nrules++;
	t->rules = (Symbol *) ralloc(t->rules, t->nrules * sizeof(Symbol));
	t->rules[t->nrules-1] = symCopy(rule);
	perf->eval_expected += (float)1/delta;
    }
}

/*
 * find all expressions for a host that has just been marked "down"
 * and invalidate them
 */
static void
mark_all(Host *hdown)
{
    Task	*t;
    Symbol	*s;
    Metric	*m;
    Expr	*x;
    int		i;

    for (t = taskq; t != NULL; t = t->next) {
	s = t->rules;
	for (i = 0; i < t->nrules; i++, s++) {
	    x = (Expr *)symValue(*s);
	    for (m = x->metrics; m != NULL; m = m->next) {
		if (m->host == hdown)
		    clobber(x);
	    }
	}
    }
}

/* execute fetches for given Task */
void
taskFetch(Task *t)
{
    Host	*h;
    Fetch	*f;
    Profile	*p;
    Metric	*m;
    pmResult	*r;
    pmValueSet	**v;
    int		i;
    int		sts;

    /* do all fetches, quick as you can */
    h = t->hosts;
    while (h) {
	f = h->fetches;
	while (f) {
	    if (f->result) pmFreeResult(f->result);
	    if (! h->down) {
		pmUseContext(f->handle);
		if ((sts = pmFetch(f->npmids, f->pmids, &f->result)) < 0) {
		    if (! archives) {
			__pmNotifyErr(LOG_ERR, "pmFetch from %s failed: %s\n",
				symName(f->host->name), pmErrStr(sts));
			host_state_changed(symName(f->host->name), STATE_LOSTCONN);
			h->down = 1;
			mark_all(h);
		    }
		    f->result = NULL;
		}
	    }
	    else
		f->result = NULL;
	    f = f->next;
	}
	h = h->next;
    }

    /* sort and distribute pmValueSets to requesting Metrics */
    h = t->hosts;
    while (h) {
	if (! h->down) {
	    f = h->fetches;
	    while (f && (r = f->result)) {
		/* sort all vlists in result r */
		v = r->vset;
		for (i = 0; i < r->numpmid; i++) {
		    if ((*v)->numval > 0) {
			qsort((*v)->vlist, (size_t)(*v)->numval,
			      sizeof(pmValue), compair);
		    }
		    v++;
		}

		/* distribute pmValueSets to Metrics */
		p = f->profiles;
		while (p) {
		    m = p->metrics;
		    while (m) {
			for (i = 0; i < r->numpmid; i++) {
			    if (m->desc.pmid == r->vset[i]->pmid) {
				if (r->vset[i]->numval > 0) {
				    m->vset = r->vset[i];
				    m->stamp = realize(r->timestamp);
				}
				break;
			    }
			}
			m = m->next;
		    }
		    p = p->next;
		}
		f = f->next;
	    }
	}
	h = h->next;
    }
}


/* send pmDescriptors for all expressions in given task */
void
sendDescs(Task *task)
{
    Symbol	*s;
    int		i;

    s = task->rules;
    for (i = 0; i < task->nrules; i++) {
	sendDesc(symValue(*s), task->rslt->vset[i]);
	s++;
    }
}


/* convert Expr value to pmValueSet value */
void
fillVSet(Expr *x, pmValueSet *vset)
{
    if (x->valid > 0) {
	if (finite(*((double *)x->ring))) {	/* copy value */
	    vset->numval = 1;
	    memcpy(&vset->vlist[0].value.pval->vbuf, x->ring, sizeof(double));
	}
	else	/* value not representable */
	    vset->numval = 0;
    }
    else {	/* value not available */
	vset->numval = PM_ERR_VALUE;
    }
}
