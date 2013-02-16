/*
 * dial.c	Functions to dial, retry etc. Als contains the dialing
 *		directory code, _and_ the famous tu-di-di music.
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
 *   jl 22.06.97  Logging connects and disconnects.
 *   jl 23.06.97  Adjustable DTR droptime
 *   jl 21.09.97  Conversion table filenames in dialdir
 *   jl 05.10.97  Line speed changed to long in dial()
 *   jl 26.01.98  last login date & time added to dialing window
 *   jl 16.04.98  start searching for dialing tags from the highlighted entry
 *   jl 12.06.98  save the old dialdir if it was an old version
 *   er 18-Apr-99 When calling a multiline BBS
 *		  tagged entries with same name are untagged
 *   jl 01.09.99  Move entry up/down in directory
 *   jl 10.02.2000 Stopbits field added
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdint.h>
#include <limits.h>
#include <arpa/inet.h>

#include "port.h"
#include "minicom.h"
#include "intl.h"

#ifdef VC_MUSIC
#  if defined(__GLIBC__)
#    include <sys/ioctl.h>
#    include <sys/kd.h>
#    include <sys/time.h>
#  endif
#endif

enum { CURRENT_VERSION = 6 };

/* Dialing directory. */
struct v1_dialent {
  char name[32];
  char number[16];
  char script[16];
  char username[32];
  char password[32];
  char term;
  char baud[8];
  char parity[2];
  char dialtype;
  char flags; /* Localecho in v0 */
  char bits[2];
  struct dialent *next;
};

struct v3_dialent {
  char name[32];
  char number[32];
  char script[32];
  char username[32];
  char password[32];
  char term;
  char baud[8];
  char parity[2];
  char dialtype;
  char flags;
  char bits[2];
  struct dialent *next;
};

struct v4_dialent {
  char name[32];
  char number[32];
  char script[32];
  char username[32];
  char password[32];
  char term;
  char baud[8];
  char parity[2];
  char dialtype;
  char flags;
  char bits[2];
  char lastdate[9];	/* jl 22.06.97 */
  char lasttime[9];	/* jl 22.06.97 */
  int  count;		/* jl 22.06.97 */
  char convfile[16];	/* jl 21.09.97 */
  char stopb[2];	/* jl 10.02.2000 */
  struct dialent *next;
};

/* v5 is a packed version of v4 so that there's no difference between 32 and
 * 64 bit versions as well as LE and BE.
 */
struct dialent {
  char     name[32];
  char     number[32];
  char     script[32];
  char     username[32];
  char     password[32];
  char     term;
  char     baud[8];
  char     parity[2];
  char     dialtype;
  char     flags;
  char     bits[2];
  char     lastdate[9];
  char     lasttime[9];
  uint32_t count;
  char     convfile[16];
  char     stopb[2];
  struct dialent *next;
} __attribute__((packed));

/* Version info. */
#define DIALMAGIC 0x55AA
struct dver {
  short magic;
  short version;
  unsigned short size;
  short res1;
  short res2;
  short res3;
  short res4;
} __attribute__((packed));

/* Forward declaration */
static void writedialdir(void);

#define dialentno(di, no) ((struct dialent *)((char *)(di) + ((no) * sizeof(struct dialent))))  

static struct dialent *dialents;
static struct dialent *d_man;
static int nrents = 1;
static int newtype;
/* Access to ".dialdir" denied? */
static int dendd = 0;
static char *tagged;
char *dial_user;
char *dial_pass;

/* Change the baud rate.  Treat all characters in the given array as if
 * they were key presses within the comm parameters dialog (C-A P) and
 * change the line speed accordingly.  Terminate when a space or other
 * unrecognised character is found.
 */
const char* change_baud(const char *s)
{
  while (s && *s
         && update_bbp_from_char(*s, P_BAUDRATE, P_BITS, P_PARITY, P_STOPB, 0))
    ++s;

  // reinitialise the port and update the status line
  if (portfd >= 0)
    port_init();
  if (st)
    mode_status();

  return s;
}

/*
 * Functions to talk to the modem.
 */

/*
 * Send a string to the modem.
 * If how == 0, '~'  sleeps 1 second.
 * If how == 1, "^~" sleeps 1 second.
 */
void mputs(const char *s, int how)
{
  char c;

  while (*s) {
    if (*s == '^' && (*(s + 1))) {
      s++;
      if (*s == '^')
        c = *s;
      else if (how == 1 && *s == '~') {
        sleep(1);
        s++;
        continue;
      } else
        c = (*s) & 31;
    } else if (*s == '\\' && (*(s + 1))) {
      s++;
      switch (toupper (*s)) {
        case '\\':
          c = *s;
          break;
        case 'U':
          if (dial_user && *dial_user)
            mputs (dial_user, how);
          s++;
          continue;
        case 'P':
          if (dial_pass && *dial_pass)
            mputs (dial_pass, how);
          s++;
          continue;
        case 'B': /* line speed change. */
          s = change_baud(++s);
          continue;
        case 'L': /* toggle linefeed addition */
          toggle_addlf();
          s++; /* nothing to do with the modem, move along */
          continue;
        case 'E': /* toggle local echo */
          toggle_local_echo();
          s++; /* again, move along. */
          continue;
	case 'G': /* run a script */
	  runscript(0, s + 1, "", "");
	  return;
        default:
          s++;
          continue;
      }
    } else
      c = *s;
    if (how == 0 && c == '~')
      sleep(1);
    else
      if (write(portfd, &c, 1) != 1)
        break;
    s++;
  }
}

/*
 * Initialize the modem.
 */
void modeminit(void)
{
  WIN *w;

  if (P_MINIT[0] == '\0')
    return;

  w = mc_tell(_("Initializing Modem"));
  m_dtrtoggle(portfd, 1);         /* jl 23.06.97 */
  mputs(P_MINIT, 0);
  mc_wclose(w, 1);
}

/*
 * Reset the modem.
 */
void modemreset(void)
{
  WIN *w;

  if (P_MRESET[0] == '\0')
    return;

  w = mc_tell(_("Resetting Modem"));
  mputs(P_MRESET, 0);
  sleep(1);
  mc_wclose(w, 1);
}

/*
 * Hang the line up.
 */
void hangup(void)
{
  WIN *w;
  int sec=1;

  w = mc_tell(_("Hanging up"));

  timer_update();
  if (P_LOGCONN[0] == 'Y')
    do_log(_("Hangup (%ld:%02ld:%02ld)"),
           online / 3600, (online / 60) % 60, online>0 ? online % 60 : 0);
  online = -1;
  old_online = -1;

  if (isdigit(P_MDROPDTR[0]))
    sscanf(P_MDROPDTR,"%2d",&sec);

  if (P_MDROPDTR[0] == 'Y' || (isdigit(P_MDROPDTR[0]) && sec>0)) {
    m_dtrtoggle(portfd, sec);   /* jl 23.06.97 */
  } else {
    mputs(P_MHANGUP, 0);
    sleep(1);
  }
#ifdef _DCDFLOW
  /* DCD has dropped, turn off hw flow control. */
  m_sethwf(portfd, 0);
#endif
  /* If we don't have DCD support fake DCD dropped */
  bogus_dcd = 0;
  mc_wclose(w, 1);
  time_status(false);
}

/*
 * This seemed to fit best in this file
 * Send a break signal.
 */
void sendbreak()
{
  WIN *w;

  w = mc_tell(_("Sending BREAK"));
  mc_wcursor(w, CNONE);

  m_break(portfd);
  mc_wclose(w, 1);
}

WIN *dialwin;
int dialtime;

#ifdef VC_MUSIC

/*
 * Play music until key is pressed.
 */
void music(void)
{
  int x, i, k;
  int consolefd = 0;
  char *disp;

  /* If we're in X, we have to explicitly use the console */
  if (strncmp(getenv("TERM"), "xterm", 5) == 0 &&
      (disp = getenv("DISPLAY")) != NULL &&
      (strcmp(disp, ":0.0") == 0 ||
       (strcmp(disp, ":0") == 0))) {
    consolefd = open("/dev/console", O_WRONLY);
    if (consolefd < 0) consolefd = 0;
  }

  /* Tell keyboard handler what we want. */
  keyboard(KSIGIO, 0);

  /* And loop forever :-) */
  for(i = 0; i < 9; i++) {
    k = 2000 - 200 * (i % 3);
    ioctl(consolefd, KIOCSOUND, k);

    /* Check keypress with timeout 160 ms */
    x = check_io(-1, 0, 160, NULL, 0, NULL);
    if (x & 2)
      break;
  }
  ioctl(consolefd, KIOCSOUND, 0);
  if (consolefd)
    close(consolefd);

  /* Wait for keypress and absorb it */
  while ((x & 2) == 0) {
    x = check_io(-1, 0, 10000, NULL, 0, NULL);
    timer_update();
  }
  keyboard(KGETKEY, 0);
}
#endif

/*
 * The dial has failed. Tell user.
 * Count down until retrytime and return.
 */
static int dialfailed(char *s, int rtime)
{
  int f, x;
  int ret = 0;

  mc_wlocate(dialwin, 1, 5);
  mc_wprintf(dialwin, _("    No connection: %s.      \n"), s);
  if (rtime < 0) {
    mc_wprintf(dialwin, _("   Press any key to continue..    "));
    if (check_io(-1, 0, 10000, NULL, 0, NULL) & 2)
      keyboard(KGETKEY, 0);
    return 0;
  }
  mc_wprintf(dialwin, _("     Retry in %2d seconds             "), rtime);

  for (f = rtime - 1; f >= 0; f--) {
    x = check_io(-1, 0, 1000, NULL, 0, NULL);
    if (x & 2) {
      /* Key pressed - absorb it. */
      x = keyboard(KGETKEY, 0);
      if (x != ' ')
        ret = -1;
      break;
    }
    mc_wlocate(dialwin, 0, 6);
    mc_wprintf(dialwin, _("     Retry in %2d seconds             "), f);
  }
#ifdef HAVE_USLEEP
  /* MARK updated 02/17/94 - Min dial delay set to 0.35 sec instead of 1 sec */
  if (f < 0) /* Allow modem time to hangup if redial time == 0 */
    usleep(350000);
#else
  if (f < 0)
    sleep(1);
#endif
  mc_wlocate(dialwin, 1, 5);
  mc_wprintf(dialwin, "                                       \n");
  mc_wprintf(dialwin, "                                             ");
  return ret;
}

/*
 * Dial a number, and display the name.
 */
long dial(struct dialent *d, struct dialent **d2)
{
  char *s = 0, *t = 0;
  int f, x = 0;
  int modidx, retries = 0;
  int maxretries = 1, rdelay = 45;
  long nb, retst = -1;
  char *reason = _("Max retries");
  time_t now, last;
  struct tm *ptime;
  char buf[128];
  char modbuf[128];
  /*  char logline[128]; */

  timer_update(); /* Statusline may still show 'Online' / jl 16.08.97 */
  /* don't do anything if already online! jl 07.07.98 */
  if (P_HASDCD[0]=='Y' && online >= 0) {
    werror(_("You are already online! Hang up first."));
    return(retst);
  }

  dialwin = mc_wopen(18, 9, 62, 16, BSINGLE, stdattr, mfcolor, mbcolor, 0, 0, 1);
  mc_wtitle(dialwin, TMID, _("Autodial"));
  mc_wcursor(dialwin, CNONE);

  mc_wputs(dialwin, "\n");
  mc_wprintf(dialwin, " %s : %s\n", _("Dialing"), d->name);
  mc_wprintf(dialwin, _("      At : %s"), d->number);
  mc_wprintf(dialwin, "\n"); /* help translators */
  if (d->lastdate[0] && d->lasttime[0])		/* jl 26.01.98 */
    mc_wprintf(dialwin, _(" Last on : %s at %s"), d->lastdate, d->lasttime);
  mc_wprintf(dialwin, "\n");
  mc_wredraw(dialwin, 1);

  /* Tell keyboard routines we need them. */
  keyboard(KSIGIO, 0);

  maxretries = atoi(P_MRETRIES);
  if (maxretries <= 0)
    maxretries = 1;
  rdelay = atoi(P_MRDELAY);
  if (rdelay < 0)
    rdelay = 0;

  /* Main retry loop of dial() */
MainLoop:
  while (++retries <= maxretries) {

    /* See if we need to try the next tagged entry. */
    if (retries > 1 && (d->flags & FL_TAG)) {
      do {
        d = d->next;
        if (d == (struct dialent *)NULL)
          d = dialents;
      } while (!(d->flags & FL_TAG));
      mc_wlocate(dialwin, 0, 1);
      mc_wprintf(dialwin, " %s : %s", _("Dialing"), d->name);
      mc_wclreol(dialwin);
      mc_wprintf(dialwin, "\n"); /* helps translators */
      mc_wprintf(dialwin, _("      At : %s"), d->number);
      mc_wclreol(dialwin);
      if (d->lastdate[0] && d->lasttime[0]) {
	mc_wprintf(dialwin, "\n"); /* don't merge with next printf, helps translators */
        mc_wprintf(dialwin, _(" Last on : %s at %s"),
                d->lastdate, d->lasttime);
        mc_wclreol(dialwin);
      }
    }

    /* Calculate dial time */
    dialtime = atoi(P_MDIALTIME);
    if (dialtime == 0)
      dialtime = 45;
    time(&now);
    last = now;

    /* Show used time */
    mc_wlocate(dialwin, 0, 4);
    mc_wprintf(dialwin, _("    Time : %-3d"), dialtime);
    if (maxretries > 1)
      mc_wprintf(dialwin, _("     Attempt #%d"), retries);
    mc_wputs(dialwin, _("\n\n\n Escape to cancel, space to retry."));

    /* Start the dial */
    m_flush(portfd);
    switch (d->dialtype) {
      case 0:
        mputs(P_MDIALPRE, 0);
        mputs(d->number, 0);
        mputs(P_MDIALSUF, 0);
        break;
      case 1:
        mputs(P_MDIALPRE2, 0);
        mputs(d->number, 0);
        mputs(P_MDIALSUF2, 0);
        break;
      case 2:
        mputs(P_MDIALPRE3, 0);
        mputs(d->number, 0);
        mputs(P_MDIALSUF3, 0);
        break;
    }

    /* Wait 'till the modem says something */
    modbuf[0] = 0;
    modidx = 0;
    s = buf;
    buf[0] = 0;
    while (dialtime > 0) {
      if (*s == 0) {
        x = check_io(portfd_connected, 0, 1000, buf, sizeof(buf), NULL);
        s = buf;
      }
      if (x & 2) {
        f = keyboard(KGETKEY, 0);
        /* Cancel if escape was pressed. */
        if (f == K_ESC)
          mputs(P_MDIALCAN, 0);

        /* On space retry. */
        if (f == ' ') {
          mputs(P_MDIALCAN, 0);
          dialfailed(_("Cancelled"), 4);
          m_flush(portfd);
          break;
        }
        keyboard(KSTOP, 0);
        mc_wclose(dialwin, 1);
        return retst;
      }
      if (x & 1) {
        /* Data available from the modem. Put in buffer. */
        if (*s == '\r' || *s == '\n') {
          /* We look for [\r\n]STRING[\r\n] */
          modbuf[modidx] = 0;
          modidx = 0;
        } else if (modidx < 127) {
          /* Normal character. Add. */
          modbuf[modidx++] = *s;
          modbuf[modidx] = 0;
        }
        /* Skip to next received char */
        if (*s)
          s++;
        /* Only look when we got a whole line. */
        if (modidx == 0 &&
            !strncmp(modbuf, P_MCONNECT, strlen(P_MCONNECT))) {
          timer_update(); /* the login scipt may take long.. */
          retst = 0;
          /* Try to do auto-bauding */
          if (sscanf(modbuf + strlen(P_MCONNECT), "%ld", &nb) == 1)
            retst = nb;
          linespd = retst;

          /* Try to figure out if this system supports DCD */
          f = m_getdcd(portfd);
          bogus_dcd = 1;

          /* jl 22.05.97, 22.09.97, 05.04.99 */
          if (P_LOGCONN[0] == 'Y')
            do_log("%s %s, %s",modbuf, d->name, d->number);

          ptime = localtime(&now);
          sprintf(d->lastdate,"%4.4d%2.2d%2.2d",
                  (ptime->tm_year)+1900,(ptime->tm_mon)+1,
                  ptime->tm_mday);
          sprintf(d->lasttime,"%02d:%02d",
                  ptime->tm_hour,ptime->tm_min);
          d->count++;

          if (d->convfile[0]) {
            loadconv(d->convfile);    /* jl 21.09.97 */
            strcpy(P_CONVF, d->convfile);
          }

          mc_wlocate(dialwin, 1, 7);
          if (d->script[0] == 0) {
            mc_wputs(dialwin,
                  _("Connected. Press any key to continue"));
#ifdef VC_MUSIC
            if (P_SOUND[0] == 'Y')
              music();
            else {
              x = check_io(-1, 0, 0, NULL, 0, NULL);
              if ((x & 2) == 2)
                keyboard(KGETKEY, 0);
            }
#else
            /* MARK updated 02/17/94 - If VC_MUSIC is not */
            /* defined, then at least make some beeps! */
            if (P_SOUND[0] == 'Y')
              mc_wputs(dialwin,"\007\007\007");
#endif
            x = check_io(-1, 0, 0, NULL, 0, NULL);
            if ((x & 2) == 2)
              keyboard(KGETKEY, 0);
          }
          keyboard(KSTOP, 0);
          mc_wclose(dialwin, 1);
          /* Print out the connect strings. */
          mc_wprintf(us, "\r\n%s\r\n", modbuf);
          dialwin = NULL;

          /* Un-tag this entry. */
          d->flags &= ~FL_TAG;

          /* store pointer to the entry that ANSWERED */
          if (d2 != (struct dialent**)NULL)
            *d2 = d;	/* jl 23.09.97 */

          /* Here should placed code to untag phones with similar names */
          if (P_MULTILINE[0] == 'Y') {
            struct dialent *d3 = dialents;

            while (d3 != (struct dialent*)NULL) {
              if (!strcmp(d3->name, d->name))
                d3->flags &= ~FL_TAG;
              d3 = d3->next;
            }
          }				/*  er 27-Apr-99 */
          return retst;
        }

        for (f = 0; f < 3; f++) {
          if (f == 0)
            t = P_MNOCON1;
          if (f == 1)
            t = P_MNOCON2;
          if (f == 2)
            t = P_MNOCON3;
          if (f == 3)
            t = P_MNOCON4;
          if ((*t) && (!strncmp(modbuf, t, strlen(t)))) {
            if (retries < maxretries) {
              x = dialfailed(t, rdelay);
              if (x < 0) {
                keyboard(KSTOP, 0);
                mc_wclose(dialwin, 1);
                return retst;
              }
            }
            if (maxretries == 1)
              reason = t;
            goto MainLoop;
          }
        }
      }

      /* Do timer routines here. */
      time(&now);
      if (last != now) {
        dialtime -= (now - last);
        if (dialtime < 0)
          dialtime = 0;
        mc_wlocate(dialwin, 11, 4);
        mc_wprintf(dialwin, "%-3d  ", dialtime);
        if (dialtime <= 0) {
          mputs(P_MDIALCAN, 0);
          reason = _("Timeout");
          retst = -1;
          if (retries < maxretries) {
            x = dialfailed(reason, rdelay);
            if (x < 0) {
              keyboard(KSTOP, 0);
              mc_wclose(dialwin, 1);
              return retst;
            }
          }
        }
      }
      last = now;
    }
  } /* End of main while cq MainLoop */
  dialfailed(reason, -1);
  keyboard(KSTOP, 0);
  mc_wclose(dialwin, 1);
  return retst;
}

/*
 * Create an empty entry.
 */
static struct dialent *mkstdent(void)
{
  struct dialent *d = malloc(sizeof(struct dialent));

  if (d == NULL)
    return d;

  d->name[0] = 0;
  d->number[0] = 0;
  d->script[0] = 0;
  d->username[0] = 0;
  d->password[0] = 0;
  d->term = 1;
  d->dialtype = 0;
  d->flags = FL_DEL;
  strcpy(d->baud, "Curr");
  strcpy(d->bits, "8");
  strcpy(d->parity, "N");
  d->lastdate[0] = 0;	/* jl 22.06.97 */
  d->lasttime[0] = 0;
  d->count = 0;
  d->convfile[0] = 0;	/* jl 21.09.97 */
  strcpy(d->stopb, "1");
  d->next = NULL;

  return d;
}

static void convert_to_save_order(struct dialent *dst, const struct dialent *src)
{
  memcpy(dst, src, sizeof(*dst));
  dst->count = htonl(dst->count);
}

static void convert_to_host_order(struct dialent *dst, const struct dialent *src)
{
  memcpy(dst, src, sizeof(*dst));
  dst->count = ntohl(dst->count);
}

/* Read version 5 of the dialing directory. */
static int v5_read(FILE *fp, struct dialent *d)
{
  struct dialent dent_n;
  if (fread(&dent_n, sizeof(dent_n), 1, fp) != 1)
    return 1;
  convert_to_host_order(d, &dent_n);
  return 0;
}

static int v6_read(FILE *fp, struct dialent *d)
{
  struct dialent dent_n;
  if (fread(&dent_n, sizeof(dent_n) - sizeof(void *), 1, fp) != 1)
    return 1;
  convert_to_host_order(d, &dent_n);
  return 0;
}

/* Read version 4 of the dialing directory. */
static int v4_read(FILE *fp, struct dialent *d, struct dver *dv)
{
  struct v4_dialent v4;

  if (fread(&v4, dv->size, 1, fp) != 1)
    return 1;

  if (dv->size < sizeof(struct dialent)) {
    if (dv->size < offsetof(struct dialent, count) + sizeof(struct dialent *)) {
      d->count = 0;
      d->lasttime[0] = 0;
      d->lastdate[0] = 0;
    }
    if (dv->size < offsetof(struct dialent, stopb) + sizeof(struct dialent *))
      d->convfile[0] = 0;
    strcpy(d->stopb, "1");
  }

  return 0;
}

/* Read version 3 of the dialing directory. */
static int v3_read(FILE *fp, struct dialent *d)
{
  struct v3_dialent v3;   /* jl 22.06.97 */

  if (fread(&v3, sizeof(v3), 1, fp) != 1)
    return 1;

  memcpy(d, &v3, offsetof(struct v3_dialent, next));

  d->lastdate[0] = 0;
  d->lasttime[0] = 0;
  d->count = 0;
  d->convfile[0] = 0;
  strcpy(d->stopb, "1");

  return 0;
}

/* Read version 2 of the dialing directory. */
static int v2_read(FILE *fp, struct dialent *d)
{
  struct v3_dialent v3;   /* jl 22.06.97 */

  if (fread((char *)&v3, sizeof(v3), 1, fp) != 1)
    return 1;

  memcpy(d, &v3, offsetof(struct v3_dialent, next));
  if (d->flags & FL_ANSI)
    d->flags |= FL_WRAP;
  d->lastdate[0] = 0;
  d->lasttime[0] = 0;
  d->count = 0;
  d->convfile[0] = 0;
  strcpy(d->stopb, "1");

  return 0;
}

/* Read version 1 of the dialing directory. */
static int v1_read(FILE *fp, struct dialent *d)
{
  struct v1_dialent v1;

  if (fread((char *)&v1, sizeof(v1), (size_t)1, fp) != 1)
    return 1;

  memcpy(d->username, v1.username, sizeof(v1) - offsetof(struct v1_dialent, username));
  strncpy(d->name, v1.name, sizeof(d->name));
  strncpy(d->number, v1.number, sizeof(d->number));
  strncpy(d->script, v1.script, sizeof(d->script));
  d->lastdate[0]=0;
  d->lasttime[0]=0;
  d->count=0;
  d->convfile[0]=0;
  strcpy(d->stopb, "1");

  return 0;
}

/* Read version 0 of the dialing directory. */
static int v0_read(FILE *fp, struct dialent *d)
{
  if (v1_read(fp, d))
    return 1;
  d->dialtype = 0;
  d->flags = 0;

  return 0;
}

/*
 * Read in the dialing directory from $HOME/.dialdir
 */
int readdialdir(void)
{
  long size;
  FILE *fp;
  char dfile[256];
  char copycmd[512];
  static int didread = 0;
  int f;
  struct dialent *d = NULL, *tail = NULL;
  struct dver dial_ver;
  WIN *w;

  if (didread)
    return 0;
  didread = 1;
  nrents = 1;
  tagged = malloc(1);
  if (!tagged)
    return 0;
  tagged[0] = 0;

  /* Make the manual dial entry. */
  d_man = mkstdent();
  strncpy(d_man->name, _("Manually entered number"), sizeof(d_man->name));
  d_man->name[sizeof(d_man->name) - 1] = 0;

  /* Construct path */
  snprintf(dfile, sizeof(dfile), "%s/.dialdir", homedir);

  /* Try to open ~/.dialdir */
  if ((fp = fopen(dfile, "r")) == NULL) {
    if (errno == EPERM) {
      werror(_("Cannot open ~/.dialdir: permission denied"));
      dialents = mkstdent();
      dendd = 1;
      return 0;
    }
    dialents = mkstdent();
    return 0;
  }

  /* Get size of the file */
  fseek(fp, 0L, SEEK_END);
  size = ftell(fp);
  if (size == 0) {
    dialents = mkstdent();
    fclose(fp);
    return 0;
  }

  /* Get version of the dialing directory */
  fseek(fp, 0L, SEEK_SET);
  if (fread(&dial_ver, sizeof(dial_ver), 1, fp) != 1)
    {
      werror(_("Failed to read dialing directory\n"));
      return -1;
    }
  if (dial_ver.magic != DIALMAGIC) {
    /* First version without version info. */
    dial_ver.version = 0;
    fseek(fp, 0L, SEEK_SET);
  } else {
    dial_ver.size = ntohs(dial_ver.size);
    size -= sizeof(dial_ver);
  }

  /* See if the size of the file is allright. */
  switch (dial_ver.version) {
    case 0:
    case 1:
      dial_ver.size = sizeof(struct v1_dialent);
      break;
    case 2:
    case 3:
      dial_ver.size = sizeof(struct v3_dialent);
      break;
    case 4:
      /* dial_ver.size = sizeof(struct dialent); */
      /* Removed the forced setting to add flexibility.
       * Now you don't need to change the version number
       * if you just add fields to the end of the dialent structure
       * before the *next pointer and don't change existing fields.
       * Just update the initialization in the functions
       * v4_read/v5_read and mkstdent (and whatever you added the field for)
       * jl 21.09.97
       */
      if (dial_ver.size < 200 ||
          dial_ver.size > sizeof(struct v4_dialent)) {
        werror(_("Phonelist garbled (unknown version?)"));
        dialents = mkstdent();
        return -1;
      }
      break;
    case 5:
      if (dial_ver.size != sizeof(struct dialent)) {
	werror(_("Phonelist corrupted"));
	return -1;
      }
      break;
    case 6:
      // v6 is the same as v5 but the pointer is not saved and thus does not
      // have different size on 32 and 64bit systems
      if (dial_ver.size != sizeof(struct dialent) - sizeof(void *)) {
        werror(_("Phonelist corrupted"));
        return -1;
      }
      break;
    default:
      werror(_("Unknown dialing directory version"));
      dendd = 1;
      dialents = mkstdent();
      return -1;
  }

  if (size % dial_ver.size != 0) {
    werror(_("Phonelist garbled (?)"));
    fclose(fp);
    dendd = 1;
    dialents = mkstdent();
    return -1;
  }

  /* Read in the dialing entries */
  nrents = size / dial_ver.size;
  if (nrents == 0) {
    dialents = mkstdent();
    nrents = 1;
    fclose(fp);
    return 0;
  }
  for(f = 1; f <= nrents; f++) {
    if ((d = malloc(sizeof (struct dialent))) == NULL) {
      if (f == 1)
        dialents = mkstdent();
      else
        tail->next = NULL;

      werror(_("Out of memory while reading dialing directory"));
      fclose(fp);
      return -1;
    }
    int r = 0;
    switch (dial_ver.version) {
      case 0:
        r = v0_read(fp, d);
        break;
      case 1:
        r = v1_read(fp, d);
        break;
      case 2:
        r = v2_read(fp, d);
        break;
      case 3:
        r = v3_read(fp, d);
        break;
      case 4:
        r = v4_read(fp, d, &dial_ver);
        break;
      case 5:
	r = v5_read(fp, d);
	break;
      case 6:
	r = v6_read(fp, d);
	break;
    }

    if (r)
      werror("Failed to read phonelist\n");

    /* MINIX terminal type is obsolete */
    if (d->term == 2)
      d->term = 1;

    if (tail)
      tail->next = d;
    else
      dialents = d;
    tail = d;
  }
  d->next = NULL;
  fclose(fp);

  if (dial_ver.version != CURRENT_VERSION) {
    if (snprintf(copycmd, sizeof(copycmd),
                 "cp -p %s %s.v%hd", dfile, dfile, dial_ver.version) > 0) {
      if (P_LOGFNAME[0] != 0)
        do_log("%s", copycmd);
      if (system(copycmd) == 0) {
        snprintf(copycmd, sizeof(copycmd),
                 _("Converted dialdir to new format, old saved as %s.v%hd"),
                 dfile, dial_ver.version);
        w = mc_tell("%s", copycmd);
        if (w) {
          sleep(2);
          mc_wclose(w,1);
        }
        writedialdir();
      }
    }
  }
  return 0;
}

/*
 * Write the new $HOME/.dialdir
 */
static void writedialdir(void)
{
  struct dialent *d;
  char dfile[256];
  FILE *fp;
  struct dver dial_ver;
  char oldfl;
  int omask;

  /* Make no sense if access denied */
  if (dendd)
    return;

  snprintf(dfile, sizeof(dfile), "%s/.dialdir", homedir);

  omask = umask(077);
  if ((fp = fopen(dfile, "w")) == NULL) {
    umask(omask);
    werror(_("Cannot open ~/.dialdir for writing!"));
    dendd = 1;
    return;
  }
  umask(omask);

  d = dialents;
  /* Set up version info. */
  dial_ver.magic   = DIALMAGIC;
  dial_ver.version = CURRENT_VERSION;
  dial_ver.size = htons(sizeof(struct dialent) - sizeof(void *));
  dial_ver.res1 = 0;	/* We don't use these res? fields, but let's */
  dial_ver.res2 = 0;	/* initialize them to a known init value for */
  dial_ver.res3 = 0;	/* whoever needs them later / jl 22.09.97    */
  dial_ver.res4 = 0;
  if (fwrite(&dial_ver, sizeof(dial_ver), 1, fp) != 1) {
    werror(_("Error writing to ~/.dialdir!"));
    fclose(fp);
    return;
  }

  /* Write dialing directory */
  while (d) {
    struct dialent dent_n;
    oldfl = d->flags;
    d->flags &= FL_SAVE;
    convert_to_save_order(&dent_n, d);
    if (fwrite(&dent_n, sizeof(dent_n) - sizeof(void *), 1, fp) != 1) {
      werror(_("Error writing to ~/.dialdir!"));
      fclose(fp);
      return;
    }
    d->flags = oldfl;
    d = d->next;
  }
  fclose(fp);
}


/*
 * Get entry "no" in list.
 */
static struct dialent *getno(int no)
{
  struct dialent *d;

  d = dialents;

  if (no < 0 || no >= nrents)
    return (struct dialent *)NULL;

  while (no--)
    d = d->next;

  return d;
}

/* Note: Minix does not exist anymore. */
static const char *te[] = { "VT102", "MINIX", "ANSI " };

/*
 * Display a "dedit" entry
 * We need to make sure that the previous value
 * gets fully overwritten in all languages (i.e. arbitrary strings)!
 */
static void dedit_toggle_entry(WIN *w, int x, int y, int toggle,
                               char *toggle_true, char *toggle_false)
{
  int lt = mbslen(toggle_true);
  int lf = mbslen(toggle_false);
  int l = ((lt > lf) ? lt : lf) + 1;
  char *buf, *s = toggle ? toggle_true : toggle_false;
  int i;

  if (!(buf = alloca(l)))
    return;

  strncpy(buf, s, l);
  for (i = mbslen(s); i < l - 1; i++)
    buf[i] = ' ';
  buf[l - 1] = 0;

  mc_wlocate(w, x, y);
  mc_wputs(w, buf);
}

/*
 * Edit an entry.
 */
static void dedit(struct dialent *d)
{
  WIN *w;
  int c;
  char *name		    = _(" A -  Name                :"),
       *number		    = _(" B -  Number              :"),
       *dial_string	    = _(" C -  Dial string #       :"),
       *local_echo_str	    = _(" D -  Local echo          :"),
       *script		    = _(" E -  Script              :"),
       *username	    = _(" F -  Username            :"),
       *password	    = _(" G -  Password            :"),
       *terminal_emulation  = _(" H -  Terminal Emulation  :"),
       *backspace_key_sends = _(" I -  Backspace key sends :"),
       *linewrap            = _(" J -  Linewrap            :"),
       *line_settings       = _(" K -  Line Settings       :"),
       *conversion_table    = _(" L -  Conversion table    :"),
       *question            = _("Change which setting?");

  w = mc_wopen(5, 4, 75, 19, BDOUBLE, stdattr, mfcolor, mbcolor, 0, 0, 1);
  mc_wprintf(w, "%s %s\n", name, d->name);
  mc_wprintf(w, "%s %s\n", number, d->number);
  mc_wprintf(w, "%s %d\n", dial_string, d->dialtype + 1);
  mc_wprintf(w, "%s %s\n", local_echo_str, _(yesno(d->flags & FL_ECHO)));
  mc_wprintf(w, "%s %s\n", script, d->script);
  mc_wprintf(w, "%s %s\n", username, d->username);
  mc_wprintf(w, "%s %s\n", password, d->password);
  mc_wprintf(w, "%s %s\n", terminal_emulation, te[d->term - 1]);
  mc_wprintf(w, "%s %s\n", backspace_key_sends,
          d->flags & FL_DEL ? _("Delete") : _("Backspace"));
  mc_wprintf(w, "%s %s\n", linewrap,
          d->flags & FL_WRAP ? _("On") : _("Off"));
  mc_wprintf(w, "%s %s %s%s%s\n", line_settings,
          d->baud, d->bits, d->parity, d->stopb);
  mc_wprintf(w, "%s %s\n", conversion_table, d->convfile);
  mc_wprintf(w, _("      Last dialed         : %s %s\n"),d->lastdate,d->lasttime);
  mc_wprintf(w, _("      Times on            : %d"),d->count);
  mc_wlocate(w, 4, 15);
  mc_wputs(w, question);
  mc_wredraw(w, 1);

  while (1) {
    mc_wlocate(w, mbslen (question) + 5, 15);
    c = wxgetch();
    if (c >= 'a')
      c -= 32;
    switch(c) {
      case '\033':
      case '\r':
      case '\n':
        mc_wclose(w, 1);
        return;
      case 'A':
        mc_wlocate(w, mbslen (name) + 1, 0);
        mc_wgets(w, d->name, 31, 32);
        break;
      case 'B':
        mc_wlocate(w, mbslen (number) + 1, 1);
        mc_wgets(w, d->number, 31, 32);
        break;
      case 'C':
        d->dialtype = (d->dialtype + 1) % 3;
        mc_wlocate(w, mbslen (dial_string) + 1, 2);
        mc_wprintf(w, "%d", d->dialtype + 1);
        mc_wflush();
        break;
      case 'D':
        d->flags ^= FL_ECHO;
        mc_wlocate(w, mbslen (local_echo_str) + 1, 3);
        mc_wprintf(w, "%s", _(yesno(d->flags & FL_ECHO)));
        mc_wflush();
        break;
      case 'E':
        mc_wlocate(w, mbslen (script) + 1, 4);
        mc_wgets(w, d->script, 31, 32);
        break;
      case 'F':
        mc_wlocate(w, mbslen (username) + 1, 5);
        mc_wgets(w, d->username, 31, 32);
        break;
      case 'G':
        mc_wlocate(w, mbslen (password) + 1, 6);
        mc_wgets(w, d->password, 31, 32);
        break;
      case 'H':
        d->term = (d->term % 3) + 1;
        /* MINIX == 2 is obsolete. */
        if (d->term == 2)
          d->term = 3;
        mc_wlocate(w, mbslen (terminal_emulation) + 1, 7);
        mc_wputs(w, te[d->term - 1]);

        /* Also set backspace key. */
        if (d->term == ANSI) {
          d->flags &= ~FL_DEL;
          d->flags |= FL_WRAP;
        } else {
          d->flags |= FL_DEL;
          d->flags &= ~FL_WRAP;
        }
	dedit_toggle_entry(w, mbslen(backspace_key_sends) + 1, 8,
	                   d->flags & FL_DEL, _("Delete"), _("Backspace"));
	dedit_toggle_entry(w, mbslen(linewrap) + 1, 9,
	                   d->flags & FL_WRAP, _("On"), _("Off"));
        break;
      case 'I':
        d->flags ^= FL_DEL;
	dedit_toggle_entry(w, mbslen(backspace_key_sends) + 1, 8,
	                   d->flags & FL_DEL, _("Delete"), _("Backspace"));
        break;
      case 'J':
        d->flags ^= FL_WRAP;
	dedit_toggle_entry(w, mbslen(linewrap) + 1, 9,
	                   d->flags & FL_WRAP, _("On"), _("Off"));
        break;
      case 'K':
        get_bbp(d->baud, d->bits, d->parity, d->stopb, 1);
        mc_wlocate(w, mbslen (line_settings) + 1, 10);
        mc_wprintf(w, "%s %s%s%s  ",
                d->baud, d->bits, d->parity, d->stopb);
        break;
      case 'L':	/* jl 21.09.97 */
        mc_wlocate(w, mbslen (conversion_table) + 1, 11);
        mc_wgets(w, d->convfile, 15, 16);
        break;
      default:
        break;
    }
  }
}

static WIN *dsub;
static const char *const what[] =
  {
    /* TRANSLATORS: Translation of each of these menu items should not be
     * longer than 7 characters. The upper-case letter is a shortcut,
     * so keep them unique and ASCII; 'h', 'j', 'k', 'l' are reserved */
    N_("Dial"), N_("Find"), N_("Add"), N_("Edit"), N_("Remove"), N_("moVe"),
    N_("Manual")
  };
/* Offsets of what[] entries from position_dialing_directory */
#define DIALOPTS (sizeof(what) / sizeof(*what))
#define DIAL_WIDTH 8 /* Width of one entry */
static int what_lens[DIALOPTS]; /* Number of bytes for <= 7 characters */
/* Number of ' ' padding entries at left and right, left is >= 1 */
static int what_padding[DIALOPTS][2];

static int dprev;

/* Draw an entry in the horizontal menu */
static void horiz_draw(size_t k)
{
  static const char spaces[] = "        ";

  mc_wprintf(dsub, "%.*s", what_padding[k][0], spaces);
  mc_wprintf(dsub, "%.*s", what_lens[k], _(what[k]));
  mc_wprintf(dsub, "%.*s", what_padding[k][1], spaces);
}

/*
 * Highlight a choice in the horizontal menu.
 */
static void dhili(int position_dialing_directory, int k)
{
  if (k == dprev)
    return;

  if (dprev >= 0) {
    mc_wlocate(dsub, position_dialing_directory + DIAL_WIDTH * dprev, 0);
    if (!useattr) {
      mc_wputs(dsub, " ");
    } else {
      mc_wsetattr(dsub, XA_REVERSE | stdattr);
      horiz_draw(dprev);
    }
  }
  dprev = k;
  mc_wlocate(dsub, position_dialing_directory + DIAL_WIDTH * k, 0);
  if (!useattr) {
    mc_wputs(dsub, ">");
  } else {
    mc_wsetattr(dsub, stdattr);
    horiz_draw(k);
  }
}

static const char *fmt = "\r %2d %c%-16.16s%-16.16s%8.8s %5.5s %4d %-15.15s\n";

/*
 * Print the dialing directory. Only draw from "cur" to bottom.
 */
static void prdir(WIN *dialw, int top, int cur)
{
  int f, start;
  struct dialent *d;

  start = cur - top;
  dirflush = 0;
  mc_wlocate(dialw, 0, start + 1);
  for (f = start; f < dialw->ys - 2; f++) {
    d = getno(f + top);
    if (d == (struct dialent *)0)
      break;
    mc_wprintf(dialw, fmt, f+1+top, (d->flags & FL_TAG) ? '>' : ' ',
            d->name, d->number, d->lastdate, d->lasttime, 
            d->count, d->script);
  }
  dirflush = 1;
  mc_wflush();
}

/*
 * Move an entry forward/back in the dial directory. jl 1.9.1999
 */
int move_entry(WIN *dialw, struct dialent *d, int cur, int *top)
{
  int ocur = cur,
      quit = 0,
      c = 0;
  struct dialent *dtmp;

  while (!quit) {
    switch (c = wxgetch()) {
      case K_DN:
      case 'j':
        if (!(d->next))
          break;
        if (cur == 0) {   /* special case: move d from start to 2nd */
          dtmp = d->next;
          d->next = dtmp->next;
          dtmp->next = d;
          dialents = dtmp;
        } else {	    /* swap d with the next one in the list */
          dtmp = getno(cur - 1);
          dtmp->next = d->next;
          d->next = d->next->next;
          dtmp->next->next = d;
        }
        cur++;
        break;
      case K_UP:
      case 'k':
        if (cur == 0)
          break;
        if (cur == 1) { /* special case: move d to start of list */
          dtmp = dialents;
          dtmp->next = d-> next;
          d->next = dtmp;
          dialents = d;
        } else {	  /* swap d with the previous one in the list */
          dtmp = getno(cur - 2);
          dtmp->next->next = d-> next;
          d->next = dtmp->next;
          dtmp->next = d;
        }
        cur--;
        break;
      case '\033':
      case '\r':
      case '\n':
        quit = 1;
        break;
      default:
        break;
    } /* end switch */

    /* If the list order changed, redraw the directory window */
    if (cur != ocur) {
      /* First remove cursor bar from the old position */
      mc_wcurbar(dialw, ocur + 1 - *top, XA_NORMAL | stdattr);
      if (cur < *top)
        (*top)--;
      else if (cur - *top > dialw->ys - 3)
        (*top)++;
      prdir(dialw, *top, *top);
      mc_wcurbar(dialw, cur + 1 - *top, XA_REVERSE | stdattr);
      ocur = cur;
    } /* end redraw condition */
  }   /* end loop   */

  return cur;
}

/* Little menu. */
static const char *d_yesno[] = { N_("   Yes  "), N_("   No   "), NULL };

/* Try to dial an entry. */
static void dial_entry(struct dialent *d)
{
  long nb;
  struct dialent *d2;

  /* Change settings for this entry. */
  if (atoi(d->baud) != 0) {
    strcpy(P_BAUDRATE, d->baud);
    strcpy(P_PARITY, d->parity);
    strcpy(P_BITS, d->bits);
    strcpy(P_STOPB, d->stopb);
    port_init();
    mode_status();
  }
  newtype = d->term;
  vt_set(-1, d->flags & FL_WRAP, -1, -1, d->flags & FL_ECHO, -1, -1, -1, -1);
  local_echo = d->flags & FL_ECHO;
  if (newtype != terminal)
    init_emul(newtype, 1);

  /* Set backspace key. */
  keyboard(KSETBS, d->flags & FL_DEL ? 127 : 8);
  strcpy(P_BACKSPACE, d->flags & FL_DEL ? "DEL" : "BS");

  /* Now that everything has been set, dial. */
  if ((nb = dial(d, &d2)) < 0)
    return;
  
  if (d2 != (struct dialent *)NULL)
    d = d2;	/* jl 22.09.97 */

  /* Did we detect a baudrate , and can we set it? */
  if (P_MAUTOBAUD[0] == 'Y' && nb) {
    sprintf(P_BAUDRATE, "%ld", nb);
    port_init();
    mode_status();
  } else if (P_SHOWSPD[0] == 'l')
    mode_status();

  /* Make sure the last date is updated   / jl 22.06.97 */
  writedialdir();

  /* Run script if needed. */
  if (d->script[0])
    runscript(0, d->script, d->username, d->password);

  /* Remember _what_ we dialed.. */
  dial_name = d->name;
  dial_number = d->number;
  dial_user = d->username;
  dial_pass = d->password;

  return;
}

/*
 * Dial an entry from the dialing directory; this
 * is used for the "-d" command line flag.
 * Now you can tag multiple entries with the -d option / jl 3.5.1999
 */
void dialone(char *entry)
{
  int num;
  struct dialent *d;
  struct dialent *d1 = (struct dialent *)NULL;
  char *s;
  char buf[128];

  s = strtok(entry,",;");
  while (s) {
    /* Find entry. */
    if ((num = atoi(s)) != 0) {
      if ((d = getno(num - 1))) {
        d->flags |= FL_TAG;
        if (d1 == (struct dialent *)NULL)
          d1 = d;
      }
    } else {
      for (d = dialents; d; d = d->next)
        if (strstr(d->name, s)) {
          d->flags |= FL_TAG;
          if (d1 == (struct dialent *)NULL)
            d1 = d;
        }
    }
    s = strtok(NULL,",;");
  }

  /* Not found. */
  if (d1 == NULL) {
    snprintf(buf, sizeof(buf), _("Entry \"%s\" not found. Enter dialdir?"), entry);
    if (ask(buf, d_yesno) != 0)
      return;
    dialdir();
    return;
  }
  /* Dial the number! */
  sleep(1);
  dial_entry(d1);
}

/*
 * Draw the dialing directory.
 */
void dialdir(void)
{
  WIN *w;
  struct dialent *d = NULL, *d1, *d2;
  static int cur = 0;
  static int ocur = 0;
  int subm = 0;
  int quit = 0;
  static int top = 0;
  int c = 0;
  int pgud = 0;
  int first = 1;
  int x1, x2;
  char *s, dname[128];
  static char manual[128];
  int changed = 0;
  static const char *tag_exit  = N_("( Escape to exit, Space to tag )"),
                    *move_exit = N_(" Move entry up/down, Escape to exit");
  unsigned int tagmvlen = 0;
  size_t i;
  int position_dialing_directory = ((COLS / 2) + 32 - DIALOPTS * DIAL_WIDTH) / 2;

  dprev = -1;
  dname[0] = 0;
  tagmvlen = strlen(_(move_exit));
  if (strlen(_(tag_exit)) > tagmvlen)
    tagmvlen = strlen(_(tag_exit));

  /* Allright, draw the dialing directory! */

  dirflush = 0;
  x1 = (COLS / 2) - 37;
  x2 = (COLS / 2) + 37;
  dsub = mc_wopen(x1 - 1, LINES - 3, x2 + 1, LINES - 3, BNONE,
               XA_REVERSE | stdattr, mfcolor, mbcolor, 0, 0, 1);
  w = mc_wopen(x1, 2, x2, LINES - 6, BSINGLE, stdattr, mfcolor, mbcolor, 0, 0, 1);
  mc_wcursor(w, CNONE);
  mc_wtitle(w, TMID, _("Dialing Directory"));
  mc_wputs(w,
        _("     Name            Number            Last on      Times Script\n"));
  for (i = 0; i < DIALOPTS; i++) {
    const char *str, *c;
    size_t j;

    str = _(what[i]);
    c = str;
    for (j = 0; j < DIAL_WIDTH - 1 && *c != 0; j++) {
      wchar_t wc;
      c += one_mbtowc(&wc, c, MB_LEN_MAX);
    }
    what_lens[i] = c - str;
    j = DIAL_WIDTH - j; /* Characters left for padding */
    what_padding[i][1] = j / 2; /* Rounding down */
    what_padding[i][0] = j - what_padding[i][1]; /* >= 1 */
  }
  mc_wlocate(dsub, position_dialing_directory, 0);
  for (i = 0; i < DIALOPTS; i++)
    horiz_draw(i);

  mc_wsetregion(w, 1, w->ys - 1);
  w->doscroll = 0;

  prdir(w, top, top);
  mc_wlocate(w, position_dialing_directory, w->ys - 1);
  mc_wprintf(w, "%*.*s", tagmvlen,tagmvlen, tag_exit);
  dhili(position_dialing_directory, subm);
  dirflush = 1;
  mc_wredraw(dsub, 1);

again:
  mc_wcurbar(w, cur + 1 - top, XA_REVERSE | stdattr);
  if (first) {
    mc_wredraw(w, 1);
    first = 0;
  }
  while(!quit) {
    d = getno(cur);
    switch (c = wxgetch()) {
      case K_UP:
      case 'k':
        cur -= (cur > 0);
        break;
      case K_DN:
      case 'j':
        cur += (cur < nrents - 1);
        break;
      case K_LT:
      case 'h':
        subm--;
        if (subm < 0)
          subm = DIALOPTS - 1;
        break;
      case K_RT:
      case 'l':
        subm = (subm + 1) % DIALOPTS;
        break;
      case K_PGUP:
      case '\002': /* Control-B */
        pgud = 1;
        quit = 1;
        break;
      case K_PGDN:
      case '\006': /* Control-F */
        pgud = 2;
        quit = 1;
        break;
      case ' ':    /* Tag. */
        mc_wlocate(w, 4, cur + 1 - top);
        d->flags ^= FL_TAG;
        mc_wsetattr(w, XA_REVERSE | stdattr);
        mc_wprintf(w, "%c", d->flags & FL_TAG ? '>' : ' ');
        mc_wsetattr(w, XA_NORMAL | stdattr);
        cur += (cur < nrents - 1);
        break;
      case '\033':
      case '\r':
      case '\n':
      selected:
        quit = (subm == 5 ? 2 : 1);
        break;
      default:
	for (i = 0; i < DIALOPTS; i++) {
	  if (strchr(_(what[i]), toupper(c)) != NULL) {
	    subm = i;
	    goto selected;
	  }
	}
        break;
    }
    /* Decide if we have to delete the cursor bar */
    if (cur != ocur || quit == 1)
      mc_wcurbar(w, ocur + 1 - top, XA_NORMAL | stdattr);

    if (cur < top) {
      top--;
      prdir(w, top, top);
    }
    if (cur - top > w->ys - 3) {
      top++;
      prdir(w, top, top);
    }
    if (cur != ocur)
      mc_wcurbar(w, cur + 1 - top, XA_REVERSE | stdattr);
    ocur = cur;
    dhili(position_dialing_directory, subm);
  }
  quit = 0;
  /* ESC means quit */
  if (c == '\033') {
    if (changed)
      writedialdir();
    mc_wclose(w, 1);
    mc_wclose(dsub, 1);
    return;
  }
  /* Page up or down ? */
  if (pgud == 1) { /* Page up */
    ocur = top;
    top -= w->ys - 2;
    if (top < 0)
      top = 0;
    cur = top;
    pgud = 0;
    if (ocur != top)
      prdir(w, top, cur);
    ocur = cur;
    goto again;
  }
  if (pgud == 2) { /* Page down */
    ocur = top;
    if (top < nrents - w->ys + 2) {
      top += w->ys - 2;
      if (top > nrents - w->ys + 2)
        top = nrents - w->ys + 2;
      cur = top;
    } else
      cur = nrents - 1;
    pgud = 0;
    if (ocur != top)
      prdir(w, top, cur);
    ocur = cur;
    goto again;
  }

  /* Dial an entry */
  if (subm == 0) {
    mc_wclose(w, 1);
    mc_wclose(dsub, 1);
    if (changed)
      writedialdir();

    /* See if any entries were tagged. */
    if (!(d->flags & FL_TAG)) {
      /* First check the entries from the highlighted one to end.. */
      for (d1 = d; d1; d1 = d1->next)
        if (d1->flags & FL_TAG) {
          d = d1;
          break;
        }
      /* ..and if none of them was tagged, check from the begining. */
      if (!d1)
        for (d1 = dialents; d1 && d1!=d; d1 = d1->next)
          if (d1->flags & FL_TAG) {
            d = d1;
            break;
          }
      /* If no tags were found, we'll dial the highlighted one */
    }
    dial_entry(d);
    return;
  }
  /* Find an entry */
  if (subm == 1) {
    s = input(_("Find an entry"), dname);
    if (s == NULL || s[0] == 0)
      goto again;
    x1 = 0;
    for (d = dialents; d; d = d->next, x1++)
      if (strstr(d->name, s))
        break;
    if (d == NULL) {
      mc_wbell();
      goto again;
    }
    /* Set current to found entry. */
    ocur = top;
    cur = x1;
    /* Find out if it fits on screen. */
    if (cur < top || cur >= top + w->ys - 2) {
      /* No, try to put it in the middle. */
      top = cur - (w->ys / 2) + 1;
      if (top < 0)
        top = 0;
      if (top > nrents - w->ys + 2)
        top = nrents - w->ys + 2;
    }
    if (ocur != top)
      prdir(w, top, top);
    ocur = cur;
  }

  /* Add / insert an entry */
  if (subm == 2) {
    d1 = mkstdent();
    if (d1 == (struct dialent *)0) {
      mc_wbell();
      goto again;
    }
    changed++;
    cur++;
    ocur = cur;
    d2 = d->next;
    d->next = d1;
    d1->next = d2;

    nrents++;
    if (cur - top > w->ys - 3) {
      top++;
      prdir(w, top, top);
    } else {
      prdir(w, top, cur);
    }
  }

  /* Edit an entry */
  if (subm == 3) {
    dedit(d);
    changed++;
    mc_wlocate(w, 0, cur + 1 - top);
    mc_wprintf(w, fmt, cur+1, (d->flags & FL_TAG) ? 16 : ' ', d->name,
            d->number, d->lastdate, d->lasttime, d->count, d->script);
  }

  /* Delete an entry from the list */
  if (subm == 4 && ask(_("Remove entry?"), d_yesno) == 0) {
    changed++;
    if (nrents == 1) {
      free((char *)d);
      d = dialents = mkstdent();
      prdir(w, top, top);
      goto again;
    }
    if (cur == 0)
      dialents = d->next;
    else
      getno(cur - 1)->next = d->next;
    free((char *)d);
    nrents--;
    if (cur - top == 0 && top == nrents) {
      top--;
      cur--;
      prdir(w, top, top);
    } else {
      if (cur == nrents)
        cur--;
      prdir(w, top, cur);
    }
    if (nrents - top <= w->ys - 3) {
      mc_wlocate(w, 0, nrents - top + 1);
      mc_wclreol(w);
    }
    ocur = cur;
  }

  /* Move the entry up/down in directory. */
  if (subm == 5) {
    mc_wlocate(w, position_dialing_directory, w->ys - 1);
    mc_wprintf(w, "%*.*s", tagmvlen,tagmvlen, move_exit);
    cur = move_entry (w, d, cur, &top);
    if (cur != ocur)
      changed++;
    ocur = cur;
    mc_wlocate(w, position_dialing_directory, w->ys - 1);
    mc_wprintf(w, "%*.*s", tagmvlen,tagmvlen, tag_exit);
  }

  /* Dial a number manually. */
  if (subm == 6) {
    s = input(_("Enter number"), manual);
    if (s && *s) {
      if (changed)
        writedialdir();
      mc_wclose(w, 1);
      mc_wclose(dsub, 1);

      strncpy(d_man->number, manual, sizeof(d_man->number));
      dial(d_man, (struct dialent**)NULL);
      if (P_SHOWSPD[0] == 'l')
        mode_status();
      return;
    }
  }
  goto again;
}
