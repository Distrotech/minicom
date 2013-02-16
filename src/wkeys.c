/*
 * wkeys.c	Read a keypress from the standard input. If it is an escape
 *		code, return a special value.
 *
 *		WARNING: possibly the most ugly code in this package!
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
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <strings.h>

#include "port.h"
#include "minicom.h"
#include "intl.h"

#if KEY_KLUDGE && defined(linux)
#  include <sys/kd.h>
#  include <sys/ioctl.h>
#endif

/* If enabled, this will cause minicom to treat ESC [ A and
 * ESC O A the same (stupid VT100 two mode keyboards).
 */
#define VT_KLUDGE 0

static struct key _keys[NUM_KEYS];
static int keys_in_buf;

static char erasechar;
static int gotalrm;
int pendingkeys = 0;
int io_pending = 0;

#ifndef NCURSES_CONST
#define NCURSES_CONST
#endif

static NCURSES_CONST char *func_key[] = {
  "", "k1", "k2", "k3", "k4", "k5", "k6", "k7", "k8", "k9", "k0",
  "kh", "kP", "ku", "kl", "kr", "kd", "kH", "kN", "kI", "kD",
  "F1", "F2", NULL };
#if KEY_KLUDGE
/*
 * A VERY DIRTY HACK FOLLOWS:
 * This routine figures out if the tty we're using is a serial
 * device OR an IBM PC console. If we're using a console, we can
 * easily reckognize single escape-keys since escape sequences
 * always return > 1 characters from a read()
 */
static int isconsole;

static int testconsole(void)
{
  /* For Linux it's easy to see if this is a VC. */
  int info;

  return ioctl(0, KDGETLED, &info) == 0;
}

/*
 * Function to read chunks of data from fd 0 all at once
 */

static int cread(char *c)
{
  static char buf[32];
  static int idx = 0;
  static int lastread = 0;

  if (idx > 0 && idx < lastread) {
    *c = buf[idx++];
    keys_in_buf--;
    if (keys_in_buf == 0 && pendingkeys == 0)
      io_pending = 0;
    return lastread;
  }
  idx = 0;
  do {
    lastread = read(0, buf, 32);
    keys_in_buf = lastread - 1;
  } while (lastread < 0 && errno == EINTR);

  *c = buf[0];
  if (lastread > 1) {
    idx = 1;
    io_pending++;
  }
  return lastread;
}
#endif

static void _initkeys(void)
{
  int i;
  static char *cbuf, *tbuf;
  char *term;

  if (_tptr == NULL) {
    if ((tbuf = (char *)malloc(512)) == NULL ||
        (cbuf = (char *)malloc(2048)) == NULL) {
      fprintf(stderr, _("Out of memory.\n"));
      exit(1);
    }
    term = getenv("TERM");
    switch (tgetent(cbuf, term)) {
      case 0:
        fprintf(stderr, _("No termcap entry.\n"));
        exit(1);
      case -1:
        fprintf(stderr, _("No /etc/termcap present!\n"));
        exit(1);
      default:
        break;
    }
    _tptr = tbuf;
  }
  /* Initialize codes for special keys */
  for (i = 0; func_key[i]; i++) {
    if ((_keys[i].cap = tgetstr(func_key[i], &_tptr)) == NULL)
      _keys[i].cap = "";
    _keys[i].len = strlen(_keys[i].cap);
  }
#if KEY_KLUDGE
  isconsole = testconsole();
#endif
}

/*
 * Read a character from the keyboard.
 * Handle special characters too!
 */
int wxgetch(void)
{
  int f, g;
  int match = 1;
  int len;
  char c;
  static unsigned char mem[8];
  static int leftmem = 0;
  static int init = 0;
  int nfound = 0;
  int start_match;
#if VT_KLUDGE
  char temp[8];
#endif
  struct timeval timeout;
  fd_set readfds;

  if (init == 0) {
    _initkeys();
    init++;
    erasechar = setcbreak(3);
  }

  /* Some sequence still in memory ? */
  if (leftmem > 0) {
    leftmem--;
    if (leftmem == 0)
      pendingkeys = 0;
    if (pendingkeys == 0 && keys_in_buf == 0)
      io_pending = 0;
    return mem[leftmem];
  }
  gotalrm = 0;
  pendingkeys = 0;

  for (len = 1; len < 8 && match; len++) {
#if KEY_KLUDGE
    if (len > 1 && keys_in_buf == 0)
#else
    if (len > 1)
#endif
    {
      timeout.tv_sec = 0;
      timeout.tv_usec = 400000; /* 400 ms */
      FD_ZERO(&readfds);
      FD_SET(0, &readfds);
      if (!(nfound = select(1, &readfds, NULL, NULL, &timeout)))
        break;
    }

#if KEY_KLUDGE
    while ((nfound = cread(&c)) < 0 && (errno == EINTR && !gotalrm))
      ;
#else
    while ((nfound = read(0, &c, 1)) < 0 && (errno == EINTR && !gotalrm))
      ;
#endif

    if (nfound < 1)
      return EOF;

    if (len == 1) {
      /* Enter and erase have precedence over anything else */
      if (c == '\n')
        return c;
      if (c == erasechar)
        return K_ERA;
    }
#if KEY_KLUDGE
    /* Return single characters immideately */
    if (isconsole && nfound == 1 && len == 1)
      return c;

    /* Another hack - detect the Meta Key. */
    if (isconsole && nfound == 2 && len == 1 && c == 27 && escape == 27) {
      cread(&c);
      return c + K_META;
    }
#endif
    mem[len - 1] = c;
    match = 0;
#if VT_KLUDGE
    /* Oh boy. Stupid vt100 2 mode keyboard. */
    strncpy(temp, mem, len);
    if (len > 1 && temp[0] == 27) {
      if (temp[1] == '[')
        temp[1] = 'O';
      else if (temp[1] == 'O')
        temp[1] = '[';
    }
    /* We now have an alternate string to check. */
#endif
    start_match = 0;
    for (f = 0; f < NUM_KEYS; f++) {
#if VT_KLUDGE
      if (_keys[f].len >= len &&
          (strncmp(_keys[f].cap, (char *)mem,  len) == 0 ||
           strncmp(_keys[f].cap, (char *)temp, len) == 0))
#else
        if (_keys[f].len >= len &&
            strncmp(_keys[f].cap, (char *)mem, len) == 0)
#endif
        {
          match++;
          if (_keys[f].len == len) {
            return f + KEY_OFFS;
          }
        }
      /* Does it match on first two chars? */
      if (_keys[f].len > 1 && len == 2 &&
          strncmp(_keys[f].cap, (char *)mem, 2) == 0)
        start_match++;
    }
#if KEY_KLUDGE
    if (!isconsole)
#endif
#ifndef _MINIX /* Minix doesn't have ESC-c meta mode */
      /* See if this _might_ be a meta-key. */
      if (escape == 27 && !start_match && len == 2 && mem[0] == 27)
        return c + K_META;
#endif
  }
  /* No match. in len we have the number of characters + 1 */
  len--; /* for convenience */
  if (len == 1)
    return mem[0];
  /* Remember there are more keys waiting in the buffer */
  pendingkeys++;
  io_pending++;

  /* Reverse the "mem" array */
  for (f = 0; f < len / 2; f++) {
    g = mem[f];
    mem[f] = mem[len - f - 1];
    mem[len - f - 1] = g;
  }
  leftmem = len - 1;
  return mem[leftmem];
}
