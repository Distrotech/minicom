/*
 * configsym.h	- Offsets into the mpars structure
 *		  When the mpars structure is changed,
 *		  change these define's too.
 *
 *		$Id: configsym.h,v 1.6 2007-10-10 20:18:20 al-guest Exp $
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
 *	When adding parameter macros here, remember to also update
 *	their default values in the pars structure mpars in rwconf.c.
 *
 * fmg 1/11/94 colors
 * fmg 2/15/94 macros
 * jl  04.09.97 conversion file
 * jl  22.02.98 setting for filename selection window
 * jseymour@jimsun.LinxNet.com (Jim Seymour) 03/26/98 - Added define for
 *    length of "struct pars" "value" member so it could be referenced
 *    elsewhere.
 * jl  05.04.98 added parameter P_MUL
 * jl  06.07.98 added parameter P_CONVCAP
 * jl  28.11.98 added parameter P_SHOWSPD
 * jl  05.04.99 logging options P_LOGFNAME, P_LOGCONN and P_LOGXFER
 * er  18-Apr.99 added parameter P_MULTILINE
 * jl  10.02.2000 parameter P_STOPB
 */

#define PARS_VAL_LEN 128

struct pars {
  /* value is first, so that (char *)mpars[0] == mpars[0].value */
  /* Try doing this in PASCAL !! :-) */
  char value[PARS_VAL_LEN];
  int flags;
  const char *desc;
};
extern struct pars mpars[];

/* fmg 2/20/94 macros - Length of Macros */

#ifndef MAC_LEN
#define MAC_LEN 257
#endif

struct macs {
  char value[MAC_LEN];
  int flags;
  const char *desc;
};
extern struct macs mmacs[];

enum config_type {
  CONFIG_GLOBAL,
  CONFIG_PERSONAL,
};

#define ADM_CHANGE	1
#define USR_CHANGE	2
#define CHANGED		(ADM_CHANGE | USR_CHANGE)

#define PROTO_BASE	0
#define MAXPROTO	12
#define PROG_BASE	12

#define P_PNN(n)	(mpars[PROTO_BASE + n].value[0])
#define P_PUD(n)	(mpars[PROTO_BASE + n].value[1])
#define P_PFULL(n)	(mpars[PROTO_BASE + n].value[2])
#define P_PIORED(n)	(mpars[PROTO_BASE + n].value[3])
#define P_MUL(n)	(mpars[PROTO_BASE + n].value[4])
#define P_PNAME(n)	(&mpars[PROTO_BASE + n].value[5])
#define P_PPROG(n)	mpars[PROG_BASE + n].value

#define P_PORT		mpars[24].value
#define P_CALLIN	mpars[25].value
#define P_CALLOUT	mpars[26].value
#define P_LOCK		mpars[27].value
#define P_BAUDRATE	mpars[28].value
#define P_BITS		mpars[29].value
#define P_PARITY	mpars[30].value
#define P_STOPB		mpars[31].value
#define P_KERMIT	mpars[32].value
#define P_KERMALLOW	mpars[33].value
#define P_KERMREAL	mpars[34].value
#define P_COLUSAGE	mpars[35].value
#define P_SCRIPTPROG	mpars[36].value
/* The next entries must be kept in order */
#define P_MINIT		mpars[37].value
#define P_MRESET	mpars[38].value
#define P_MDIALPRE	mpars[39].value
#define P_MDIALSUF	mpars[40].value
#define P_MDIALPRE2	mpars[41].value
#define P_MDIALSUF2	mpars[42].value
#define P_MDIALPRE3	mpars[43].value
#define P_MDIALSUF3	mpars[44].value
#define P_MCONNECT	mpars[45].value
#define P_MNOCON1	mpars[46].value
#define P_MNOCON2	mpars[47].value
#define P_MNOCON3	mpars[48].value
#define P_MNOCON4	mpars[49].value
#define P_MHANGUP	mpars[50].value
#define P_MDIALCAN	mpars[51].value
#define P_MDIALTIME	mpars[52].value
#define P_MRDELAY	mpars[53].value
#define P_MRETRIES	mpars[54].value
/* Yup, until here. */
#define P_MDROPDTR	mpars[55].value
#define P_MAUTOBAUD	mpars[56].value
#define P_SHOWSPD	mpars[57].value
#define P_UPDIR		mpars[58].value
#define P_DOWNDIR	mpars[59].value
#define P_SCRIPTDIR	mpars[60].value
#define P_ESCAPE	mpars[61].value
#define P_BACKSPACE	mpars[62].value
#define P_STATLINE	mpars[63].value
#define P_HASDCD	mpars[64].value
#define P_HASRTS	mpars[65].value
#define P_HASXON	mpars[66].value
#define P_PAUTO		mpars[67].value

/* fmg colors - these are used in signaling when values have changed
                so that the preferences saving function knows what to save */

#define P_MFG           mpars[68].value
#define P_MBG           mpars[69].value
#define P_TFG           mpars[70].value
#define P_TBG           mpars[71].value
#define P_SFG           mpars[72].value
#define P_SBG           mpars[73].value

/* fmg  macros file name & entry used to signal when macros need to be saved */

#define P_MACROS        mpars[74].value  /* macros save filename */
#define P_MACCHG        mpars[75].value  /* macros changed flag */
#define P_MACENAB	mpars[76].value	 /* macros enabled flag */

#define P_SOUND		mpars[77].value
#define P_HISTSIZE      mpars[78].value  /* History buffer size */

#define P_CONVF		mpars[79].value  /* Char.conversion table */
#define P_CONVCAP	mpars[80].value  /* Use conversion on capture file */

#define P_FSELW		mpars[81].value  /* Filename selection window */
#define P_ASKDNDIR	mpars[82].value  /* Ask dir. for downloads or not */

/* jl 4.1999 logfile options */
#define P_LOGFNAME	mpars[83].value  /* Filename for the logfile */
#define P_LOGCONN	mpars[84].value  /* Log connects and hangups */
#define P_LOGXFER	mpars[85].value  /* Log file transfers */

#define P_MULTILINE	mpars[86].value  /* Multi-node untag  er 18-Apr-99 */

/* Terminal behaviour */
#define P_LOCALECHO	mpars[87].value
#define P_ADDLINEFEED	mpars[88].value
#define P_LINEWRAP      mpars[89].value  /* Line wrap */
#define P_DISPLAYHEX    mpars[90].value  /* Do output as hex */
#define P_ADDCARRIAGERETURN mpars[91].value

#define P_ANSWERBACK    mpars[92].value  /* User defined answerback string */

/* fmg - macros struct */

#define P_MAC1          mmacs[0].value
#define P_MAC2          mmacs[1].value
#define P_MAC3          mmacs[2].value
#define P_MAC4          mmacs[3].value
#define P_MAC5          mmacs[4].value
#define P_MAC6          mmacs[5].value
#define P_MAC7          mmacs[6].value
#define P_MAC8          mmacs[7].value
#define P_MAC9          mmacs[8].value
#define P_MAC10          mmacs[9].value

