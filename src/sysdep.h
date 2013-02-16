/*
 * sysdep.h	Header file for the really system dependant routines
 *		in sysdep1.c and sysdep2.c. Because of this, the
 *		header file "port.h" is not used when "sysdep.h" is
 *		included. This file is only included from sysdep[12].c
 *		anyway!
 *
 *		$Id: sysdep.h,v 1.6 2008-03-21 20:27:56 al-guest Exp $
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
#ifdef HAVE_FEATURES_H
#include <features.h>
#endif
#include <sys/types.h>

/* Include standard Posix header files. */
#ifdef HAVE_UNISTD_H
#  include <stdlib.h>
#  include <unistd.h>
#endif
#ifdef HAVE_SYS_WAIT_H /* coherent 3 doesn't have it ? */
#  include <sys/wait.h>
#endif
/* Now see if we need to use sgtty, termio or termios. */
#ifdef _SCO
#  define _IBCS2 /* So we get struct winsize :-) */
#endif
#ifdef POSIX_TERMIOS
#  include <termios.h>
#else
#  ifdef HAVE_TERMIO_H
#    include <termios.h>
#  else
#    define _V7
#    ifdef HAVE_SGTTY_H
#      include <sgtty.h>
#    endif
#  endif
#endif
#ifdef HAVE_SYS_IOCTL_H 
#  include <sys/ioctl.h>
#endif

/* And more "standard" include files. */
#include <stdio.h>
#include <setjmp.h>

/* Be sure we know WEXITSTATUS and WTERMSIG */
#if !defined(_BSD43)
#  ifndef WEXITSTATUS
#    define WEXITSTATUS(s) (((s) >> 8) & 0377)
#  endif
#  ifndef WTERMSIG
#    define WTERMSIG(s) ((s) & 0177)
#  endif
#endif

/* Some ancient SysV systems don't define these */
#ifndef VMIN
#  define VMIN 4
#endif
#ifndef VTIME
#  define VTIME 5
#endif
#ifndef IUCLC
#  define IUCLC 0
#endif
#ifndef IXANY
#  define IXANY 0
#endif

/* Different names for the same beast. */
#ifndef TIOCMODG			/* BSD 4.3 */
#  ifdef TIOCMGET
#    define TIOCMODG TIOCMGET		/* Posix */
#  else
#    ifdef MCGETA
#      define TIOCMODG MCGETA		/* HP/UX */
#    endif
#  endif
#endif

#ifndef TIOCMODS
#  ifdef TIOCMSET
#    define TIOCMODS TIOCMSET
#  else
#    ifdef MCSETA
#      define TIOCMODS MCSETA
#    endif
#  endif
#endif

#ifndef TIOCM_CAR			/* BSD + Posix */
#  ifdef MDCD
#    define TIOCM_CAR MDCD		/* HP/UX */
#  endif
#endif

/* Define some thing that might not be there */
#ifndef TANDEM
#  define TANDEM 0
#endif
#ifndef BITS8
#  define BITS8 0
#endif
#ifndef PASS8
#  ifdef LLITOUT
#  define PASS8 LLITOUT
#  else
#  define PASS8 0
#  endif
#endif
#ifndef CRTSCTS
#  define CRTSCTS 0
#endif

/* If this is SysV without Posix, emulate Posix. */
#if defined(_SYSV)
#if !defined(_POSIX) || !defined(HAVE_TERMIOS_H)
#  define termios termio
#  ifndef TCSANOW
#    define TCSANOW 0
#  endif
#  define tcgetattr(fd, tty)        ioctl(fd, TCGETA, tty)
#  define tcsetattr(fd, flags, tty) ioctl(fd, TCSETA, tty)
#  define tcsendbreak(fd, len)      ioctl(fd, TCSBRK, 0)
#  define speed_t int
#  define cfsetispeed(xtty, xspd) \
		((xtty)->c_cflag = ((xtty)->c_cflag & ~CBAUD) | (xspd))
#  define cfsetospeed(tty, spd)
#endif
#endif

/* Redefine cfset{i,o}speed for Linux > 1.1.68 && libc < 4.5.21 */
#if defined (__GLIBC__) && defined(CBAUDEX)
#  undef cfsetispeed
#  undef cfsetospeed
#  define cfsetispeed(xtty, xspd) \
		((xtty)->c_cflag = ((xtty)->c_cflag & ~CBAUD) | (xspd))
#  define cfsetospeed(tty, spd)
#endif
