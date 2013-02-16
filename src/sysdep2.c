/*
 * sysdep2.c	System dependant routines
 *
 *		getrowcols	- get number of columns and rows.
 *		setcbreak	- set tty mode to raw, cbreak or normal.
 *		enab_sig	- enable / disable tty driver signals.
 *		strtok		- for systems that don't have it.
 *		dup2		- for ancient systems like SVR2.
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

#include "port.h"
#include "sysdep.h"

#ifdef POSIX_TERMIOS
static struct termios savetty;
#else
static struct sgttyb savetty;
static struct tchars savetty2;
#endif

/* Get the number of rows and columns for this screen. */
void getrowcols(int *rows, int *cols)
{
  char *p;

#ifdef TIOCGWINSZ
  struct winsize ws;

  if (ioctl(0, TIOCGWINSZ, &ws) >= 0) {
    *rows = ws.ws_row;
    *cols = ws.ws_col;
  }
#else
#  ifdef TIOCGSIZE
  struct ttysize ws;

  if (ioctl(0, TIOCGSIZE, &ws) >= 0) {
    *rows = ws.ts_lines;
    *cols = ws.ts_cols;
  }
#  endif
#endif
  /* An extra check here because eg. SCO does have TIOCGWINSZ
   * defined but the support is not in the kernel (ioctl
   * returns -1. Yeah :-(
   */
  if (*rows == 0 && (p = getenv("LINES")) != NULL)
    *rows = atoi(p);
  if (*cols == 0 && (p = getenv("COLUMNS")) != NULL)
    *cols = atoi(p);
}

/*
 * Set cbreak mode.
 * Mode 0 = normal.
 * Mode 1 = cbreak, no echo
 * Mode 2 = raw, no echo.
 * Mode 3 = only return erasechar (for wkeys.c)
 *
 * Returns: the current erase character.
 */
int setcbreak(int mode)
{
#ifdef POSIX_TERMIOS
  struct termios tty;
  static int init = 0;
  static int erasechar;

#ifndef XCASE
#  ifdef _XCASE
#    define XCASE _XCASE
#  else
#    define XCASE 0
#  endif
#endif

  if (init == 0) {
    tcgetattr(0, &savetty);
    erasechar = savetty.c_cc[VERASE];
    init++;
  }

  if (mode == 3)
    return erasechar;

  /* Always return to default settings first */
  tcsetattr(0, TCSADRAIN, &savetty);

  if (mode == 0) {
    return erasechar;
  }

  tcgetattr(0, &tty);
  if (mode == 1) {
    tty.c_oflag &= ~OPOST;
    tty.c_lflag &= ~(XCASE|ECHONL|NOFLSH);
    tty.c_lflag &= ~(ICANON | ISIG | ECHO);
    tty.c_iflag &= ~(ICRNL|INLCR);
    tty.c_cflag |= CREAD;
    tty.c_cc[VTIME] = 5;
    tty.c_cc[VMIN] = 1;
  }
  if (mode == 2) { /* raw */
    tty.c_iflag &= ~(IGNBRK | IGNCR | INLCR | ICRNL | IUCLC |
        IXANY | IXON | IXOFF | INPCK | ISTRIP);
    tty.c_iflag |= (BRKINT | IGNPAR);
    tty.c_oflag &= ~OPOST;
    tty.c_lflag &= ~(XCASE|ECHONL|NOFLSH);
    tty.c_lflag &= ~(ICANON | ISIG | ECHO);
    tty.c_cflag |= CREAD;
    tty.c_cc[VTIME] = 5;
    tty.c_cc[VMIN] = 1;
  }
  tcsetattr(0, TCSADRAIN, &tty);
  return erasechar;
#else
  struct sgttyb args;
  static int init = 0;
  static int erasechar;
#ifdef _BSD43
  static struct ltchars ltchars;
#endif

  if (init == 0) {
    ioctl(0, TIOCGETP, &savetty);
    ioctl(0, TIOCGETC, &savetty2);
#ifdef _BSD43
    ioctl(0, TIOCGLTC, &ltchars);
#endif
    erasechar = savetty.sg_erase;
    init++;
  }

  if (mode == 3)
    return erasechar;

  if (mode == 0) {
    ioctl(0, TIOCSETP, &savetty);
    ioctl(0, TIOCSETC, &savetty2);
#ifdef _BSD43
    ioctl(0, TIOCSLTC, &ltchars);
#endif
    return erasechar;
  }

  ioctl(0, TIOCGETP, &args);
  if (mode == 1) {
    args.sg_flags |= CBREAK;
    args.sg_flags &= ~(ECHO|RAW);
  }
  if (mode == 2) {
    args.sg_flags |= RAW;
    args.sg_flags &= ~(ECHO|CBREAK);
  }
  ioctl(0, TIOCSETP, &args);
  return erasechar;
#endif

}

/* Enable / disable signals from tty driver */
void enab_sig(int onoff, int intchar)
{
#ifdef POSIX_TERMIOS
  struct termios tty;

  tcgetattr(0, &tty);
  if (onoff)
    tty.c_lflag |= ISIG;
  else
    tty.c_lflag &= ~ISIG;
  /* Set interrupt etc. characters: Zmodem support. */
  if (onoff && intchar) {
    tty.c_cc[VINTR] = intchar;
    tty.c_cc[VQUIT] = -1;
#ifdef VSUSP
    tty.c_cc[VSUSP] = -1;
#endif
  }

  tcsetattr(0, TCSADRAIN, &tty);
#endif
#ifdef _V7
  struct tchars tch;
  struct sgttyb sg;

  ioctl(0, TIOCGETP, &sg);
  ioctl(0, TIOCGETC, &tch);
  if (onoff) {
    sg.sg_flags &= ~RAW;
    sg.sg_flags |= CBREAK;
  } else {
    sg.sg_flags &= ~CBREAK;
    sg.sg_flags |= RAW;
  }
  if (onoff && intchar) {
    tch.t_intrc = intchar;
    tch.t_quitc = -1;
  }
  ioctl(0, TIOCSETP, &sg);
  ioctl(0, TIOCSETC, &tch);
#endif
}

#ifdef _SVR2
/* Fake the dup2() system call */
int dup2(int from, int to)
{
  int files[20];
  int n, f, exstat = -1;

  /* Ignore if the same */
  if (from == to)
    return to;

  /* Initialize file descriptor table */
  for (f = 0; f < 20; f++)
    files[f] = 0;

  /* Close "to" file descriptor, if open */
  close(to);

  /* Keep opening files until we reach "to" */
  while ((n = open("/dev/null", 0)) < to && n >= 0) {
    if (n == from)
      break;
    files[n] = 1;
  }
  if (n == to) {
    /* Close "to" again, and perform dup() */
    close(n);
    exstat = dup(from);
  } else {
    /* We failed. Set exit status and errno. */
    if (n > 0)
      close(n);
    exstat = -1;
    errno = EBADF;
  }
  /* Close all temporarily opened file descriptors */
  for (f = 0; f < 20; f++)
    if (files[f])
      close(f);

  /* We're done. */
  return exstat;
}
#endif
