/*
 * Copyright (c) 2012-2013 Red Hat.
 * Copyright (c) 1997-2001 Silicon Graphics, Inc.  All Rights Reserved.
 */

/* Check all ops except one turned off for all combinations.
 * Check connection limits
 */

#include <pcp/pmapi.h>
#include <pcp/impl.h>

int
main()
{
    int			s, sts, op, host;
    unsigned int	i;
    char		name[20];
    __pmSockAddr	*inaddr;

    sts = 0;
    for (op = 0; op < WORD_BIT; op++)
	if ((s = __pmAccAddOp(1 << op)) < 0) {
	    printf("Bad op %d: %s\n", op, strerror(errno));
	    sts = s;
	}

    if (sts < 0)
	exit(1);

    for (host = 0; host < WORD_BIT; host++) {
	sprintf(name, "155.%d.%d.%d", host * 3, 17+host, host);
	if ((s = __pmAccAddHost(name, ~(1 << host), ~(1 << host), host)) < 0) {
	    printf("cannot add host for op%d: %s\n", host, strerror(s));
	    sts = s;
	}
    }
    if (sts < 0)
	exit(1);

    putc('\n', stderr);
    __pmAccDumpHosts(stderr);

    for (host = 0; host < WORD_BIT; host++) {
	int	j;

	for (j = 0; j <= host; j++) {
	    char	buf[20];
	    sprintf(buf, "%d.%d.%d.%d", 155, host * 3, 17+host, host);
	    if ((inaddr =__pmStringToSockAddr(buf)) == NULL) {
	      printf("insufficient memory\n");
	      continue;
	    }
	    sts = __pmAccAddClient(inaddr, &i);
	    __pmSockAddrFree(inaddr);
	    if (sts < 0) {
		if (j == host && sts == PM_ERR_CONNLIMIT)
		    continue;
		printf("add client from host %d (j=%d): %s\n",
		       j, host, pmErrStr(sts));
		continue;
	    }
	    else if (i != (~(1 << host)))
		printf("host %d: __pmAccAddClient returns denyOpsResult 0x%x (expected 0x%x)\n",
		       host, i, ~(1 << host));
	}
    }

    putc('\n', stderr);
    __pmAccDumpHosts(stderr);

    exit(0);
}
