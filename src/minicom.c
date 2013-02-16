/*
 * minicom.c	Main program. The main loop of the terminal emulator
 *		itself is in main.c. (Yeah yeah it's confusing).
 *
 *		This file is part of the minicom communications package,
 *		Copyright 1991-1996 Miquel van Smoorenburg.
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
 * fmg 1/11/94 colors
 * jl  30.06.97 log it if you quit without reset while online
 * fmg 8/20/97 History buffer searching added
 * jseymour@jimsun.LinxNet.com (Jim Seymour) 03/26/98 - Added support for
 *    multiple tty devices via new "get_port()" function.
 * kubota@debian.or.jp 07/98  - Added option -C to start capturing from the
 *				command line
 * jl  09.07.98 added option -S to start a script at startup
 * mark.einon@gmail.com 16/02/11 - Added option to timestamp terminal output
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <getopt.h>
#include <wchar.h>
#include <wctype.h>
#include <iconv.h>
#include <limits.h>

#define EXTERN
#include "port.h"
#include "minicom.h"
#include "intl.h"

static const char option_string[] =
#ifdef ENABLE_NLS
"I18n "
#endif
"";

#define RESET 1
#define NORESET 2

#ifdef DEBUG
/* Show signals when debug is on. */
static void signore(int sig)
{
  if (stdwin)
    werror(_("Got signal %d"), sig);
  else
    printf("%s\r\n", _("Got signal %d"), sig);
}
#endif /*DEBUG*/

static int line_timestamp;

/*
 * Sub - menu's.
 */

static const char *c1[] = { N_("   Yes  "), N_("   No   "), NULL };
static const char *c2[] = { N_("  Close "), N_(" Pause  "), N_("  Exit  "), NULL };
static const char *c3[] = { N_("  Close "), N_(" Unpause"), N_("  Exit  "), NULL };
static const char *c7[] = { N_("   Yes  "), N_("   No   "), NULL };

/* Initialize modem port. */
void port_init(void)
{
  m_setparms(portfd, P_BAUDRATE, P_PARITY, P_BITS, P_STOPB,
             P_HASRTS[0] == 'Y', P_HASXON[0] == 'Y');
}

static void do_hang(int askit)
{
  int c = 0;

  if (askit)
    c = ask(_("Hang-up line?"), c7);
  if (c == 0)
    hangup();
}

/*
 * We've got the hangup or term signal.
 */
static void hangsig(int sig)
{
  if (stdwin)
    werror(_("Killed by signal %d !\n"), sig);
  if (capfp)
    fclose(capfp);

  keyboard(KUNINSTALL, 0);
  hangup();
  modemreset();
  leave("\n");
}

/*
 * Jump to a shell
 */
#ifdef SIGTSTP
static void shjump(int sig)
{
  sigset_t ss, oldss;

  (void)sig;
  mc_wleave();
  signal(SIGTSTP, SIG_DFL);
  sigemptyset(&ss);
  sigaddset(&ss, SIGTSTP);
  sigprocmask(SIG_UNBLOCK, &ss, &oldss);
  fputs(_("Suspended. Type \"fg\" to resume.\n"),stdout);
  kill(getpid(), SIGTSTP);
  sigprocmask(SIG_SETMASK, &oldss, NULL);
  signal(SIGTSTP, shjump);
  mc_wreturn();
  if (use_status)
    show_status();
}
#else
static void shjump(int sig)
{
  char *sh;
  int pid;
  int status;
  int f;

  (void)sig;
  sh = getenv("SHELL");
  if (sh == NULL) {
    werror(_("SHELL variable not set"));
    return;
  }
  if ((pid = fork()) == -1) {
    werror(_("Out of memory: could not fork()"));
    return;
  }
  if (pid != 0)
    mc_wleave();
  if (pid == 0) {
    for (f = 1; f < _NSIG; f++)
      signal(f, SIG_DFL);
    for (f = 3; f < 20; f++)
      close(f);
    fputs(_("Shelled out. Type \"exit\" to return.\n"), stdout);
    execl(sh, sh, NULL);
    exit(1);
  }
  m_wait(&status);
  mc_wreturn();
  if (use_status)
    show_status();
}
#endif /*SIGTSTP*/

/* Get a line from either window or scroll back buffer. */
static ELM *mc_getline(WIN *w, int no)
{
  int i;
  static ELM outofrange[MAXCOLS] = {{0,0,0}};

  if (no < us->histlines) {
    /* Get a line from the history buffer. */
    i = no + us->histline /*- 1*/;
    if (i >= us->histlines)
      i -= us->histlines;
    if (i < 0)
      i = us->histlines - 1;
    return us->histbuf + (i * us->xs);
  }

  /* Get a line from the "us" window. */
  no -= us->histlines;
  if (no >= w->ys) {
    if (outofrange[0].value == 0) {
      for (i = 0; i < MAXCOLS; i++) {
        outofrange[i].value = ' ';
        outofrange[i].color = us->color;
        outofrange[i].attr  = us->attr;
      }
      outofrange[0].value = '~';
    }
    return outofrange;
  }
  return w->map + (no * us->xs);
}

/* Redraw the window. */
static void drawhist(WIN *w, int y, int r)
{
  int f;

  w->direct = 0;
  for (f = 0; f < w->ys; f++)
    mc_wdrawelm(w, f, mc_getline(w, y++));
  if (r)
    mc_wredraw(w, 1);
  w->direct = 1;
}

/*
 * fmg 8/20/97
 * drawhist_look()
 * Redraw the window, highlight line that was found to contain
 * pattern 'look'
 * Needed by re-draw screen function after EACH find_next()
 */
void drawhist_look(WIN *w, int y, int r, wchar_t *look, int case_matters)
{
  int f;
  ELM *tmp_e;
  wchar_t tmp_line[MAXCOLS];

  tmp_line[0]='\0';
  w->direct = 0;
  for (f = 0; f < w->ys; f++) {
    tmp_e = mc_getline(w, y++);

    /* First we "accumulate" the line into a variable */
    mc_wdrawelm_var(w, tmp_e, tmp_line);

    /* Does it have what we want? */
    if (wcslen(look) > 1 && wcslen(tmp_line) > 1) {
      if (StrStr(tmp_line,look, case_matters))
        mc_wdrawelm_inverse(w, f, tmp_e); /* 'inverse' it */
      else
        mc_wdrawelm(w, f, tmp_e); /* 'normal' output */
    }
  }

  if (r)
    mc_wredraw(w, 1);
  w->direct = 1;
}

/*
 * fmg 8/20/97
 * Search history - main function that started the C-code blasphemy :-)
 * This function doesn't care about case/case-less status...
 */
void searchhist(WIN *w_hist, wchar_t *str)
{
  int y;
  WIN *w_new;
  const char *hline;
  size_t i;

  /* Find out how big a window we must open. */
  y = w_hist->y2;
  if (st == (WIN *)0 || (st && tempst))
    y--;

  /* Open a Search line window. */
  w_new = mc_wopen(0, y+1, w_hist->x2, y+1, 0, st_attr, sfcolor, sbcolor, 0, 0, 1);
  w_new->doscroll = 0;
  w_new->wrap = 0;

  hline = _("SEARCH FOR (ESC=Exit)");
  mc_wprintf(w_new, "%s(%d):",hline,MAX_SEARCH);
  mc_wredraw(w_new, 1);
  mc_wflush();

  mc_wlocate(w_new, mbslen(hline)+6, 0);
  for (i = 0; str[i] != 0; i++)
    mc_wputc(w_new, str[i]);
  mc_wlocate(w_new, mbslen(hline)+6, 0);
  mc_wgetwcs(w_new, str, MAX_SEARCH, MAX_SEARCH);
#if 0
  if (!str[0]) { /* then unchanged... must have pressed ESC... get out */
    mc_wflush();
    mc_wclose(w_new, 0);
    return;
  }
#endif

  mc_wredraw(w_hist, 1);
  mc_wclose(w_new, 1);
  mc_wflush();

  return;
}

/*
 * fmg 8/20/97
 * Move scope to next hit of pattern in the buffer.
 * Returns line-number of next "hit_line" or -1 if none found
 * (we beep elsewhere ;-)
 */
int find_next(WIN *w, WIN *w_hist,
              int hit_line,     /* 'current' Match line */
              wchar_t *look,    /* pattern */
              int case_matters) /* guess... */
{
  int next_line;
  ELM *tmp_e;
  wchar_t tmp_line[MAXCOLS];
  int all_lines;

  if (!look)
    return(++hit_line); /* next line */

  tmp_line[0] = '\0';	/* Personal phobia, I need to do this.. */

  hit_line++;           /* we NEED this so we don't search only same line! */
  all_lines = w->histlines + w_hist->ys;

  if (hit_line >= all_lines) {	/* Make sure we've got a valid line! */
    werror(_("Search Wrapping Around to Start!"));
    hit_line = 0;
  }

  for (next_line = hit_line; next_line <= all_lines; next_line++) {
    /* we do 'something' here... :-) */
    tmp_e = mc_getline(w_hist, next_line);

    /*
     * First we "accumulate" the line into a variable.
     * To see 'why', see what an 'ELM' structure looks like!
     */
    mc_wdrawelm_var(w, tmp_e, tmp_line);

    /* Does it have what we want? */
    if (wcslen(tmp_line) > 1 && wcslen(look) > 1)
      if (StrStr(tmp_line, look, case_matters))
        return next_line;
  }

  if (hit_line >= all_lines) {	/* Make sure we've got a valid line! */
    werror(_("Search Wrapping Around to Start!"));
    hit_line = 0;
  }

  return -1; /* nothing found! */
}

/*
 * fmg 8/22/97
 * Needed this for the case-less conparison... and Linux libc
 * doesn't seem to have a strnstr function... so we fudge.. ;-)
 */
const wchar_t *upcase(wchar_t *dest, wchar_t *src)
{
  wchar_t *d = dest;

  while (*src)
    *d++ = towupper(*src++);
  *d = '\0';
  return dest;
}

/*
 * fmg 8/22/97
 * Needed this for the case-less conparison... and Linux libc
 * doesn't seem to have a strnstr function... so we fudge.. ;-)
 */
wchar_t *StrStr(wchar_t *str1, wchar_t *str2, int case_matters)
{
  wchar_t tmpstr1[MAXCOLS], tmpstr2[MAXCOLS];

  if (case_matters)
    return wcsstr(str1, str2);
  else
    return wcsstr(upcase(tmpstr1, str1), upcase(tmpstr2, str2));
}

static void drawcite(WIN *w, int y, int citey, int start, int end)
{
  if (y+citey >= start && y+citey <= end)
    mc_wdrawelm_inverse(w, y, mc_getline(w, y+citey));
  else
    mc_wdrawelm(w, y, mc_getline(w, y+citey));
}

static void drawcite_whole(WIN *w, int y, int start, int end)
{
  int sy;
  for (sy = 0; sy < w->ys; sy++)
    drawcite(w, sy, y, start, end);
}

static void do_cite(WIN *w, int start, int end)
{
  wchar_t tmp_line[MAXCOLS];
  ELM *tmp_e;
  int x, y;

  for (y=start; y<=end; y++) {
    vt_send('>');
    vt_send(' ');
    tmp_e = mc_getline(w, y);
    mc_wdrawelm_var(w, tmp_e, tmp_line);
    tmp_line[w->xs] = 0;
    for (x = w->xs-1; x >= 0; x--) {
      if (tmp_line[x] <= ' ')
        tmp_line[x]=0;
      else
        break;
    }
    for (x = 0; tmp_line[x]; x++) {
      char buf[MB_LEN_MAX];
      size_t i, len;

      len = one_wctomb(buf, tmp_line[x]);
      for (i = 0; i < len; i++)
	vt_send(buf[i]);
    }
    vt_send(13);
  }
}

/* Scroll back */
static void scrollback(void)
{
  int y,c;
  WIN *b_us, *b_st;
  ELM *tmp_e;
  int case_matters=0;	/* fmg: case-importance, needed for 'N' */
  static wchar_t look_for[MAX_SEARCH];	/* fmg: last used search pattern */
  wchar_t tmp_line[MAXCOLS];
  int citemode = 0;
  int cite_ystart = 1000000,
      cite_yend = -1,
      cite_y = 0;
  int inverse;
  int loop = 1;

  char hline0[128], hline1[128], *hline;
  static int hit=0;

  /* Find out how big a window we must open. */
  y = us->y2;

  if (st == (WIN *)0 || (st && tempst))
    y--;

  /* Open a window. */
  b_us = mc_wopen(0, 0, us->x2, y, 0, us->attr, COLFG(us->color),
               COLBG(us->color), 0, 0, 0);
  mc_wcursor(b_us, CNONE);

  /* Open a help line window. */
  b_st = mc_wopen(0, y+1, us->x2, y+1, 0, st_attr, sfcolor, sbcolor, 0, 0, 1);
  b_st->doscroll = 0;
  b_st->wrap = 0;

  /* Make sure help line is as wide as window. */

  /*
   * fmg 8/20/97
   * added /=Srch, \=CaseLess, and N=Next and changed rest of line...
   * Hope you like it :-)
   */
  strcpy(hline0,
         _("HISTORY: U=Up D=Down F=PgDn B=PgUp s=Srch S=CaseLess N=Next C=Cite ESC=Exit "));

  if (b_st->xs < 127)
    hline0[b_st->xs] = 0;
  hline = hline0;
  mc_wprintf(b_st, "%s", hline);
  mc_wredraw(b_st, 1);
  mc_wflush();

  /* And do the job. */
  y = us->histlines;

  /* fmg 8/20/97
   * Needed for N)extSearch, keeps track of line on which current "hit"
   * is... we advance it to 'N'ext hit in find_next(). We start at "top"
   * of history stack
   */
  hit = 0;

  drawhist(b_us, y, 0);

  while (loop) {
    c = wxgetch();
    switch (c) {
      /*
       * fmg 8/22/97
       * Take care of the search key: Caseless
       */
      case '\\':
      case 'S':
        case_matters = 0; /* case-importance, ie. none :-) */
        /*
         * fmg 8/22/97
         * Take care of the search key: Exact Match
         */
      case '/':
      case 's':
        if (!us->histlines) {
          mc_wbell();
          werror(_("History buffer Disabled!"));
          break;
        }
        if (!us->histline) {
          mc_wbell();
          werror(_("History buffer empty!"));
          break;
        }
        if (citemode)
          break;

        /* we need this for the case-importance-toggle to work.. */
        if (c == '/' || c == 's')
          case_matters=1; /* case-importance, ie. DOES */

        /* open up new search window... */
        searchhist(b_us, look_for);
        /* must redraw status line... */
        mc_wlocate(b_st, 0, 0); /* move back to column 0! */
        mc_wprintf(b_st, "%s", hline); /* and show the above-defined hline */
        mc_wredraw(b_st, 1); /* again... */
        /* highlight any matches */
        if (wcslen(look_for) > 1) {
          hit = find_next(us, b_us, y, look_for, case_matters);

          if (hit == -1) {
            mc_wbell();
            mc_wflush();
            hit = 0;
            break;
          }
          drawhist_look(b_us, hit, 1, look_for, case_matters);
          y = hit;
        } else {
          mc_wbell();
          break;
        }
        mc_wflush();
        break;
        /*
         * fmg 8/22/97
         * Take care of the Next Hit key...
         * Popup an error window if no previous... why not start a new
         * search? How do we know which case-importance they wanted?
         */
      case 'n':
      case 'N':
        /* highlight NEXT match */
        if (citemode)
          break;
        if (wcslen(look_for) > 1) {
          hit = find_next(us, b_us, y, look_for, case_matters);

          if (hit == -1) {
            mc_wbell();
            mc_wflush();
            hit = 0;
            break;
          }
          drawhist_look(b_us, hit, 1, look_for, case_matters);
          y = hit;
        } else	{ /* no search pattern... */
          mc_wbell();
          werror(_("No previous search!\n  Please 's' or 'S' first!"));
        }
        mc_wflush();
        break;

      case 'u':
      case 'U':
      case K_UP:
        if (citemode && cite_y) {
          cite_y--;
          if (cite_ystart != 1000000) {
            cite_yend = y + cite_y;
            drawcite(b_us, cite_y+1, y, cite_ystart, cite_yend);
            drawcite(b_us, cite_y, y, cite_ystart, cite_yend);
          }
          mc_wlocate(b_us, 0, cite_y);
          break;
        }
        if (y <= 0)
          break;
        y--;
        if (cite_ystart != 1000000)
          cite_yend = y + cite_y;
        mc_wscroll(b_us, S_DOWN);

        /*
         * fmg 8/20/97
         * This is needed so that the movement in window will HIGHLIGHT
         * the lines that have the pattern we wanted... it's just nice.
         * This almost beggs for a function :-)
         */
        if (citemode) {
          inverse = (y+cite_y >= cite_ystart && y+cite_y <= cite_yend);
        } else {
          tmp_e = mc_getline(b_us, y);
          if (wcslen(look_for) > 1) {
            /* quick scan for pattern match */
            mc_wdrawelm_var(b_us, tmp_e, tmp_line);
            inverse = (wcslen(tmp_line)>1 &&
                         StrStr(tmp_line, look_for, case_matters));
          } else
            inverse = 0;
        }

        if (inverse)
          mc_wdrawelm_inverse(b_us, 0, mc_getline(b_us, y));
        else
          mc_wdrawelm(b_us, 0, mc_getline(b_us, y));
        if (citemode)
          mc_wlocate(b_us, 0, cite_y);
        mc_wflush();
        break;
      case 'd':
      case 'D':
      case K_DN:
        if (citemode && cite_y < b_us->ys-1) {
          cite_y++;
          if (cite_ystart != 1000000) {
            cite_yend = y + cite_y;
            drawcite(b_us, cite_y-1, y, cite_ystart, cite_yend);
            drawcite(b_us, cite_y, y, cite_ystart, cite_yend);
          }
          mc_wlocate(b_us, 0, cite_y);
          break;
        }

        if (y >= us->histlines)
          break;
        y++;
        if (cite_ystart != 1000000)
          cite_yend = y + cite_y;
        mc_wscroll(b_us, S_UP);

        /*
         * fmg 8/20/97
         * This is needed so that the movement in window will HIGHLIGHT
         * the lines that have the pattern we wanted... it's just nice.
         * This almost beggs for a function :-)
         */
        if (citemode) {
          inverse = (y+cite_y >= cite_ystart && y+cite_y <= cite_yend);
        } else {
          tmp_e = mc_getline(b_us, y + b_us->ys - 1);
          if (wcslen(look_for) > 1) {
            /* quick scan for pattern match */
            mc_wdrawelm_var(b_us, tmp_e, tmp_line);
            inverse = (wcslen(tmp_line)>1 &&
                         StrStr(tmp_line, look_for, case_matters));
          } else
            inverse = 0;
        }

        if (inverse)
          mc_wdrawelm_inverse(b_us, b_us->ys - 1,
                           mc_getline(b_us, y + b_us->ys - 1));
        else
          mc_wdrawelm(b_us, b_us->ys - 1,
                   mc_getline(b_us, y + b_us->ys - 1));
        if (citemode)
          mc_wlocate(b_us, 0, cite_y);
        mc_wflush();
        break;
      case 'b':
      case 'B':
      case K_PGUP:
        if (y <= 0)
          break;
        y -= b_us->ys;
        if (y < 0)
          y = 0;
        if (cite_ystart != 1000000)
          cite_yend = y + cite_y;

        /*
         * fmg 8/20/97
         * This is needed so that the movement in window will HIGHLIGHT
         * the lines that have the pattern we wanted... it's just nice.
         * Highlight any matches
         */
        if (wcslen(look_for) > 1 && us->histline)
          drawhist_look(b_us, y, 1, look_for, case_matters);
        else
          drawhist(b_us, y, 1);

        if (citemode)
          mc_wlocate(b_us, 0, cite_y);
        break;
      case 'f':
      case 'F':
      case ' ': /* filipg: space bar will go page-down... pager-like */
      case K_PGDN:
        if (y >= us->histlines)
          break;
        y += b_us->ys;
        if (y > us->histlines)
          y=us->histlines;
        if (cite_ystart != 1000000)
          cite_yend = y + cite_y;

        /*
         * fmg 8/20/97
         * This is needed so that the movement in window will HIGHLIGHT
         * the lines that have the pattern we wanted... it's just nice.
         * Highlight any matches
         */
        if (wcslen(look_for) > 1 && us->histline)
          drawhist_look(b_us, y, 1, look_for, case_matters);
        else
          drawhist(b_us, y, 1);
        if (citemode)
          mc_wlocate(b_us, 0, cite_y);
        break;
      case 'C': case 'c': /* start citation mode */
        if (citemode ^= 1) {
          cite_y = 0;
          cite_ystart = 1000000;
          cite_yend = -1;
          strcpy(hline1, _("  CITATION: ENTER=select start line ESC=exit                               "));
          if (b_st->xs < 127)
            hline1[b_st->xs]=0;
          hline = hline1;
        } else {
          hline = hline0;
        }
        mc_wlocate(b_st, 0, 0);
        mc_wprintf(b_st, "%s", hline);
        mc_wredraw(b_st, 1);
        if (citemode)
          mc_wlocate(b_us, 0, cite_y);
        break;
      case 10: case 13:
        if (!citemode) break;
        if (cite_ystart == 1000000) {
          cite_yend = cite_ystart = y + cite_y;
          strcpy(hline1, _("  CITATION: ENTER=select end line ESC=exit                                 "));
          if (b_st->xs < 127)
            hline1[b_st->xs]=0;
        } else {
          if (cite_ystart > cite_yend)
            break;
          drawcite_whole(b_us, y, 1000000, -1);
          loop = 0;
          break;
        }
        mc_wlocate(b_st, 0, 0);
        mc_wprintf(b_st, "%s", hline);
        mc_wredraw(b_st, 1);
        mc_wdrawelm_inverse(b_us, cite_y, mc_getline(b_us, cite_ystart));
        mc_wlocate(b_us, 0, cite_y);
        break;
      case K_ESC:
        if (!citemode) {
          loop = 0;
          break;
        }
        if (cite_ystart == 1000000) {
          citemode = 0;
          hline = hline0;
        } else {
          cite_ystart = 1000000;
          strcpy(hline1, _("  CITATION: ENTER=select start line ESC=exit                               "));
        }
        drawcite_whole(b_us, y, cite_ystart, cite_yend);
        mc_wlocate(b_st, 0, 0);
        mc_wprintf(b_st, "%s", hline);
        mc_wredraw(b_st, 1);
        if (citemode)
          mc_wlocate(b_us, 0, cite_y);
        break;
    }
  }
  /* Cleanup. */
  if (citemode)
    do_cite(b_us, cite_ystart, cite_yend);
  mc_wclose(b_us, y == us->histlines ? 0 : 1);
  mc_wclose(b_st, 1);
  mc_wlocate(us, us->curx, us->cury);
  mc_wflush();
  mc_wredraw(us, 1);
}

#ifdef SIGWINCH
/* The window size has changed. Re-initialize. */
static void change_size(int sig)
{
  (void)sig;
  size_changed = 1;
  signal(SIGWINCH, change_size);
}
#endif /*SIGWINCH*/

static void usage(int env_args, int optind, char *mc)
{
  if (env_args >= optind && mc)
    fprintf(stderr, _("Wrong option in environment MINICOM=\"%s\"\n"), mc);
  fprintf(stderr, _("Type \"minicom %s\" for help.\n"), "--help");
  exit(1);
}

/* Give some help information */
static void helpthem(void)
{
  char *mc = getenv("MINICOM");

  printf(_(
    "Usage: %s [OPTION]... [configuration]\n"
    "A terminal program for Linux and other unix-like systems.\n\n"
    "  -b, --baudrate         : set baudrate (ignore the value from config)\n"
    "  -D, --device           : set device name (ignore the value from config)\n"
    "  -s, --setup            : enter setup mode\n"
    "  -o, --noinit           : do not initialize modem & lockfiles at startup\n"
    "  -m, --metakey          : use meta or alt key for commands\n"
    "  -M, --metakey8         : use 8bit meta key for commands\n"
    "  -l, --ansi             : literal; assume screen uses non IBM-PC character set\n"
    "  -L, --iso              : don't assume screen uses ISO8859\n"
    "  -w, --wrap             : Linewrap on\n"
    "  -H, --displayhex       : display output in hex\n"
    "  -z, --statline         : try to use terminal's status line\n"
    "  -7, --7bit             : force 7bit mode\n"
    "  -8, --8bit             : force 8bit mode\n"
    "  -c, --color=on/off     : ANSI style color usage on or off\n"
    "  -a, --attrib=on/off    : use reverse or highlight attributes on or off\n"
    "  -t, --term=TERM        : override TERM environment variable\n"
    "  -S, --script=SCRIPT    : run SCRIPT at startup\n"
    "  -d, --dial=ENTRY       : dial ENTRY from the dialing directory\n"
    "  -p, --ptty=TTYP        : connect to pseudo terminal\n"
    "  -C, --capturefile=FILE : start capturing to FILE\n"
    "  -T, --disabletime      : disable display of online time\n"
    "  -R, --remotecharset    : character set of communication partner\n"
    "  -v, --version          : output version information and exit\n"
    "  configuration          : configuration file to use\n\n"
    "These options can also be specified in the MINICOM environment variable.\n"),
    PACKAGE);
  if (mc) {
    printf(_("This variable is currently set to \"%s\".\n"), mc);
  } else {
    printf(_("This variable is currently unset.\n"));
  }
  printf(_(
    "The configuration directory for the access file and the configurations\n"
    "is compiled to %s.\n\n"
    "Report bugs to <minicom-devel@lists.alioth.debian.org>.\n"), CONFDIR);
}

static void set_addlf(int val)
{
  vt_set(val, -1, -1, -1, -1, -1, -1, -1, -1);
}

/* Toggle linefeed addition.  Can be called through the menu, or by a macro. */
void toggle_addlf(void)
{
  addlf = !addlf;
  set_addlf(addlf);
}

static void set_addcr(int val)
{
  vt_set(-1, -1, -1, -1, -1, -1, -1, -1, val);
}

/* Toggle carriagereturn addition.  Can be called through the menu, or by a macro. */
void toggle_addcr(void)
{
  addcr = !addcr;
  set_addcr(addcr);
}

static void set_local_echo(int val)
{
  vt_set(-1, -1, -1, -1, val, -1 ,-1, -1, -1);
}

/* Toggle local echo.  Can be called through the menu, or by a macro. */
void toggle_local_echo(void)
{
  local_echo = !local_echo;
  set_local_echo(local_echo);
}

static void set_line_timestamp(int val)
{
  vt_set(-1, -1, -1, -1 ,-1, -1, -1, val, -1);
}

/* Toggle host timestamping on/off */
static void toggle_line_timestamp(void)
{
  ++line_timestamp;
  line_timestamp %= TIMESTAMP_LINE_NR_OF_OPTIONS;
  set_line_timestamp(line_timestamp);
}

/* -------------------------------------------- */

static void do_iconv_just_copy(char **inbuf, size_t *inbytesleft,
                               char **outbuf, size_t *outbytesleft)
{
  while (*outbytesleft && *inbytesleft)
    {
      *(*outbuf) = *(*inbuf);
      ++(*outbuf);
      ++(*inbuf);
      --(*outbytesleft);
      --(*inbytesleft);
    }
}

#ifdef HAVE_ICONV
static iconv_t iconv_rem2local;
static int iconv_enabled;

int using_iconv(void)
{
  return iconv_enabled;
}

static void init_iconv(const char *remote_charset)
{
  char local_charset[40];
  char *tmp;

  if (!remote_charset)
    return;

  // try to find some charset that minicom is using
  tmp = getenv("LC_CTYPE");
  if (!tmp)
    tmp = getenv("LC_ALL");
  if (!tmp)
    tmp = getenv("LANG");
  if (!tmp)
    tmp = "ASCII";

  // should be like 'en_US.UTF-8'
  if (strchr(tmp, '.'))
    tmp = strchr(tmp, '.') + 1;

  snprintf(local_charset, sizeof(local_charset),
           "%s//TRANSLIT", tmp);
  local_charset[sizeof(local_charset) - 1] = 0;

  if (0)
    {
      printf("Remote charset: %s\n", remote_charset);
      printf("Local charset: %s\n", local_charset);
    }

  iconv_rem2local = iconv_open(local_charset, remote_charset);
  if (iconv_rem2local != (iconv_t)-1)
    iconv_enabled = 1;
  else
    printf("Activating iconv failed with: %s(%d)\n",
           strerror(errno), errno);
}

void do_iconv(char **inbuf, size_t *inbytesleft,
              char **outbuf, size_t *outbytesleft)
{
  if (!iconv_enabled
      || iconv(iconv_rem2local,
               inbuf, inbytesleft,
               outbuf, outbytesleft) == (size_t)-1)
    {
      do_iconv_just_copy(inbuf, inbytesleft, outbuf, outbytesleft);

      // in case of error re-init conversion descriptor
      // (will this work?)
      if (iconv_enabled)
	iconv(iconv_rem2local, NULL, NULL, NULL, NULL);
    }
}

static void close_iconv(void)
{
  if (iconv_enabled)
    iconv_close(iconv_rem2local);
}
#else

int using_iconv(void)
{
  return 0;
}

static void init_iconv(const char *remote_charset)
{
  (void)remote_charset;
}

void do_iconv(char **inbuf, size_t *inbytesleft,
              char **outbuf, size_t *outbytesleft)
{
  do_iconv_just_copy(inbuf, inbytesleft, outbuf, outbytesleft);
}

static void close_iconv(void)
{
}
#endif
/* -------------------------------------------- */


int main(int argc, char **argv)
{
  int c;			/* Command character */
  int quit = 0;			/* 'q' or 'x' pressed */
  char *s, *bufp;		/* Scratch pointers */
  int doinit = 1;		/* -o option */
  char capname[128];		/* Name of capture file */
  struct passwd *pwd;		/* To look up user name */
  char *use_port;		/* Name of initialization file */
  char *args[20];		/* New argv pointer */
  int argk = 1;			/* New argc */
  char *mc;			/* For 'MINICOM' env. variable */
  int env_args;			/* Number of args in env. variable */
  char *cmd_dial;		/* Entry from the command line. */
  int alt_code = 0;		/* Type of alt key */
  char *cmdline_baudrate = NULL;/* Baudrate given on the command line via -b */
  char *cmdline_device = NULL;  /* Device/Port given on the command line via -D */
  char *remote_charset = NULL;  /* Remote charset given on the command line via -R */
  char pseudo[64];
  /* char* console_encoding = getenv ("LC_CTYPE"); */

  static struct option long_options[] =
  {
    { "setup",         no_argument,       NULL, 's' },
    { "help",          no_argument,       NULL, 'h' },
    { "ptty",          required_argument, NULL, 'p' },
    { "metakey",       no_argument,       NULL, 'm' },
    { "metakey8",      no_argument,       NULL, 'M' },
    { "ansi",          no_argument,       NULL, 'l' },
    { "iso",           no_argument,       NULL, 'L' },
    { "term",          required_argument, NULL, 't' },
    { "noinit",        no_argument,       NULL, 'o' },
    { "color",         required_argument, NULL, 'c' },
    { "attrib",        required_argument, NULL, 'a' },
    { "dial",          required_argument, NULL, 'd' },
    { "statline",      no_argument,       NULL, 'z' },
    { "capturefile",   required_argument, NULL, 'C' },
    { "script",        required_argument, NULL, 'S' },
    { "7bit",          no_argument,       NULL, '7' },
    { "8bit",          no_argument,       NULL, '8' },
    { "version",       no_argument,       NULL, 'v' },
    { "wrap",          no_argument,       NULL, 'w' },
    { "displayhex",    no_argument,       NULL, 'H' },
    { "disabletime",   no_argument,       NULL, 'T' },
    { "baudrate",      required_argument, NULL, 'b' },
    { "device",        required_argument, NULL, 'D' },
    { "remotecharset", required_argument, NULL, 'R' },
    { NULL, 0, NULL, 0 }
  };

  /* initialize locale support */
  setlocale(LC_ALL, "");
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);

  /* Initialize global variables */
  portfd =  -1;
  capfp = NULL;
  docap = 0;
  online = -1;
  linespd = 0;
  stdattr = XA_NORMAL;
  us = NULL;
  addlf = 0;
  addcr = 0;
  line_timestamp = 0;
  wrapln = 0;
  display_hex = 0;
  disable_online_time = 0;
  local_echo = 0;
  strcpy(capname, "minicom.cap");
  lockfile[0] = 0;
  tempst = 0;
  st = NULL;
  us = NULL;
  bogus_dcd = 0;
  usecolor = 0;
  screen_ibmpc = screen_iso = 1;
  useattr = 1;
  strncpy(termtype, getenv("TERM") ? getenv("TERM") : "dumb", sizeof(termtype));
  stdattr = XA_NORMAL;
  use_port = "dfl";
  alt_override = 0;
  scr_name[0] = 0;
  scr_user[0] = 0;
  scr_passwd[0] = 0;
  dial_name = (char *)NULL;
  dial_number = (char *)NULL;
  dial_user = (char *)NULL;
  dial_pass = (char *)NULL;
  size_changed = 0;
  escape = 1;
  cmd_dial = NULL;

  /* fmg 1/11/94 colors (set defaults) */
  /* MARK updated 02/17/95 to be more similiar to TELIX */
  mfcolor = YELLOW;
  mbcolor = BLUE;
  tfcolor = WHITE;
  tbcolor = BLACK;
  sfcolor = WHITE;
  sbcolor = RED;
  st_attr = XA_NORMAL;


  /* If no LANG or LC_ALL is set, fall back to 7bit mode
   * since in 8bit mode we can't have fancy window borders... */
  {
    char *e1 = getenv("LANG"), *e2 = getenv("LC_ALL");
    if ((!e1 || !strcmp("C", e1) || !strcmp("POSIX", e1)) &&
	(!e2 || !strcmp("C", e2) || !strcmp("POSIX", e2)))
      screen_ibmpc = screen_iso = 0;
  }


  /* MARK updated 02/17/95 default history buffer size */
  num_hist_lines = 256;

  /* fmg - but we reset these to F=WHITE, B=BLACK if -b flag found */

  /* Before processing the options, first add options
   * from the environment variable 'MINICOM'.
   */
  args[0] = "minicom";
  if ((mc = getenv("MINICOM")) != NULL) {
    char buf[80];
    strncpy(buf, mc, 80);
    bufp = buf;
    buf[79] = 0;
    while (isspace(*bufp))
      bufp++;
    while (*bufp && argk < 19) {
      for (s = bufp; !isspace(*bufp) && *bufp; bufp++)
	;
      args[argk++] = s;
      while (isspace(*bufp))
	*bufp++ = 0;
    }
  }
  env_args = argk;

  /* Add command - line options */
  for(c = 1; c < argc && argk < 19; c++)
    args[argk++] = argv[c];
  args[argk] = NULL;

  do {
    /* Process options with getopt */
    while ((c = getopt_long(argk, args, "v78zhlLsomMHb:wTc:a:t:d:p:C:S:D:R:",
                            long_options, NULL)) != EOF)
      switch(c) {
	case 'v':
	  printf(_("%s version %s"), PACKAGE, VERSION);
#ifdef __DATE__
	  printf(_(" (compiled %s)"), __DATE__);
#endif
	  printf("\n");
	  printf(_("Copyright (C) Miquel van Smoorenburg.\n\n"));
	  printf("This program is free software; you can redistribute it and/or\n"
	         "modify it under the terms of the GNU General Public License\n"
                 "as published by the Free Software Foundation; either version\n"
                 "2 of the License, or (at your option) any later version.\n\n");
          exit(1);
          break;
        case 's': /* setup mode */
          dosetup = 1;
          break;
        case 'h':
          helpthem();
          exit(1);
          break;
        case 'p': /* Pseudo terminal to use. */
          if (strncmp(optarg, "/dev/", 5) == 0)
            optarg += 5;
          if (((strncmp(optarg, "tty", 3) != 0)
                /* /dev/pts support --misiek. */
                && (strncmp(optarg, "pts", 3) != 0)
                /* /dev/pty support --jl. */
                && (strncmp(optarg, "pty", 3) != 0))
              || !strchr("pqrstuvwxyz/", optarg[3])) {
            fprintf(stderr, _("minicom: argument to -p must be a pty\n"));
            exit(1);
          }
          snprintf(pseudo, sizeof(pseudo), "/dev/%s", optarg);
          dial_tty = pseudo;
          break;
        case 'm': /* ESC prefix metakey */
          alt_override++;
          alt_code = 27;
          break;
        case 'M': /* 8th bit metakey. */
          alt_override++;
          alt_code = 128;
          break;
        case 'l': /* Don't assume literal ANSI chars */
          screen_ibmpc = 0;
          break;
        case 'L': /* Don't assume literal ISO8859 chars */
          screen_iso = 0;
          break;
        case 't': /* Terminal type */
          strncpy(termtype, optarg, sizeof(termtype));
#ifdef __GLIBC__
          /* Bug in older libc's (< 4.5.26 I think) */
          if ((s = getenv("TERMCAP")) != NULL && *s != '/')
            unsetenv("TERMCAP");
#endif
          break;
        case 'o': /* DON'T initialize */
          doinit = 0;
          break;
        case 'c': /* Color on/off */
          if (strcmp("on", optarg) == 0) {
            usecolor = 1;
            stdattr = XA_BOLD;
            break;
          }
          if (strcmp("off", optarg) == 0) {
            usecolor = 0;
            stdattr = XA_NORMAL;
            break;
          }
          usage(env_args, optind - 1, mc);
          break;
        case 'a': /* Attributes on/off */
          if (strcmp("on", optarg) == 0) {
            useattr = 1;
            break;
          }
          if (strcmp("off", optarg) == 0) {
            useattr = 0;
            break;
          }
          usage(env_args, optind - 1, mc);
          break;
        case 'd': /* Dial from the command line. */
          cmd_dial = optarg;
          break;
        case 'z': /* Enable status line. */
          use_status = 1;
          break;
        case 'C': /* Capturing */
          capfp = fopen(optarg, "a");
          if (capfp == NULL) {
            werror(_("Cannot open capture file"));
            exit(1);
          }
          docap = 1;
          vt_set(addlf, -1, docap, -1, -1, -1, -1, -1, addcr);
          break;
        case 'S': /* start Script */
          strncpy(scr_name, optarg, 33);
          break;
        case '7': /* 7bit fallback mode */
	  screen_ibmpc = screen_iso = 0;
          break;
	case '8': /* force 8bit mode */
	  screen_ibmpc = screen_iso = 1;
	  break;
        case 'w': /* Linewrap on */
          wrapln = 1;
          break;
        case 'H': /* Display in hex */
          display_hex = 1;
          break;
        case 'T': /* disable online time */
          disable_online_time = 1;
          break;
	case 'b':
	  cmdline_baudrate = optarg;
	  break;
	case 'D':
	  cmdline_device = optarg;
	  break;
	case 'R':
	  remote_charset = optarg;
	  break;
        default:
          usage(env_args, optind, mc);
          break;
      }

    /* Now, get portname if mentioned. Stop at end or '-'. */
    while (optind < argk && args[optind][0] != '-')
      use_port = args[optind++];

    /* Loop again if more options */
  } while (optind < argk);

  init_iconv(remote_charset);

  if (screen_iso && screen_ibmpc)
    /* init VT */
    vt_set(-1, -1, -1, -1, -1, -1, 1, -1, -1);

  /* Avoid fraude ! */	
  for (s = use_port; *s; s++)
    if (*s == '/')
      *s = '_';
  snprintf(parfile, sizeof(parfile), "%s/minirc.%s", CONFDIR, use_port);

  /* Get password file information of this user. */
  if ((pwd = getpwuid(getuid())) == NULL) {
    fputs(_("You don't exist. Go away.\n"), stderr);
    exit(1);
  }

  /* Remember home directory and username. */
  if ((s = getenv("HOME")) == NULL)
    strncpy(homedir, pwd->pw_dir, sizeof(homedir));
  else
    strncpy(homedir, s, sizeof(homedir));
  strncpy(username, pwd->pw_name, sizeof(username));

  /* Get personal parameter file */
  snprintf(pparfile, sizeof(pparfile), "%s/.minirc.%s", homedir, use_port);

  read_parms();
  num_hist_lines = atoi(P_HISTSIZE);
  strcpy(logfname,P_LOGFNAME);

  /* Set default terminal behaviour */
  addlf      = strcasecmp(P_ADDLINEFEED, "yes") == 0;
  local_echo = strcasecmp(P_LOCALECHO,   "yes") == 0;
  addcr      = strcasecmp(P_ADDCARRIAGERETURN, "yes") == 0;

  /* -w overrides config file */
  if (!wrapln)
    wrapln = strcasecmp(P_LINEWRAP, "yes") == 0;

  /* -H overrides config file */
  if (!display_hex)
    display_hex = strcasecmp(P_DISPLAYHEX, "yes") == 0;

  /* After reading in the config via read_parms we can possibly overwrite
   * the baudrate with a value given at the cmdline */
  if (cmdline_baudrate) {
    unsigned int b = strtol(cmdline_baudrate, (char **)NULL, 0);
    if (speed_valid(b)) {
      snprintf(P_BAUDRATE, sizeof(P_BAUDRATE), "%d", b);
      P_BAUDRATE[sizeof(P_BAUDRATE) - 1] = 0;
    }
  }

  /* Now we can also overwrite the device name, if one was given */
  if (cmdline_device) {
    strncpy(P_PORT, cmdline_device, sizeof(P_PORT));
    P_PORT[sizeof(P_PORT) - 1] = 0;
  }

  stdwin = NULL; /* It better be! */

  /* Reset colors if we don't use 'em. */
  if (!usecolor) {
    mfcolor = tfcolor = sfcolor = WHITE;
    mbcolor = tbcolor = sbcolor = BLACK;
    st_attr = XA_REVERSE;
  }

  if (dial_tty == NULL) {
    if (!dosetup) {
      while ((dial_tty = get_port(P_PORT)) != NULL && open_term(doinit, 1, 0) < 0)
        ;
      if (dial_tty == NULL)
        exit(1);
    }
  }
  else {
    if (!dosetup && open_term(doinit, 1, 0) < 0)
      exit(1);
  }

  mc_setenv("TERM", termtype);

  if (win_init(tfcolor, tbcolor, XA_NORMAL) < 0)
    leave("");

  if (COLS < 40 || LINES < 10)
    leave(_("Sorry. Your screen is too small.\n"));

  if (dosetup) {
    if (config(1)) {
      mc_wclose(stdwin, 1);
      exit(0);
    }
    while ((dial_tty = get_port(P_PORT)) != NULL && open_term(doinit, 1, 0) < 0)
      ;
    if (dial_tty == NULL)
      exit(1);
  }

  /* Signal handling */
  signal(SIGTERM, hangsig);
  signal(SIGHUP, hangsig);
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);

#ifdef SIGTSTP
  signal(SIGTSTP, shjump);
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
#endif
#ifdef SIGTINT
  signal(SIGTINT, SIG_IGN);
#endif
#ifdef SIGWINCH
  signal(SIGWINCH, change_size);
#endif

#ifdef DEBUG
  for(c = 1; c < _NSIG; c++) {
    if (c == SIGTERM)
      continue; /* Saviour when hung */
    signal(c, signore);
  }
#endif

  keyboard(KINSTALL, 0);

  if (strcmp(P_BACKSPACE, "BS") != 0)
    keyboard(KSETBS, P_BACKSPACE[0] == 'B' ? 8 : 127);
  if (alt_override)
    keyboard(KSETESC, alt_code);
  else if (strcmp(P_ESCAPE, "^A") != 0) {
    switch (P_ESCAPE[0]) {
      case '^':
        c = P_ESCAPE[1] & 31;
        break;
      case 'E':
        c = 27;
        break;
      default:
        c = 128;
        break;
    }
    keyboard(KSETESC, c);
  }

  st = NULL;
  us = NULL;

  init_emul(VT100, 1);

  if (doinit)
    modeminit();

  mc_wprintf(us, "\n%s %s\r\n", _("Welcome to minicom"), VERSION);
  mc_wprintf(us, "\n%s: %s\r\n", _("OPTIONS"), option_string);
#if defined (__DATE__) && defined (__TIME__)
  mc_wprintf(us, "%s %s, %s.\r\n",_("Compiled on"), __DATE__,__TIME__);
#endif
  {
    struct stat st;
    char port_date[20] = "";
    if (stat(P_PORT, &st) == 0)
      {
	time_t t = time(NULL);
        struct tm tm;
        if (   st.st_mtime + 20 * 60 * 60 > t
            && localtime_r(&st.st_mtime, &tm))
            {
              strftime(port_date, sizeof(port_date), ", %T", &tm);
              port_date[sizeof(port_date) - 1] = 0;
            }
      }
    mc_wprintf(us, "%s %s%s\r\n", _("Port"), P_PORT, port_date);
  }
  if (using_iconv())
    mc_wprintf(us, "%s\r\n", _("Using character set conversion"));
  mc_wprintf(us, _("\nPress %sZ for help on special keys%c\n\n"),esc_key(),'\r');

  readdialdir();

  if (scr_name[0])
    runscript (0, scr_name, "", "");

  if (cmd_dial)
    dialone(cmd_dial);

  set_local_echo(local_echo);
  set_addlf(addlf);
  set_line_timestamp(line_timestamp);

  /* The main loop calls do_terminal and gets a function key back. */
  while (!quit) {
    c = do_terminal();
dirty_goto:
    switch (c + 32 *(c >= 'A' && c <= 'Z')) {
      case 'a': /* Add line feed */
        toggle_addlf();
        s = addlf ?  _("Add linefeed ON") : _("Add linefeed OFF");
        status_set_display(s, 0);
        break;
      case 'u': /* Add carriage return */
        toggle_addcr();
        s = addcr ?  _("Add carriage return ON") : _("Add carriage return OFF");
        status_set_display(s, 0);
        break;
      case 'e': /* Local echo on/off. */
        toggle_local_echo();
        s = local_echo ?  _("Local echo ON") : _("Local echo OFF");
        status_set_display(s, 0);
        break;
      case 'z': /* Help */
        c = help();
        if (c != 'z')
          goto dirty_goto;
        break;
      case 'c': /* Clear screen */
        mc_winclr(us);
        break;
      case 'f': /* Send break */
        sendbreak();
        break;
      case 'b': /* Scroll back */
        scrollback();
        break;
      case 'm': /* Initialize modem */
        if (P_HASDCD[0]=='Y' && online >= 0) {
          c = ask(_("You are online. Really initialize?"), c1);
          if (c != 0)
            break;
        }
        modeminit();
        break;
      case 'q': /* Exit without resetting */
        c = ask(_("Leave without reset?"), c1);
        if (c == 0)
          quit = NORESET;
        if (!strcmp(P_MACCHG,"CHANGED")) {
          c = ask (_("Save macros?"),c1);
          if (c == 0)
            if (dodflsave() < 0) { /* fmg - error */
              c = 'O'; /* hehe */
              quit = 0;
              goto dirty_goto;
            }
        }
        break;
      case 'x': /* Exit Minicom */
        c = ask(_("Leave Minicom?"), c1);
        if (c == 0) {
          quit = RESET;
          if(online >= 0)
            do_hang(0);
          modemreset();
        }
        if (!strcmp(P_MACCHG,"CHANGED")) {
          c = ask (_("Save macros?"), c1);
          if (c == 0)
            if (dodflsave() < 0) { /* fmg - error */
              c = 'O'; /* hehe */
              quit = 0;
              goto dirty_goto;
            }
        }
        break;
      case 'l': /* Capture file */
        if (capfp == (FILE *)0 && !docap) {
          s = input(_("Capture to which file? "), capname);
          if (s == NULL || *s == 0)
            break;
          if ((capfp = fopen(s, "a")) == (FILE *)NULL) {
            werror(_("Cannot open capture file"));
            break;
          }
          docap = 1;
        } else if (capfp != (FILE *)0 && !docap) {
          c = ask(_("Capture file"), c3);
          if (c == 0) {
            fclose(capfp);
            capfp = (FILE *)NULL;
            docap = 0;
          }
          if (c == 1)
            docap = 1;
        } else if (capfp != (FILE *)0 && docap) {
          c = ask(_("Capture file"), c2);
          if (c == 0) {
            fclose(capfp);
            capfp = (FILE *)NULL;
            docap = 0;
          }
          if (c == 1)
            docap = 0;
        }
        vt_set(addlf, -1, docap, -1, -1, -1, -1, -1, addcr);
        break;
      case 'p': /* Set parameters */
        get_bbp(P_BAUDRATE, P_BITS, P_PARITY, P_STOPB, 0);
        port_init();
        if (st)
          mode_status();
        quit = 0;
        break;
      case 'k': /* Run kermit */
        kermit();
        break;
      case 'h': /* Hang up */
        do_hang(1);
        break;
      case 'd': /* Dial */
        dialdir();
        break;
      case 't': /* Terminal emulation */
        c = dotermmenu();
        if (c > 0)
          init_emul(c, 1);
        break;
      case 'w': /* Line wrap on-off */
        c = !us->wrap;
        vt_set(addlf, c, docap, -1, -1, -1, -1, -1, addcr);
        s = c ? _("Linewrap ON") : _("Linewrap OFF");
	status_set_display(s, 0);
        break;
      case 'n': /* Line timestamp */
	toggle_line_timestamp();
        switch (line_timestamp)
          {
          default:
          case TIMESTAMP_LINE_OFF:
            s = _("Timestamp OFF");
            break;
          case TIMESTAMP_LINE_SIMPLE:
            s = _("Timestamp every line (simple)");
            break;
          case TIMESTAMP_LINE_EXTENDED:
            s = _("Timestamp every line (extended)");
            break;
          case TIMESTAMP_LINE_PER_SECOND:
            s = _("Timestamp lines every second");
            break;
          }
        status_set_display(s, 0);
        break;
      case 'o': /* Configure Minicom */
        (void) config(0);
        break;
      case 's': /* Upload */
        updown('U', 0);
        break;
      case 'r': /* Download */
        updown('D', 0);
        break;
      case 'j': /* Jump to a shell */
        shjump(0);
        break;
      case 'g': /* Run script */
        runscript(1, "", "", "");
        break;
      case 'i': /* Re-init, re-open portfd. */
        cursormode = (cursormode == NORMAL) ? APPL : NORMAL;
        keyboard(cursormode == NORMAL ? KCURST : KCURAPP, 0);
        curs_status();
        break;
      case 'y': /* Paste file */
	paste_file();
	break;
      case EOF: /* Cannot read from stdin anymore, exit silently */
        quit = NORESET;
        break;
      default:
        break;
    }
  };

  /* Reset parameters */
  if (quit != NORESET)
    m_restorestate(portfd);
  else {
    if (P_LOGCONN[0] == 'Y' && online > 0)
      do_log("%s", _("Quit without reset while online."));
    m_hupcl(portfd, 0);
  }

  signal(SIGTERM, SIG_IGN);
  signal(SIGHUP, SIG_IGN);
#ifdef SIGTSTP
  signal(SIGTSTP, SIG_DFL);
#endif
  signal(SIGQUIT, SIG_DFL);

  if (capfp != (FILE *)0)
    fclose(capfp);
  mc_wclose(us, 0);
  mc_wclose(st, 0);
  mc_wclose(stdwin, 1);
  keyboard(KUNINSTALL, 0);
  lockfile_remove();
  close(portfd);

  if (quit != NORESET && P_CALLIN[0])
    fastsystem(P_CALLIN, NULL, NULL, NULL);

  close_iconv();

  return 0;
}
