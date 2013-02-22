/*
 * Mounts, info on current mounts
 *
 * Copyright (c) 2012 Red Hat.
 * Copyright (c) 2001,2003,2004 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (c) 2001 Alan Bailey (bailey@mcs.anl.gov or abailey@ncsa.uiuc.edu) 
 * for the portions of the code supporting the initial agent functionality.
 * All rights reserved. 
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
#include <dirent.h>
#include <sys/stat.h>

/*
 * Mounts PMDA
 *
 * Metrics
 *   mounts.device
 *     The device which the mount is mounted on
 *   mounts.type
 *     The type of filesystem
 *   mounts.options
 *     The mounting options
 *   mounts.up
 *     always equals 1
 */

/*
 * all metrics supported in this PMDA - one table entry for each
 */

#ifdef IS_SOLARIS
#define MOUNT_FILE "/etc/vfstab"
#else
#define MOUNT_FILE "/proc/mounts"
#endif

static pmdaInstid *mounts = NULL;

static pmdaIndom indomtab[] = {
#define MOUNTS_INDOM    0
  { MOUNTS_INDOM, 0, NULL }
};

static pmdaMetric metrictab[] = {
/* mounts.device */
    { NULL,
      { PMDA_PMID(0,0), PM_TYPE_STRING, MOUNTS_INDOM, PM_SEM_INSTANT,
        PMDA_PMUNITS(0,0,0,0,0,0) }, },
/* mounts.type */
    { NULL,
      { PMDA_PMID(0,1), PM_TYPE_STRING, MOUNTS_INDOM, PM_SEM_INSTANT,
        PMDA_PMUNITS(0,0,0,0,0,0) }, },
/* mounts.options */
    { NULL,
      { PMDA_PMID(0,2), PM_TYPE_STRING, MOUNTS_INDOM, PM_SEM_INSTANT,
        PMDA_PMUNITS(0,0,0,0,0,0) }, },
/* mounts.up */
    { NULL,
      { PMDA_PMID(0,3), PM_TYPE_DOUBLE, MOUNTS_INDOM, PM_SEM_INSTANT,
        PMDA_PMUNITS(0,0,0,0,0,0) }, },
};

/* Structures needed for this file */
typedef struct {
  int up;
  char device[100];
  char type[100];
  char options[100];
} mountinfo;

/* Variables needed for this file */
static mountinfo *mount_list = NULL;
static struct stat file_change;
static int isDSO = 1;
static char mypath[MAXPATHLEN];
static char *username;

/* Routines in this file */
static void mounts_clear_config_info(void);
static void mounts_grab_config_info(void);
static void mounts_config_file_check(void);

static void mounts_refresh_mounts(void);

static void
mounts_config_file_check(void) {
  /*
   * This code was taken from the simple PMDA, in simple.c, for checking
   * to see if a file has been changed.  It has been changed slightly.
   * Note that this then calls mounts_clear_config_info and
   * mounts_grab_config_info if the file has been updated.
   */

  struct stat statbuf;
  static int  last_error;
  int sep = __pmPathSeparator();

  snprintf(mypath, sizeof(mypath), "%s%c" "mounts" "%c" "mounts.conf",
		pmGetConfig("PCP_PMDAS_DIR"), sep, sep);
  if (stat(mypath, &statbuf) == -1) {
    if (oserror() != last_error) {
      last_error = oserror();
      __pmNotifyErr(LOG_WARNING, "stat failed on %s: %s\n",
		    mypath, pmErrStr(last_error));
    }
  } else {
    last_error = 0;
#if defined(HAVE_ST_MTIME_WITH_E)
    if (statbuf.st_mtime != file_change.st_mtime) {
#elif defined(HAVE_ST_MTIME_WITH_SPEC)
    if (statbuf.st_mtimespec.tv_sec != file_change.st_mtimespec.tv_sec ||
        statbuf.st_mtimespec.tv_nsec != file_change.st_mtimespec.tv_nsec) {
#else     
    if (statbuf.st_mtim.tv_sec != file_change.st_mtim.tv_sec ||
        statbuf.st_mtim.tv_nsec != file_change.st_mtim.tv_nsec) {
#endif
      mounts_clear_config_info();
      mounts_grab_config_info();
      file_change = statbuf;
    }
  }  
}       


static void
mounts_clear_config_info(void) {

  int i;

  /* Free the memory holding the mount name */
  for (i = 0; i < indomtab[MOUNTS_INDOM].it_numinst; i++) {
    free(mounts[i].i_name);
    mounts[i].i_name = NULL;
  }

  /* Free the mounts structure */
  if (mounts != NULL) {
    free(mounts);
  }

  /* Free the mount_list structure */
  if (mount_list != NULL) {
    free(mount_list);
  }

  mount_list = NULL;
  indomtab[MOUNTS_INDOM].it_set = mounts = NULL;
  indomtab[MOUNTS_INDOM].it_numinst = 0;

}


void mounts_grab_config_info() {
  /* 
   * This routine opens the config file and stores the information in the
   * mounts structure.  The mounts structure must be reallocated as
   * necessary, and also the num_procs structure needs to be reallocated
   * as we define new mounts.  When all of that is done, we fill in the
   * values in the indomtab structure, those being the number of instances
   * and the pointer to the mounts structure.
   *
   * A lot of this code was taken from the simple.c code, but in this
   * case, I changed a lot of it to fit my purposes...
   */

  FILE *fp;
  char mount_name[1024];
  char *q;
  int mount_number = 0;
  int sep = __pmPathSeparator();

  snprintf(mypath, sizeof(mypath), "%s%c" "mounts" "%c" "mounts.conf",
		pmGetConfig("PCP_PMDAS_DIR"), sep, sep);
  if ((fp = fopen(mypath, "r")) == NULL) {
    __pmNotifyErr(LOG_ERR, "fopen on %s failed: %s\n",
		  mypath, pmErrStr(-oserror()));
    if (mounts) {
	free(mounts);
	mounts = NULL;
	mount_number = 0;
    }
    goto done;
  }

  while (fgets(mount_name, sizeof(mount_name), fp) != NULL) {
    if (mount_name[0] == '#')
      continue;
    /* Remove the newline */
    if ((q = strchr(mount_name, '\n')) != NULL) {
      *q = '\0';
    } else {
      /* This means the line was too long */
      __pmNotifyErr(LOG_WARNING, "line number %d in the config file was too long\n"
          ,mount_number+1);
    }

    mounts = realloc(mounts, (mount_number+1)*sizeof(pmdaInstid));
    if (mounts == NULL) {
      __pmNoMem("process", (mount_number+1)*sizeof(pmdaInstid),
		PM_FATAL_ERR);
      /* Not reached */   
    }
    mounts[mount_number].i_name = malloc(strlen(mount_name) + 1);
    strcpy(mounts[mount_number].i_name, mount_name);
    mounts[mount_number].i_inst = mount_number;

    mount_number++;

  }
  fclose(fp);

done:
  if (mounts == NULL) {
    __pmNotifyErr(LOG_WARNING, "\"mounts\" instance domain is empty");
  }

  indomtab[MOUNTS_INDOM].it_set = mounts;
  indomtab[MOUNTS_INDOM].it_numinst = mount_number;

  mount_list = realloc(mount_list, (mount_number)*sizeof(mountinfo));
}


static void
mounts_refresh_mounts(void) {

  FILE *fd;
  char device[100];
  char mount[100];
  char type[100];
  char options[100];
  char junk[10];
  int item;
  int mount_name;

  /* Clear the variables */
  for(item = 0; item < indomtab[MOUNTS_INDOM].it_numinst; item++) {
    strcpy(mount_list[item].device, "none");
    strcpy(mount_list[item].type, "none");
    strcpy(mount_list[item].options, "none");
    mount_list[item].up = 0;
  }


  if ((fd = fopen(MOUNT_FILE, "r")) != NULL) {
#ifdef IS_SOLARIS
    char device_to_fsck[100];
    char fsck_pass[100];
    char mount_at_boot[100];

    while ((fscanf(fd, "%s %s %s %s %s %s %s", 
				   device, device_to_fsck, mount, type, fsck_pass, 
				   mount_at_boot, options)) == 7) {
#else
    while ((fscanf(fd, "%s %s %s %s", device, mount, type, options)) == 4) {
#endif
      if (fgets(junk, sizeof(junk), fd) == NULL) {
	/* early EOF? will be caught in next iteration */
	;
      }

      for(mount_name = 0; mount_name < indomtab[MOUNTS_INDOM].it_numinst; 
          mount_name++) {
        if (strcmp(mount, (mounts[mount_name]).i_name) == 0) {
          strcpy(mount_list[mount_name].device, device);
          strcpy(mount_list[mount_name].type, type);
          strcpy(mount_list[mount_name].options, options);
          mount_list[mount_name].up = 1;
        }
      }

      memset(device, 0, sizeof(device));
      memset(mount, 0, sizeof(mount));
      memset(type, 0, sizeof(type));
      memset(options, 0, sizeof(options));
    }
  }

  fclose(fd);

}


static int
mounts_fetch(int numpmid, pmID pmidlist[], pmResult **resp, pmdaExt *pmda) {
  /*
   * This is the wrapper over the pmdaFetch routine, to handle the problem
   * of varying instance domains.  All this does is delete the previous
   * mount list, and then get the current one, by calling
   * mounts_refresh_mounts.
   */

  mounts_config_file_check();
  mounts_refresh_mounts();
  return pmdaFetch(numpmid, pmidlist, resp, pmda);
}


/*
 * callback provided to pmdaFetch
 */
static int
mounts_fetchCallBack(pmdaMetric *mdesc, unsigned int inst, pmAtomValue *atom)
{
    __pmID_int		*idp = (__pmID_int *)&(mdesc->m_desc.pmid);

    if (idp->cluster != 0) {
      return PM_ERR_PMID;
    }
    if ((idp->item > 3) || (idp->item < 0)) {
      return PM_ERR_PMID;
    }

    if (idp->item == 0) {
      atom->cp = (mount_list[inst]).device;
    } else if (idp->item == 1) {
      atom->cp = (mount_list[inst]).type;
    } else if (idp->item == 2) {
      atom->cp = (mount_list[inst]).options;
    } else if (idp->item == 3) {
      atom->d = (mount_list[inst]).up;
      //      atom->d = 1;
    } else {
      return PM_ERR_INST;
    }

    return 0;
}



/*
 * Initialise the agent (both daemon and DSO).
 */
void 
mounts_init(pmdaInterface *dp)
{
    if (isDSO) {
      int sep = __pmPathSeparator();
      snprintf(mypath, sizeof(mypath), "%s%c" "mounts" "%c" "help",
		pmGetConfig("PCP_PMDAS_DIR"), sep, sep);
      pmdaDSO(dp, PMDA_INTERFACE_2, "mounts DSO", mypath);
    } else {
	__pmSetProcessIdentity(username);
    }

    if (dp->status != 0)
        return;

    dp->version.two.fetch = mounts_fetch;

    /* Let's grab the info right away just to make sure it's there. */
    mounts_grab_config_info();

    pmdaSetFetchCallBack(dp, mounts_fetchCallBack);

    pmdaInit(dp, indomtab, sizeof(indomtab)/sizeof(indomtab[0]), 
	     metrictab, sizeof(metrictab)/sizeof(metrictab[0]));
}

static void
usage(void)
{
    fprintf(stderr, "Usage: %s [options]\n\n", pmProgname);
    fputs("Options:\n"
	  "  -d domain    use domain (numeric) for metrics domain of PMDA\n"
	  "  -l logfile   write log into logfile rather than using default log name\n"
	  "  -U username  user account to run under (default \"pcp\")\n",
	      stderr);		
    exit(1);
}

/*
 * Set up the agent if running as a daemon.
 */
int
main(int argc, char **argv)
{
    int			c, sep = __pmPathSeparator();
    int			err = 0;
    pmdaInterface	desc;

    isDSO = 0;
    __pmSetProgname(argv[0]);
    __pmGetUsername(&username);

    snprintf(mypath, sizeof(mypath), "%s%c" "mounts" "%c" "help",
		pmGetConfig("PCP_PMDAS_DIR"), sep, sep);
    pmdaDaemon(&desc, PMDA_INTERFACE_2, pmProgname, MOUNTS,
		"mounts.log", mypath);

    while ((c = pmdaGetOpt(argc, argv, "D:d:l:U:?", &desc, &err)) != EOF) {
	switch(c) {
	case 'U':
	    username = optarg;
	    break;
	default:
	    err++;
	}
    }
    if (err)
	usage();

    pmdaOpenLog(&desc);
    mounts_init(&desc);
    pmdaConnect(&desc);
    pmdaMain(&desc);
    exit(0);
}
