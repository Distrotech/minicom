/*
 * common.c	Functions common to minicom and runscript programs
 *
 *		This file is part of the minicom communications package,
 *		Copyright 1991-1995 Miquel van Smoorenburg,
 *		1997-1998 Jukka Lahtinen.
 *
 *		This program is free software; you can redistribute it or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *		Functions
 *		char *pfix_home(char *)   - prefix filename with home directory
 *		void do_log(const char *) - write a line to the logfile
 *
 *		moved from config.c to a separate file, so they are easier
 *		to use in both the Minicom main program and runscript.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * 27.10.98 jl  converted do_log to use stdarg
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <limits.h>

#include "port.h"
#include "minicom.h"
#include <stdarg.h>

/* Prefix a non-absolute file with the home directory. */
char *pfix_home(char *s)
{
#if defined(FILENAME_MAX)
  static char buf[FILENAME_MAX];
#else
  static char buf[256];
#endif

  if (s && *s != '/') {
    snprintf(buf, sizeof(buf),"%s/%s", homedir, s);
    return buf;
  }
  return s;
}

void do_log(const char *line, ...)
{
#ifdef LOGFILE
/* Write a line to the log file.   jl 22.06.97 */
  FILE *logfile;
  char *logname = pfix_home(logfname);
  struct tm *ptr;
  time_t    ttime;
  va_list   ap;

  if (logfname[0] == 0)
    return;
  logfile = fopen(logname,"a");
  if (!logfile)
    return;

  va_start(ap, line);
  ttime = time(NULL);
  ptr = localtime(&ttime);

  fprintf(logfile,"%04d%02d%02d %02d:%02d:%02d ",
	  (ptr->tm_year)+1900, (ptr->tm_mon)+1, ptr->tm_mday,
	  ptr->tm_hour, ptr->tm_min, ptr->tm_sec);
  vfprintf(logfile, line, ap);
  fprintf(logfile, "\n");
  fclose(logfile);
#else
  /* dummy, don't do anything */
  (void)line;
#endif
}

/* mbtowc (), except that mbtowc (.. , "", ..) == 1, errors are treated as
 * (wchar_t)*s */
size_t one_mbtowc(wchar_t *pwc, const char *s, size_t n)
{
  int len;

  len = mbtowc(pwc, s, n);
  if (len == -1)
    *pwc = *s;
  if (len <= 0)
    len = 1;
  return len;
}

/* wctomb (), except that mbtowc (.. , 0) == 1, errors are treated as
 * *s = (char)wchar */
size_t
one_wctomb(char *s, wchar_t wchar)
{
  int len;

  len = wctomb(s, wchar);
  if (len == -1)
    s[0] = (char)wchar;
  if (len <= 0)
    len = 1;
  return len;
}

/* Number of characters in S */
size_t
mbslen(const char *s)
{
  size_t len;

  len = 0;
  while (*s != 0) {
    wchar_t wc;

    s += one_mbtowc(&wc, s, MB_LEN_MAX);
    len++;
  }
  return len;
}
