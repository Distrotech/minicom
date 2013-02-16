/*
 * vt100.h	Header file for the vt100 emulator.
 *
 *		$Id: vt100.h,v 1.4 2007-10-10 20:18:20 al-guest Exp $
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
#ifndef __MINICOM__SRC__VT100_H__
#define __MINICOM__SRC__VT100_H__
#include <stdio.h>

/* Keypad and cursor key modes. */
#define NORMAL	1
#define APPL	2

/* Don't change - hardcoded in minicom's dial.c */
#define VT100	1
#define ANSI	3

extern int vt_nl_delay;		/* Delay after CR key */
extern int vt_ch_delay;		/* Delay after each character */

/* Prototypes from vt100.c */
void vt_install(void(*)(const char *, int), void (*)(int, int), WIN *);
void vt_init(int, int, int, int, int, int);
void vt_pinit(WIN *, int, int);
void vt_set(int, int, int, int, int, int, int, int, int);
void vt_out(int);
void vt_send(int ch);

#endif /* ! __MINICOM__SRC__VT100_H__ */
