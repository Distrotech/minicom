/*
 * keyboard.h	Constants to talk to the keyboard driver.
 *
 * 		$Id: keyboard.h,v 1.3 2008-08-02 20:27:07 al-guest Exp $
 *
 *		This file is part of the minicom communications package,
 *		Copyright 1991-1995 Miquel van Smoorenburg.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#define KSTOP		1
#define KKPST		2
#define KVT100		3
#define KANSI		4
#define KMINIX		5
#define KSTART		6
#define KKPAPP		7
#define KCURST		8
#define KCURAPP		9
#define KSIGIO		10
#define KSETESC		11
#define KSETBS		12
#define KGETKEY		13

#define HELLO		SIGUSR1
#define ACK		SIGUSR2

#define KINSTALL	100
#define KUNINSTALL	101
