/*
 * port.h	General include file that includes all nessecary files
 *		for the configured system. This is a general file,
 *		not application dependant. It maybe a bit of overhead
 *		but simplifies porting greatly.
 *
 *		$Id: port.h,v 1.10 2009-06-06 18:50:42 al-guest Exp $
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
#ifdef ISC
#  include <sys/bsdtypes.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#  include <sys/param.h>
#endif
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#  include <stdlib.h>
#else
   char *getenv(void);
#endif

#ifdef HAVE_TERMCAP_H
#  include <termcap.h>
#endif
#ifdef HAVE_NCURSES_TERMCAP_H
#  include <ncurses/termcap.h>
#endif

#ifdef _UWIN2P0
# include <time.h>
# define MAXNAMLEN 80
#endif
#include <signal.h>
#include <fcntl.h>
#include <setjmp.h>
#include <sys/stat.h>
#ifdef _MINIX
#include <sys/times.h>
#else
#include <time.h>
#include <sys/time.h>
#endif
#include <string.h>
#include <stdio.h>
#include <pwd.h>
#include <ctype.h>
#include <errno.h>
#if defined(_BSD43) || defined(_SYSV) || (defined(BSD) && (BSD >= 199103))
#  define NOSTREAMS
#  include <sys/file.h>
#endif

#ifndef _NSIG
#  ifndef NSIG
#    define _NSIG 31
#  else
#    define _NSIG NSIG
#  endif
#endif

/* Enable music routines. Could we use defined(i386) here? */
#if defined(__linux__) || defined(_SCO)
# define VC_MUSIC 1
#endif

extern char **environ;
