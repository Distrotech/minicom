/*
 * util.c       Little helper routines that didn't fit anywhere else.
 *
 *		This file is part of the minicom communications package,
 *		Copyright 1991-1995 Miquel van Smoorenburg.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * jseymour@jimsun.LinxNet.com (Jim Seymour) 03/26/98 - Added get_port()
 *    function to support multiple port specifications in config.
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "port.h"
#include "minicom.h"
#include "intl.h"

/*
 * A modified version of the getargs routine.
 */
static int getargs(char *s, char **arps, int maxargs)
{
  register int i;
  register char *sp;
  register char qchar;
  int literal = 0;

  i = 0;
  while (i < maxargs) {
    while (*s == ' ' || *s == '\t')
      ++s;
    if (*s == '\n' || *s == '\0')
      break;
    arps[i++] = sp = s;
    qchar = 0;
    while(*s != '\0'  &&  *s != '\n') {
      if (literal) {
	literal = 0;
	*sp++ = *s++;
	continue;
      }
      literal = 0;
      if (qchar == 0 && (*s == ' ' || *s == '\t')) {
	++s;
	break;
      }
      switch(*s) {
	default:
	  *sp++ = *s++;
	  break;
	case '\\':
	  literal = 1;
	  s++;
	  break;
	case '"':
	case '\'':
	  if(qchar == *s) {
	    qchar = 0;
	    ++s;
	    break;
	  }
	  if(qchar)
	    *sp++ = *s++;
	  else
	    qchar = *s++;
	  break;
      }
    }
    *sp++ = 0;
  }
  if (i >= maxargs)
    return -1;
  arps[i] = NULL;
  return i;
}

/*
 * Is a character from s2 in s1?
 */
#if 0
static int anys(const char *s1, const char *s2)
{
  while (*s2)
    if (strchr(s1, *s2++))
      return 1;
  return 0;
}
#endif

/*
 * If there is a shell-metacharacter in "cmd",
 * call a shell to do the dirty work.
 */
int fastexec(char *cmd)
{
  char *words[128];
  char *p;

  /* This is potentially security relevant (e.g. user selects a file
   * with embedded shellcode for upload), so disable it for now and
   * see if someone complains.     27. 09. 2003 */
#if 0
  if (anys(cmd, "~`$&*()=|{};?><"))
    return execl("/bin/sh", "sh", "-c", cmd, NULL);
#endif

  /* Delete escape-characters ment for the shell */
  p = cmd;
  while ((p = strchr(p, '\\')) && *(p+1) != ' ')
    memmove(p, p + 1, strlen(p+1));

  /* Split line into words */
  if (getargs(cmd, words, 127) < 0)
    return -1;
  return execvp(words[0], words);
}

/*
 * Fork, then redirect I/O if neccesary.
 * in    : new stdin
 * out   : new stdout
 * err   : new stderr
 * Returns exit status of "cmd" on success, -1 on error.
 */
int fastsystem(char *cmd, char *in, char *out, char *err)
{
  int pid;
  int st;
  int async = 0;
  char *p;

  /* If the command line ends with '&', don't wait for child. */
  p = strrchr(cmd, '&');
  if (p != (char *)0 && !p[1]) {
    *p = 0;
    async = 1;
  }
  
  /* Fork. */
  if ((pid = fork()) == 0) { /* child */
    if (in) {
      close(0);
      if (open(in, O_RDONLY) < 0)
        exit(-1);
    }
    if (out) {
      close(1);
      if (open(out, O_WRONLY) < 0)
        exit(-1);
    }
    if (err) {
      close(2);
      if (open(err, O_RDWR) < 0)
        exit(-1);
    }
    exit(fastexec(cmd));
  } else if (pid > 0) { /* parent */
    if (async)
      return 0;
    pid = m_wait(&st);
    if (pid < 0)
      return -1;
    return st;
  }
  return -1;
}

/*
 * Get next port from a space-, comma-, or semi-colon-separated
 * list (we're easy :-)) in a PARS_VAL_LEN length string.
 *
 * Returns NULL pointer on end-of-list.
 *
 * This would appear to be more complicated than it needs be.
 *
 * WARNING: Not MT-safe.  Multiple calls to this routine modify the same
 * local static storage space.
 */
char * get_port(char *port_list)
{
  static char next_port[PARS_VAL_LEN];
  static char loc_port_list[PARS_VAL_LEN];
  static char *sp = NULL;
  static char *ep;

  /* first pass? */
  if (sp == NULL) {
    strncpy(loc_port_list, port_list, PARS_VAL_LEN);
    loc_port_list[PARS_VAL_LEN - 1] = 0;
    ep = &loc_port_list[strlen(loc_port_list)];
    sp = strtok(loc_port_list, ";, ");
  }
  else if (*sp != 0)
    sp = strtok(sp, ";, ");
  else
    sp = NULL;

  if (sp != NULL) {
    strncpy(next_port, sp, PARS_VAL_LEN);
    next_port[PARS_VAL_LEN - 1] = 0;
    /* point to next token--skipping multiple occurrences of delimiters */
    for (sp += strlen(next_port); sp != ep && *sp != '/'; ++sp)
      ;
    return next_port;
  }
  else
    return NULL;
}
