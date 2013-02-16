/*
 * getsdir.c
 *
 *	Get and return a sorted directory listing
 *
 *	Copyright (c) 1998 by James S. Seymour (jseymour@jimsun.LinxNet.com)
 *
 *	This code is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *	Note: this code uses "wildmat.c", which has different copyright
 *	and licensing conditions.  See the source, Luke.
 *
 *
 *  2011: getsdir() has been simplified wrt memory management by
 *        Adam Lackorzynski
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include "getsdir.h"
#include "intl.h"
#include "minicom.h"


/* locally defined constants */

#define MAX_CNT 100		/* number of entries to hold in holding buf */

typedef struct dat_buf {		/* structure of input buffers */
  struct dat_buf *nxt;			/* pointer to next buffer */
  unsigned cnt;				/* data count in present buffer */
  GETSDIR_ENTRY data[MAX_CNT];		/* data in present buffer */
} DAT_BUF;

static int g_sortflags;			/* sort flags */

/* sort compare routines */

/*
 * name:	namecmpr
 *
 * purpose:	return stat to qsort on comparison between name fields in
 *		directory entry.
 *
 * synopsis:	static in namecmpr(d1, d2)
 *		GETSDIR_ENTRY *d1;
 *		GETSDIR_ENTRY *d2;
 *
 * input:	See explanation of qsort
 *
 * process:	See explanation of qsort
 *
 * output:	See explanation of qsort
 *
 * notes:	See explanation of qsort
 */
static int namecmpr(GETSDIR_ENTRY *d1, GETSDIR_ENTRY *d2)
{
  if (g_sortflags & (GETSDIR_DIRSF | GETSDIR_DIRSL)) {
    if (S_ISDIR((d1->mode)) && !S_ISDIR((d2->mode)))
      return (g_sortflags & GETSDIR_DIRSF) ? -1 : 1;
    else if (S_ISDIR((d2->mode)) && ! S_ISDIR((d1->mode)))
      return (g_sortflags & GETSDIR_DIRSF) ? 1 : -1;
  }

  return (g_sortflags & GETSDIR_RSORT)
          ? strcmp(d2->fname, d1->fname) : strcmp(d1->fname, d2->fname);

} /* namecmpr */


/*
 * name:	timecmpr
 *
 * purpose:	return stat to qsort on comparison between time fields in
 *		directory entry.
 *
 * synopsis:	static in timecmpr(d1, d2)
 *		GETSDIR_ENTRY *d1;
 *		GETSDIR_ENTRY *d2;
 *
 * input:	See explanation of qsort
 *
 * process:	See explanation of qsort
 *
 * output:	See explanation of qsort
 *
 * notes:	See explanation of qsort
 */
static int timecmpr(GETSDIR_ENTRY *d1, GETSDIR_ENTRY *d2)
{
  if (g_sortflags & (GETSDIR_DIRSF | GETSDIR_DIRSL)) {
    if (S_ISDIR((d1->mode)) && !S_ISDIR((d2->mode)))
      return (g_sortflags & GETSDIR_DIRSF) ? -1 : 1;
    else if (S_ISDIR((d2->mode)) && ! S_ISDIR((d1->mode)))
      return (g_sortflags & GETSDIR_DIRSF) ? 1 : -1;
  }

  return (g_sortflags & GETSDIR_RSORT)
         ? (d2->time - d1->time) : (d1->time - d2->time);
} /* timecmpr */

/*
 * name:	getsdir
 *
 * purpose:	To return a directory listing - possibly sorted
 *
 * synopsis:	#include <dirent.h>
 *
 *		int getsdir(dirpath, pattern, sortflags, modemask, datptr, len)
 *		const char *dirpath;
 *		const char *pattern;
 *		int sortflags;
 *		mode_t modemask;
 *		GETSDIR_ENTRY **datptr;
 *		int *len;
 *
 * input:	 *dirpath - pointer to path to directory to get list of
 *			    files from
 *		 *pattern - pointer to optional wildmat pattern
 *		sortflags - specification flags of how to sort the
 *			    resulting list.  See descriptions below.
 *		 modemask - caller-supplied mode mask.  Bits in this will
 *			    be ANDed against directory entries to determine
 *			    whether to return them.  Ignored if 0.
 *		 **datptr - pointer to a destination pointer variable.
 *			    getsdir will allocate the required amount
 *			    of memory for the results and will return a
 *			    pointer to the returned data in this variable.
 *
 *			    The data will be in the form:
 *				typedef struct dirEntry {
 *				    char fname[MAXNAMLEN + 1];
 *				    time_t time;
 *				    mode_t mode;
 *				} GETSDIR_ENTRY;
 *		     *len - pointer to int to contain length of longest
 *			    string in returned array.
 *
 * process:	For each 0..MAX_CNT values read from specified directory,
 *		allocates a temporary buffer to store the entries into.
 *		When end of directory is detected, merges the buffers into
 *		a single array of data and sorts into order based on key.
 *
 * output:	Count of number of data items pointed to by datptr or
 *		-1 if error.  errno may or may not be valid, based on
 *		type of error encountered.
 *
 * notes:	If there is any error, -1 is returned.
 *
 *		It is the caller's responsibility to free the memory
 *		block pointed to by datptr on return when done with the
 *		data, except in case of error return.
 *
 *		See also: opendir(3C), readdir(3C), closedir(3C), qsort(3C)
 *
 *		The pattern parameter is optional and may be 0-length or
 *		a NULL pointer.
 *
 *		The sort flags affect the output as follows:
 *
 *		    GETSDIR_PARNT - include parent dir (..)
 *		    GETSDIR_NSORT - sort by name
 *		    GETSDIR_TSORT - sort by time (NSORT wins if both)
 *
 *		    The following are only meaningful if GETSDIR_NSORT or
 *		    GETSDIR_TSORT are specified:
 *
 *			GETSDIR_DIRSF - dirs first
 *			GETSDIR_DIRSL - dirs last
 *			GETSDIR_RSORT - reverse sort (does not affect
 *					GETSDIR_DIRSF/GETSDIR_DIRSL)
 *
 *		So-called "hidden" files (those beginning with a ".") are
 *		not returned unless a pattern like ".*" is specified.
 *
 *		The present directory (".") is never returned.
 */

int getsdir(const char *dirpath, const char *pattern, int sortflags,
            mode_t modemask, GETSDIR_ENTRY **datptr, int *len)
{
  unsigned cnt = 0;		/* data count */

  DIR *dirp;			/* point to open dir */
  struct dirent *dp;		/* structure of dir as per system */
  struct stat statbuf;		/* structure of file stat as per system */
  char fpath[BUFSIZ];		/* filename with dir path prepended */
  int cmprstat;

  g_sortflags = sortflags;	/* for sort funcs */
  *len = 0;			/* longest name */

  /* open the specified directory */
  if ((dirp = opendir(dirpath)) == NULL)
    return -1;

  while ((dp = readdir(dirp)))
    {
      if (!strcmp(dp->d_name, "."))
        continue;

      if ((sortflags & GETSDIR_PARNT) && !strcmp(dp->d_name, ".."))
        cmprstat = 1;
      else if (pattern && *pattern)
        cmprstat = wildmat(dp->d_name, pattern);
      else
        cmprstat = 1;

      if (cmprstat)
        {			/* matching name? */
          *datptr = realloc(*datptr, sizeof(**datptr) * (cnt + 1));
          if (!*datptr)
            {
              free(*datptr);
              return -1;
            }

          /* copy the filename */
          strncpy((*datptr)[cnt].fname, dp->d_name, MAXNAMLEN);

          /* get information about the directory entry */
          snprintf(fpath, sizeof(fpath), "%s/%s", dirpath, dp->d_name);
          if (stat(fpath, &statbuf))	/* if error getting stat... */
            continue;

          if (modemask && !(S_IFMT & modemask & statbuf.st_mode))
            continue;

          int l;
          if ((l = strlen(dp->d_name)) > *len)
            *len = l;

          (*datptr)[cnt].time   = statbuf.st_mtime;
          (*datptr)[cnt].mode   = statbuf.st_mode;
          (*datptr)[cnt].cflags = 0;

          cnt++;
        }
    }

  closedir(dirp);		/* close file pointer */

  /* post-process array by option */
  if (cnt && sortflags) {
    if (sortflags & GETSDIR_NSORT)
      qsort(*datptr, cnt, sizeof(GETSDIR_ENTRY),
            (int (*)(const void *, const void *))namecmpr);
    else if (sortflags & GETSDIR_TSORT)
      qsort(*datptr, cnt, sizeof(GETSDIR_ENTRY),
            (int (*)(const void *, const void *))timecmpr);
  }

  return cnt;
} /* getsdir */



#ifdef GETSDIR_STANDALONE_TEST
/*
 * debug for getsdir()
 *
 * usage: getsdir <dirpath>
 *
 */
extern char *ctime(void);

void main(int argc, char **argv)
{
  GETSDIR_ENTRY *dirdat;
  int cnt, index;
  int sortflags = 0;
  mode_t modemask = (mode_t) 0;
  int len;

  if (argc != 4) {
    fprintf(stderr,"usage: %s <dirpath> <pattern> <sortflags>\n", argv[0]);
    exit(1);
  }

  switch (argv[3][0]) {
    case 'n': sortflags = GETSDIR_NSORT;
              break;
    case 't': sortflags = GETSDIR_TSORT;
              break;
  }

  /* sortflags |= GETSDIR_DIRSL | GETSDIR_RSORT; */
  sortflags |= GETSDIR_DIRSF;
  /* sortflags |= GETSDIR_PARNT; */
  /* modemask = S_IFDIR | S_IFREG; */

  printf("modemask==%x\n", modemask);

  if ((cnt = getsdir(argv[1], argv[2], sortflags, modemask, &dirdat, &len)) == -1) {
    fprintf(stderr, "%s: error getting directory\n", argv[0]);
    exit(1);
  }

  printf(_("%d files:\n"), cnt);
  for (index = 1; index <= cnt; ++index, ++dirdat)
    printf("%2d: %-20s%s", index, dirdat->fname, ctime(&dirdat->time));

  free(dirdat);
  return 0;
}
#endif
