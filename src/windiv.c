/*
 * windiv.c	Some extra window routines for minicom, that
 *		I did not want to fold into window.c
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
 * hgk+jl 02.98 File selection window (no longer used this way..)
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <dirent.h>
#include <sys/stat.h>
#include "port.h"
#include "minicom.h"
#include "intl.h"

#ifndef max
  #define max(a,b)	((a)>(b)?(a):(b))
#endif

#ifndef min
  #define min(a,b)	((a)<(b)?(a):(b))
#endif

/*
 * Popup a window and put a text in it.
 */
static WIN *vmc_tell(const char *fmt, va_list va)
{
  WIN *w;
  char buf[128];

  if (stdwin == NULL)
    return NULL;

  vsnprintf(buf, sizeof(buf), fmt, va);

  w = mc_wopen((COLS / 2) - 2 - mbslen(buf) / 2, 8,
	    (COLS / 2) + 2 + mbslen(buf) / 2, 10,
	     BDOUBLE, stdattr, mfcolor, mbcolor, 0, 0, 1);
  mc_wcursor(w, CNONE);
  mc_wlocate(w, 2, 1);
  mc_wputs(w, buf);
  mc_wredraw(w, 1);
  return w;
}

WIN *mc_tell(const char *s, ...)
{
  WIN *w;
  va_list ap;

  va_start(ap, s);
  w = vmc_tell(s, ap);
  va_end(ap);
  return w;
}

/*
 * Show an error message.
 */
void werror(const char *s, ...)
{
  WIN *tellwin;
  va_list ap;

  va_start(ap, s);
  tellwin = vmc_tell(s, ap);
  va_end(ap);
  sleep(2);
  mc_wclose(tellwin, 1);
}

/*
 * Vertical "mc_wselect" function.
 */
int ask(const char *what, const char **s)
{
  int num = 0;
  int cur = 0, ocur = 0;
  int f, c;
  WIN *w;
  unsigned int size, offs;

  for (f = 0; s[f]; f++)
    num++;

  size = 5 * num;
  offs = 0;
  if (mbslen(what) > 2 * size + 4) {
    size = mbslen(what) / 2 + 2;
    offs = size - 5*num;
  }
  w = mc_wopen((COLS / 2) - size , 8, (COLS / 2) + 1 + size, 9,
             BSINGLE, stdattr, mfcolor, mbcolor, 0, 0, 1);

  dirflush = 0;

  mc_wcursor(w, CNONE);
  mc_wlocate(w, 1 + size - (mbslen(what) / 2), 0);
  mc_wputs(w, what);

  for (f = 1; f < num; f++) {
    mc_wlocate(w, 2 + offs + 10*f, 1);
    mc_wputs(w, _(s[f]));
  }
  mc_wredraw(w, 1);

  while (1) {
    mc_wlocate(w, 2 + offs + 10 * cur, 1);
    if (!useattr)
      mc_wprintf(w, ">%s", _(s[cur]) + 1);
    else {
      mc_wsetattr(w, XA_REVERSE | stdattr);
      mc_wputs(w, _(s[cur]));
    }
    ocur = cur;
    mc_wflush();
    switch (c = wxgetch()) {
      case ' ':
      case 27:
      case 3:
        dirflush = 1;
        mc_wclose(w, 1);
        return -1;
      case '\r':
      case '\n':
        dirflush = 1;
        mc_wclose(w, 1);
        return cur;
      case K_LT:
      case 'h':
        cur--;
        if (cur < 0)
          cur = num - 1;
        break;
      default:
        cur = (cur + 1) % num;
        break;
    }
    mc_wlocate(w, 2 + offs + 10 * ocur, 1);
    mc_wsetattr(w, stdattr);
    if (!useattr)
      mc_wputs(w, " ");
    else
      mc_wputs(w, _(s[ocur]));
  }
}

/*
 * Popup a window and ask for input.
 */
char *input(char *s, char *buf)
{
  WIN *w;

  w = mc_wopen((COLS / 2) - 20, 11, (COLS / 2) + 20, 12,
             BDOUBLE, stdattr, mfcolor, mbcolor, 1, 0, 1);
  mc_wputs(w, s);
  mc_wlocate(w, 0, 1);
  mc_wprintf(w, "> %-38.38s", buf);
  mc_wlocate(w, 2, 1);
  if (mc_wgets(w, buf, 38, 128) < 0)
    buf = NULL;
  mc_wclose(w, 1);
  return buf;
}
