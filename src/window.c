/*
 * window.c	Very portable window routines.
 *		Currently this code is used in _both_ the BBS
 *		system and minicom.
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
 * 01.01.98 dickey@clark.net: fix for a history window closing bug
 * fmg 8/20/97: Added support for Search of History Buffer
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <limits.h>
#include <stdarg.h>
#include <wchar.h>

#include "port.h"
#include "minicom.h"
#include "intl.h"

/* Status line code on/off. */
#define ST_LINE 1

/* fmg 2/20/94 macros - Length of Macros */

#ifndef MAC_LEN
#define MAC_LEN 257
#endif

/* Don't want to include all header stuff for three prototypes from sysdep.c */
#ifdef __STDC__
  int setcbreak(int);
  int wxgetch(void);
  void getrowcols(int *rows, int *cols);
#else
  int setcbreak();
  int wxgetch();
  void getrowcols();
#endif

#ifndef BBS
#include "config.h"
#endif

#define BUFFERSIZE 2048

#define swap(x, y) { int d = (x); (x) = (y); (y) = d; }

/* Terminal capabilities */
static const char *CM, *IS, *RS, *AC, *EA;
static const char *ME, *SE, *UE, *AE;
static const char *AS, *MB, *MD, *MR, *SO, *US;
static const char *CE, *Al, *Dl, *AL, *DL;
static const char *CS, *SF, *SR, *VB, *BL;
static const char *VE, *VI, *KS, *KE;
static const char *CD, *CL, *IC, *DC;
static const char *CR, *NL;
#ifdef ST_LINE
static const char *TS, *FS, *DS;
#endif

/* Special characters */
static char D_UL;
static char D_HOR;
static char D_UR;
static char D_LL;
static char D_VER;
static char D_LR;

static char S_UL;
static char S_HOR;
static char S_UR;
static char S_LL;
static char S_VER;
static char S_LR;

static char _bufstart[BUFFERSIZE];
static char *_bufpos = _bufstart;
static char *_buffend;
static ELM *gmap;

static char curattr = -1;
static char curcolor = -1;
static int curx = -1;
static int cury = -1;
static int _intern = 0;
static int _curstype = CNORMAL;
static int _has_am = 0;
static int _mv_standout = 0;
static ELM oldc;
static int sflag = 0;

/*
 * Smooth is only defined for slow machines running Minicom.
 * With this defined, Minicom will buffer only per-line
 * and the output will look much less 'jerky'. (I hope :-)
 */
#ifdef SMOOTH
static WIN *curwin = NULL;
WIN *us;
#endif

int useattr = 1;
int dirflush = 1;
int LINES, COLS;
int usecolor = 0;
WIN *stdwin;
/*
 * The following is an external pointer to the termcap info.
 * If it's NOT zero then the main program has already
 * read the termcap for us. No sense in doing it twice.
 */
char *_tptr = NULL;
int screen_ibmpc = 0;
int screen_iso = 0;
int w_init = 0;
int use_status = 0; /* Turned on in main() */

/* Standard vt100 map (ac cpability) */
static const char *def_ac = "+\273,\253aaffggjjkkllmmnnooqqssttuuvvwwxx";

#ifdef DEBUG

/*
 * Debug to stdout
 */
int debug(char *s, ...)
{
  char lala[80];
  va_list ap;

  va_start(ap, s);
  vsnprintf(lala, sizeof(lala), s, ap);
  va_end(ap);
  write(2, lala, strlen(lala));
  return 0;
}
#endif

/* ===== Low level routines ===== */

/*
 * Flush the screen buffer
 */
void mc_wflush(void)
{
  int todo, done;

  todo = _bufpos - _bufstart;
  _bufpos = _bufstart;

  while (todo > 0) {
    done = write(1, _bufpos, todo);
    if (done > 0) {
      todo -= done;
      _bufpos += done;
    }
    if (done < 0 && errno != EINTR)
      break;
  }
  _bufpos = _bufstart;
}

/*
 * Output a raw character to the screen
 */
static int outchar(int c)
{
  *_bufpos++ = c;
  if (_bufpos >= _buffend)
    mc_wflush();
#if defined(SMOOTH)
  if (curwin == us && (c == '\n' || c == '\r'))
    mc_wflush();
#endif
  return 0;
}

/*
 * Output a raw string to the screen.
 */
static void outstr(const char *s)
{
  tputs(s, 1, outchar);
}


/*
 * Turn off all attributes
 */
static void _attroff(void)
{
  if (ME)
    outstr(ME);
  else {
    if (SE)
      outstr(SE);
    if (UE)
      outstr(UE);
  }
  if (AE)
    outstr(AE);
}

/*
 * Turn some attributes on
 */
static void _attron(char attr)
{
  if (attr & XA_REVERSE	&& MR)
    outstr(MR);
  if (attr & XA_BOLD && MD)
    outstr(MD);
  if (attr & XA_STANDOUT && SO)
    outstr(SO);
  if (attr & XA_UNDERLINE && US)
    outstr(US);
  if (attr & XA_BLINK && MB)
    outstr(MB);
  if (attr & XA_ALTCHARSET && AS)
    outstr(AS);
}

/*
 * Set the colors
 */
static void _colson(char color)
{
  char buf[12];
  sprintf(buf, "\033[%d;%dm", COLFG(color) + 30, COLBG(color) + 40);
  outstr(buf);
}

/*
 * Set global attributes, if different.
 */
static void _setattr(char attr, char color)
{
  if (!useattr)
    return;

  if (!usecolor) {
    curcolor = color;
    if (attr == curattr)
      return;
    curattr = attr;
    _attroff();
    _attron(attr);
    return;
  }
  if (attr == curattr && color == curcolor)
    return;
  _attroff();
  _colson(color);
  _attron(attr);
  curattr = attr;
  curcolor = color;
}

/*
 * Goto (x, y) in stdwin
 */
static void _gotoxy(int x, int y)
{
  int oldattr = -1;

#ifdef ST_LINE
  int tmp;

  /* Sanity check. */
  if (x >= COLS || y > LINES || (x == curx && y == cury))
    return;

  if (use_status) {
    /* Leaving status line? */
    if (cury == LINES && y < cury) {
      outstr(FS);
      /* Re-set attributes. */
      tmp = curattr;
      curattr = -1;
      _setattr(tmp, curcolor);
      outstr(tgoto(CM, x, y));
      curx = x; cury = y;
      return;
    }
    /* Writing on status line? */
    else if (y == LINES) {
      /* From normal screen? */
      if (cury < y) {
        outstr(tgoto(TS, x, x));
        curx = x;
        cury = y;
        /* Set the right attributes. */
        tmp = curattr;
        curattr = -1;
        _setattr(tmp, curcolor);
        return;
      }
    }
  }
#else
  /* Sanity check. */
  if (x >= COLS || y >= LINES || (x == curx && y == cury)) {
#  if 0
    if (x >= COLS || y >= LINES)
      fprintf(stderr, "OOPS: (x, y) == (%d, %d)\n",
          COLS, LINES);
#  endif
    return;
  }
#endif

  if (!_mv_standout && curattr != XA_NORMAL) {
    oldattr = curattr;
    _setattr(XA_NORMAL, curcolor);
  }
  if (CR != NULL && y == cury && x == 0)
    outstr(CR);
#if 0 /* Hmm, sometimes NL only works in the first column */
  else if (NL != NULL && x == curx && y == cury + 1)
    outstr(NL);
#else
  else if (NL != NULL && x == 0 && x == curx && y == cury + 1)
    outstr(NL);
#endif
  else if (BC != NULL && y == cury && x == curx - 1)
    outstr(BC);
  else
    outstr(tgoto(CM, x, y));
  curx = x;
  cury = y;
  if (oldattr != -1)
    _setattr(oldattr, curcolor);
}

/*
 * Write a character in stdwin at x, y with attr & color
 * 'doit' can be  -1: only write to screen, not to memory
 *                 0: only write to memory, not to screen
 *                 1: write to both screen and memory
 */
static void _write(wchar_t c, int doit, int x, int y, char attr, char color)
{
  ELM *e;

  /* If the terminal has automatic margins, we can't write to the
   * last line, last character. After scrolling, this "invisible"
   * character is automatically restored.
   */
  if (_has_am && y >= LINES - 1 && x >= COLS - 1) {
    doit = 0;
    sflag = 1;
    oldc.value = c;
    oldc.attr = attr;
    oldc.color = color;
  }
#ifdef ST_LINE
  if (x < COLS && y <= LINES)
#else
  if (x < COLS && y < LINES)
#endif
  {
    if (doit != 0) {
      static int x0 = -1, y0 = -1, c0 = 0;
      static char attr0, color0;
      if (x!=x0+1 || y!=y0 || attr!=attr0 || color!=color0 || !(c0&128)) {
        _gotoxy(x, y);
        _setattr(attr, color);
      }
      x0 = x; y0 = y; attr0 = attr; color0 = color; c0 = c;
      if (using_iconv() || (attr & XA_ALTCHARSET) != 0)
        outchar((char)c);
      else {
        char buf[MB_LEN_MAX];
        size_t i, len;

        len = one_wctomb(buf, c);
        for (i = 0; i < (size_t)len; i++)
        outchar(buf[i]);
      }

      curx++;
    }
    if (doit >= 0) {
      e = &gmap[x + y * COLS];
      e->value = c;
      e->attr = attr;
      e->color = color;
    }
  }
}

/*
 * Set cursor type.
 */
static void _cursor(int type)
{
  _curstype = type;

  if (type == CNORMAL && VE != NULL)
    outstr(VE);
  if (type == CNONE && VE != NULL && VI != NULL)
    outstr(VI);
}


/* ==== High level routines ==== */


#if 0
/* This code is functional, but not yet used.
 * It might be one day....
 */
/*
 * Resize a window
 */
void mc_wresize(WIN *win, int lines, int cols)
{
  int x, y;
  ELM *oldmap, *newmap, *e, *n;

  if ((newmap = malloc((lines + 1) * cols * sizeof(ELM))) == NULL)
    return;
  if (win == stdwin)
    oldmap = gmap;
  else
    oldmap = win->map;

  for (y = 0; y < lines; y++)
    for (x = 0; x < cols; x++) {
      n = &newmap[y + x * cols];
      if (x < win->xs && y < win->ys) {
        e = &oldmap[y + x * COLS];
        n->value = e->value;
        n->color = e->color;
        n->attr = e->attr;
      } else {
        n->value = ' ';
        n->color = win->color;
        n->attr = win->attr;
      }
    }
  if (win->sy2 == win->y2)
    win->sy2 = win->y1 + lines - 1;
  win->y2 = win->y1 + lines - 1;
  win->ys = lines;
  win->xs = cols;
  free(oldmap);
  if (win == stdwin) {
    gmap = newmap;
    LINES = lines;
    COLS = cols;
  } else
    win->map = newmap;
}
#endif

/*
 * Create a new window.
 */
WIN *mc_wopen(int x1, int y1, int x2, int y2, int border, int attr,
           int fg, int bg, int direct, int histlines, int doclr)
{
  WIN *w;
  ELM *e;
  int bytes;
  int x, y;
  int color;
  int offs;
  int xattr;
#ifdef SMOOTH
  curwin = NULL;
#endif

  if ((w = malloc(sizeof(WIN))) == NULL)
    return w;

  offs = (border != BNONE);
  if (!screen_ibmpc && AS)
    xattr = attr | XA_ALTCHARSET;
  else
    xattr = attr;

  if (x1 > x2)
    swap(x1, x2);
  if (y1 > y2)
    swap(y1, y2);
  if (x1 < offs)
    x1 = offs;
  if (y1 < offs)
    y1 = offs;
#if 0
  if (x2 >= COLS - offs)
    x2 = COLS - offs - 1;
  if (y2 >= LINES - offs)
    y2 = LINES - offs - 1;
#endif

  w->xs = x2 - x1 + 1;
  w->ys = y2 - y1 + 1;
  w->x1 = x1;
  w->x2 = x2;
  w->y1 = w->sy1 = y1;
  w->y2 = w->sy2 = y2;
  w->doscroll = 1;
  w->border = border;
  w->cursor = CNORMAL;
  w->attr = attr;
  w->autocr = 1;
  w->wrap = 1;
  color = w->color = COLATTR(fg, bg);
  w->curx = 0;
  w->cury = 0;

  w->o_curx = curx;
  w->o_cury = cury;
  w->o_attr = curattr;
  w->o_color = curcolor;
  w->o_cursor = _curstype;
  w->direct = direct;

  if (border != BNONE) {
    x1--;
    x2++;
    y1--;
    y2++;
  }
  /* Store whatever we are overlapping */
  bytes = (y2 - y1 + 1) * (x2 - x1 + 1) * sizeof(ELM) + 100;
  if ((e = malloc(bytes)) == NULL) {
	free(w);
	return NULL;
  }
  w->map = e;
  /* How many bytes is one line */
  bytes = (x2 - x1 + 1) * sizeof(ELM);
  /* Loop */
  for (y = y1; y <= y2; y++) {
    memcpy(e, gmap + COLS * y + x1, bytes);
    e += (x2 - x1 + 1);
  }

  /* Do we want history? */
  w->histline = w->histlines = 0;
  w->histbuf = NULL;
  if (histlines) {
    /* Reserve some memory. */
    bytes = w->xs * histlines * sizeof(ELM);
    if ((w->histbuf = malloc(bytes)) == NULL) {
      free(w->map);
      free(w);
      return NULL;
    }
    w->histlines = histlines;

    /* Clear the history buf. */
    e = w->histbuf;
    for (y = 0; y < w->xs * histlines; y++) {
      e->value = ' ';
      e->attr = attr;
      e->color = color;
      e++;
    }
  }

  /* And draw the window */
  if (border) {
    _write(border == BSINGLE ? S_UL : D_UL, w->direct, x1, y1, xattr, color);
    for (x = x1 + 1; x < x2; x++)
      _write(border == BSINGLE ? S_HOR : D_HOR, w->direct, x, y1, xattr, color);
    _write(border == BSINGLE ? S_UR : D_UR, w->direct, x2, y1, xattr, color);
    for (y = y1 + 1; y < y2; y++) {
      _write(border == BSINGLE ? S_VER : D_VER, w->direct, x1, y, xattr, color);
      for (x = x1 + 1; x < x2; x++)
        _write(' ', w->direct, x, y, attr, color);
      _write(border == BSINGLE ? S_VER : D_VER, w->direct, x2, y, xattr, color);
    }
    _write(border == BSINGLE ? S_LL : D_LL, w->direct, x1, y2, xattr, color);
    for (x = x1 + 1; x < x2; x++)
      _write(border == BSINGLE ? S_HOR : D_HOR, w->direct, x, y2, xattr, color);
    _write(border == BSINGLE ? S_LR : D_LR, w->direct, x2, y2, xattr, color);
    if (w->direct)
      _gotoxy(x1 + 1, y1 + 1);
  } else
    if (doclr)
      mc_winclr(w);
  mc_wcursor(w, CNORMAL);

  if (w->direct)
    mc_wflush();
  return w;
}

/*
 * Close a window.
 */
void mc_wclose(WIN *win, int replace)
{
  ELM *e;
  int x, y;

#ifdef SMOOTH
  curwin = NULL;
#endif

  if (!win)
    return;

  if (win == stdwin) {
    win_end();
    return;
  }
  e = win->map;

  if (win->border) {
    win->x1--;
    win->x2++;
    win->y1--;
    win->y2++;
  }
  mc_wcursor(win, win->o_cursor);
  if (replace) {
    for (y = win->y1; y <= win->y2; y++) {
      /* temporal way to avoid 'half-character' problem */
      /* in multibyte characters such as Japanese -- from here */
      ELM *g;
      g = gmap + (y * stdwin->xs);
      for (x = 0 ; x < win->x1; x++) {
        _write(g->value, 1, x, y, g->attr, g->color);
        g++;
      }
      /* to here */
      for (x = win->x1; x <= win->x2; x++) {
        _write(e->value, 1, x, y, e->attr, e->color);
        e++;
      }
    }
    _gotoxy(win->o_curx, win->o_cury);
    _setattr(win->o_attr, win->o_color);
  }
  free(win->map);
  if (win->histbuf)
    free(win->histbuf);
  free(win);	/* 1.1.98 dickey@clark.net  */
  mc_wflush();
}

static int oldx, oldy;
static int ocursor;

/*
 * Clear screen & restore keyboard modes
 */
void mc_wleave(void)
{
  oldx = curx;
  oldy = cury;
  ocursor = _curstype;

  setcbreak(0); /* Normal */
  _gotoxy(0, LINES - 1);
  _setattr(XA_NORMAL, COLATTR(WHITE, BLACK));
  _cursor(CNORMAL);
  if (CL != NULL)
    outstr(CL);
  else
    outstr("\n");
#ifdef ST_LINE
  if (DS)
    outstr(DS);
#endif
  if (KE != NULL)
    outstr(KE);
  if (RS != NULL)
    outstr(RS);
  mc_wflush();
}

void mc_wreturn(void)
{
  int x, y;
  ELM *e;

#ifdef SMOOTH
  curwin = NULL;
#endif

  curattr = -1;
  curcolor = -1;

  setcbreak(1); /* Cbreak, no echo */

  if (IS != NULL)
    outstr(IS); /* Initialization string */
  if (EA != NULL)
    outstr(EA); /* Graphics init. */
  if (KS != NULL)
    outstr(KS); /* Keypad mode */

  _gotoxy(0, 0);
  _cursor(ocursor);

  e = gmap;
  for (y = 0; y <LINES; y++) {
    for(x = 0; x < COLS; x++) {
      _write(e->value, -1, x, y, e->attr, e->color);
      e++;
    }
  }
  _gotoxy(oldx, oldy);
  mc_wflush();
}

/*
 * Redraw the whole window.
 */
void mc_wredraw(WIN *w, int newdirect)
{
  int minx, maxx, miny, maxy;
  ELM *e;
  int x, y;
  int addcnt;

  minx = w->x1;
  maxx = w->x2;
  miny = w->y1;
  maxy = w->y2;
  addcnt = stdwin->xs - w->xs;

  if (w->border) {
    minx--;
    maxx++;
    miny--;
    maxy++;
    addcnt -= 2;
  }

  _gotoxy(minx, miny);
  _cursor(CNONE);
  e = gmap + (miny * stdwin->xs) + minx;

  for (y = miny; y <= maxy; y++) {
    for(x = minx; x <= maxx; x++) {
      _write(e->value, -1, x, y, e->attr, e->color);
      e++;
    }
    e += addcnt;
  }
  _gotoxy(w->x1 + w->curx, w->y1 + w->cury);
  _cursor(w->cursor);
  mc_wflush();
  w->direct = newdirect;
}

/*
 * Clear to end of line, low level.
 */
static int _wclreol(WIN *w)
{
  int x;
  int doit = 1;
  int y;

#ifdef SMOOTH
  curwin = w;
#endif
  y = w->cury + w->y1;

  if (w->direct && (w->x2 == COLS - 1) && CE) {
    _gotoxy(w->curx + w->x1, y);
    _setattr(w->attr, w->color);
    outstr(CE);
    doit = 0;
  }
  for (x = w->curx + w->x1; x <= w->x2; x++) {
    _write(' ', (w->direct && doit) ? 1 : 0, x, y, w->attr, w->color);
  }
  return doit;
}

/*
 * Scroll a window.
 */
void mc_wscroll(WIN *win, int dir)
{
  ELM *e, *f;
  char *src, *dst;
  int x, y;
  int doit = 1;
  int ocurx, fs = 0, len;
  int phys_scr = 0;

#ifdef SMOOTH
  curwin = win;
#endif

  /*
   * If the window *is* the physical screen, we can scroll very simple.
   * This improves performance on slow screens (eg ATARI ST) dramatically.
   */
  if (win->direct && SF != NULL &&
      (dir == S_UP || SR != NULL) && (LINES == win->sy2 - win->sy1 + 1)) {
    doit = 0;
    phys_scr = 1;
    _setattr(win->attr, win->color);
    if (dir == S_UP) {
      _gotoxy(0, LINES - 1);
      outstr(SF);
    } else {
      _gotoxy(0, 0);
      outstr(SR);
    }
  }
  /*
   * If the window is as wide as the physical screen, we can
   * scroll it with insert/delete line (or set scroll region - vt100!)
   */
  else if (win->direct && win->xs == COLS &&
      ((CS != NULL && SF != NULL && SR != NULL)
       || (Dl != NULL && Al != NULL))) {
    doit = 0;
    phys_scr = 1;
    _setattr(win->attr, win->color);
    if (CS != NULL && SF != NULL && SR != NULL) { /* Scrolling Region */
      /* If the scroll region we want to initialize already is as
       * big as the physical screen, we don't _have_ to
       * initialize it.
       */
      if (win->sy2 == LINES - 1 && win->sy1 == 0)
        fs = 1;
      if (!fs) {
        outstr(tgoto(CS, win->sy2, win->sy1));
        cury = 0;
      }
      if (dir == S_UP) {
        _gotoxy(0, win->sy2);
        outstr(SF);
      } else {
        _gotoxy(0, win->sy1);
        outstr(SR);
      }
      if (!fs) {
        outstr(tgoto(CS, LINES - 1, 0));
        cury = 0;
      }
      _gotoxy(0, win->sy2);
    } else { /* Use insert/delete line */
      if (dir == S_UP) {
        _gotoxy(0, win->sy1);
        outstr(Dl);
        _gotoxy(0, win->sy2);
        outstr(Al);
      } else {
        _gotoxy(0, win->sy2);
        outstr(Dl);
        _gotoxy(0, win->sy1);
        outstr(Al);
      }
    }
  }

  /* If a terminal has automatic margins, we can't write
   * to the lower right. After scrolling we have to restore
   * the non-visible character that is now visible.
   */
  if (sflag && win->sy2 == (LINES - 1) && win->sy1 != win->sy2) {
    if (dir == S_UP) {
      _write(oldc.value, 1, COLS - 1, LINES - 2,
             oldc.attr, oldc.color);
    }
    sflag = 0;
  }

  ocurx = win->curx;

  /* If this window has a history buf, see if we want to use it. */
  if (win->histbuf && dir == S_UP &&
      win->sy2 == win->y2 && win->sy1 == win->y1) {

    /* Calculate screen buffer */
    e = gmap + win->y1 * COLS + win->x1;

    /* Calculate history buffer */
    f = win->histbuf + (win->xs * win->histline);

    /* Copy line from screen to history buffer */
    memcpy((char *)f, (char *)e, win->xs * sizeof(ELM));

    /* Postion the next line in the history buffer */
    win->histline++;
    if (win->histline >= win->histlines)
      win->histline = 0;
  }

  /* If the window is screen-wide and has no border, there
   * is a much simpler & FASTER way of scrolling the memory image !!
   */
  if (phys_scr) {
    len = (win->sy2 - win->sy1) * win->xs * sizeof(ELM);
    if (dir == S_UP)  {
      dst = (char *)&gmap[0];				/* First line */
      src = (char *)&gmap[win->xs];			/* Second line */
      win->cury = win->sy2 - win->y1;
    } else {
      src = (char *)&gmap[0];				/* First line */
      dst = (char *)&gmap[win->xs];			/* Second line */
      win->cury = win->sy1 - win->y1;
    }
    /* memmove copies len bytes from src to dst, even if the
     * objects overlap.
     */
    fflush(stdout);
#ifdef _SYSV
    memcpy((char *)dst, (char *)src, len);
#else
#  ifdef _BSD43
    bcopy((char *)src, (char *)dst, len);
#  else
    memmove((char *)dst, (char *)src, len);
#  endif
#endif
  } else {
    /* Now scroll the memory image. */
    if (dir == S_UP) {
      for (y = win->sy1 + 1; y <= win->sy2; y++) {
        e = gmap + y * COLS + win->x1;
        for (x = win->x1; x <= win->x2; x++) {
          _write(e->value, win->direct && doit, x, y - 1, e->attr, e->color);
          e++;
        }
      }
      win->curx = 0;
      win->cury = win->sy2 - win->y1;
      if (doit)
        _wclreol(win);
    } else {
      for (y = win->sy2 - 1; y >= win->sy1; y--) {
        e = gmap + y * COLS + win->x1;
        for (x = win->x1; x <= win->x2; x++) {
          _write(e->value, win->direct && doit, x, y + 1, e->attr, e->color);
          e++;
        }
      }
      win->curx = 0;
      win->cury = win->sy1 - win->y1;
      if (doit)
        _wclreol(win);
    }
  }

  win->curx = ocurx;

  if (!doit)
    for (x = win->x1; x <= win->x2; x++)
      _write(' ', 0, x, win->y1 + win->cury, win->attr, win->color);
  if (!_intern && win->direct)
    _gotoxy(win->x1 + win->curx, win->y1 + win->cury);
  if (dirflush && !_intern && win->direct)
    mc_wflush();
}

/*
 * Locate the cursor in a window.
 */
void mc_wlocate(WIN *win, int x, int y)
{
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;
  if (x >= win->xs)
    x = win->xs - 1;
  if (y >= win->ys)
    y = win->ys - 1;

  win->curx = x;
  win->cury = y;
  if (win->direct)
    _gotoxy(win->x1 + x, win->y1 + y);

  if (dirflush)
    mc_wflush();
}

/*
 * Print a character in a window.
 */
void mc_wputc(WIN *win, wchar_t c)
{
  int mv = 0;

#ifdef SMOOTH
  curwin = win;
#endif

  switch(c) {
    case '\r':
      win->curx = 0;
      mv++;
      break;
    case '\b':
      if (win->curx == 0)
        break;
      win->curx--;
      mv++;
      break;
    case '\007':
      mc_wbell();
      break;
    case '\t':
      do {
        mc_wputc(win, ' '); /* Recursion! */
      } while (win->curx % 8);
      break;
    case '\n':
      if (win->autocr)
        win->curx = 0;
      /*FALLTHRU*/
    default:
      /* See if we need to scroll/move. (vt100 behaviour!) */
      if (c == '\n' || (win->curx >= win->xs && win->wrap)) {
        if (c != '\n')
          win->curx = 0;
        win->cury++;
        mv++;
        if (win->cury == win->sy2 - win->y1 + 1) {
          if (win->doscroll)
            mc_wscroll(win, S_UP);
          else
            win->cury = win->sy1 - win->y1;
        }
        if (win->cury >= win->ys)
          win->cury = win->ys - 1;
      }
      /* Now write the character. */
      if (c != '\n') {
	if (!win->wrap && win->curx >= win->xs)
	  c = '>';
        _write(c, win->direct, win->curx + win->x1,
               win->cury + win->y1, win->attr, win->color);
        if (++win->curx >= win->xs && !win->wrap) {
          win->curx--;
          curx = 0; /* Force to move */
          mv++;
        }
      }
      break;
  }
  if (mv && win->direct)
    _gotoxy(win->x1 + win->curx, win->y1 + win->cury);

  if (win->direct && dirflush && !_intern)
    mc_wflush();
}

/* Draw one line in a window */
void mc_wdrawelm(WIN *w, int y, ELM *e)
{
  int x;

  /* MARK updated 02/17/94 - Fixes bug, to do all 80 cols, not 79 cols */
  for (x = w->x1; x <= w->x2; x++) {
    _write(e->value, w->direct, x, y + w->y1, e->attr, e->color);
    /*y + w->y1, XA_NORMAL, e->color);*/
    e++;
  }
}

/*
 * fmg 8/20/97
 * 'accumulate' one line of ELM's into a string
 * WHY: need this in search function to see if line contains search pattern
 */
void mc_wdrawelm_var(WIN *w, ELM *e, wchar_t *buf)
{
  int x, c = 0;

  /* MARK updated 02/17/94 - Fixes bug, to do all 80 cols, not 79 cols */
  for (x = w->x1; x <= w->x2; x++) {
    buf[c++] = e->value;
    e++;
  }
}

/*
 * fmg 8/20/97
 * 'draw' one line of ELM's in a window INVERTED (text-mode-wise)
 * WHY: need this in search function to see if line contains search pattern
 */
void mc_wdrawelm_inverse(WIN *w, int y, ELM *e)
{
  int x;

  /* MARK updated 02/17/94 - Fixes bug, to do all 80 cols, not 79 cols */
  /* filipg 8/19/97: this will BOLD-up the line */
  /* first position */
  x = w->x1;
  _write(e->value, w->direct, x, y + w->y1, XA_NORMAL, e->color);

  e++;

  /* everything in the middle will be BLINK */
  for (x = w->x1 + 1; x <= w->x2 - 1; x++) {
    _write(e->value, w->direct, x, y + w->y1, XA_BOLD, WHITE);
    e++;
  }

  /* last position */
  x = w->x2;
  _write(e->value, w->direct, x, y + w->y1, XA_NORMAL, e->color);
}

/*
 * Print a string in a window.
 */
void mc_wputs(WIN *win, const char *s)
{
  _intern = 1;

  while (*s) {
    wchar_t wc;

    s += one_mbtowc(&wc, s, MB_LEN_MAX);
    mc_wputc(win, wc);
  }
  if (dirflush && win->direct)
    mc_wflush();
  _intern = 0;
}

/*
 * Print a formatted string in a window.
 * Should return stringlength - but who cares.
 */
int mc_wprintf(WIN *win, const char *fmt, ...)
{
  char buf[160];
  va_list va;

  va_start(va, fmt);
  vsnprintf(buf, sizeof(buf), fmt, va);
  va_end(va);
  mc_wputs(win, buf);

  return 0;
}

/*
 * Sound a bell.
 */
void mc_wbell(void)
{
  if (BL != NULL)
    outstr(BL);
  else if (VB != NULL)
    outstr(VB);
  else
    outchar('\007');
  mc_wflush();
}

/*
 * Set cursor type.
 */
void mc_wcursor(WIN *win, int type)
{
  win->cursor = type;
  if (win->direct) {
    _cursor(type);
    if (dirflush)
      mc_wflush();
  }
}

void mc_wtitle(WIN *w, int pos, const char *s)
{
  int x = 0;

#ifdef SMOOTH
  curwin = NULL;
#endif

  if (w->border == BNONE)
    return;

  if (pos == TLEFT)
    x = w->x1;
  if (pos == TRIGHT)
    x = w->x2 - mbslen(s) - 1;
  if (pos == TMID)
    x = w->x1 + (w->xs - mbslen(s)) / 2 - 1;
  if (x < w->x1)
    x = w->x1;

  if (x < w->x2)
    _write('[', w->direct, x++, w->y1 - 1, w->attr, w->color);
  while (*s && x <= w->x2) {
    wchar_t wc;

    s += one_mbtowc(&wc, s, MB_LEN_MAX);
    _write(wc, w->direct, x++, w->y1 - 1, w->attr, w->color);
  }
  if (x <= w->x2)
    _write(']', w->direct, x++, w->y1 - 1, w->attr, w->color);

  if (w->direct) {
    _gotoxy(w->x1 + w->curx, w->y1 + w->cury);
    if (dirflush)
      mc_wflush();
  }
}


/* ==== Menu Functions ==== */

/*
 * Change attributes of one line of a window.
 */
void mc_wcurbar(WIN *w, int y, int attr)
{
  ELM *e;
  int x;

#ifdef SMOOTH
  curwin = w;
#endif

  y += w->y1;

  e = gmap + y * COLS + w->x1;

  /* If we can't do reverse, just put a '>' in front of
   * the line. We only support XA_NORMAL & XA_REVERSE.
   */
  if (!useattr || MR == NULL) {
    if (attr & XA_REVERSE)
      x = '>';
    else
      x = ' ';
    _write(x, w->direct, w->x1, y, attr, e->color);
  } else {
    for (x = w->x1; x <= w->x2; x++) {
      _write(e->value, w->direct, x, y, attr, e->color);
      e++;
    }
  }
  if ((VI == NULL || _curstype == CNORMAL) && w->direct)
    _gotoxy(w->x1, y);
  if (w->direct)
    mc_wflush();
}

/*
 * mc_wselect - select one of many choices.
 */
int mc_wselect(int x, int y, const char *const *choices,
            void (*const *funlist)(void),
            const char *title, int attr, int fg, int bg)
{
  const char *const *a = choices;
  unsigned int len = 0;
  int count = 0;
  int cur = 0;
  int c;
  WIN *w;
  int high_on = XA_REVERSE | attr;
  int high_off = attr;

  /* first count how many, and max. width. */

  while (*a != NULL) {
    count++;
    if (mbslen(_(*a)) > len)
      len = mbslen(_(*a));
    a++;
  }
  if (title != NULL && mbslen(title) + 2 > len)
    len = mbslen(title) + 2;
  if (attr & XA_REVERSE) {
    high_on = attr & ~XA_REVERSE;
    high_off = attr;
  }

  if ((w = mc_wopen(x, y, x + len + 2, y + count - 1,
                 BDOUBLE, attr, fg, bg, 0, 0, 0)) == NULL)
    return -1;
  mc_wcursor(w, CNONE);

  if (title != NULL)
    mc_wtitle(w, TMID, title);

  for (c = 0; c < count; c++)
    mc_wprintf(w, " %s%s", _(choices[c]), c == count - 1 ? "" : "\n");

  mc_wcurbar(w, cur, high_on);
  mc_wredraw(w, 1);

  while (1) {
    while ((c = wxgetch()) != 27 && c != '\n' && c!= '\r' && c != ' ') {
      if (c == K_UP || c == K_DN || c == 'j' || c == 'k' ||
          c == K_HOME || c == K_END)
        mc_wcurbar(w, cur, high_off);
      switch (c) {
        case K_UP:
        case 'k':
          cur--;
          if (cur < 0)
            cur = count - 1;
          break;
        case K_DN:
        case 'j':
          cur++;
          if (cur >= count)
            cur = 0;
          break;
        case K_HOME:
          cur = 0;
          break;
        case K_END:
          cur = count - 1;
          break;
      }
      if (c == K_UP || c == K_DN || c == 'j' || c == 'k' ||
          c == K_HOME || c == K_END)
        mc_wcurbar(w, cur, high_on);
    }
    mc_wcursor(w, CNORMAL);
    if (c == ' ' || c == 27) {
      mc_wclose(w, 1);
      return 0;
    }
    if (funlist == NULL || funlist[cur] == NULL) {
      mc_wclose(w, 1);
      return cur + 1;
    }
    (*funlist[cur])();
    mc_wcursor(w, CNONE);
  }
}


/* ==== Clearing functions ==== */

/*
 * Clear characters.
 */
void mc_wclrch(WIN *w, int n)
{
  int x, y, x_end;

#ifdef SMOOTH
  curwin = w;
#endif

  y = w->cury + w->y1;
  x = x_end = w->curx + w->x1;
  x_end += n - 1;

  if (x_end > w->x2)
    x_end = w->x2;

  if (w->direct)
    _gotoxy(w->x1, y);

  for ( ; x <= x_end; x++)
    _write(' ', w->direct, x, y, w->attr, w->color);

  if (w->direct && dirflush)
    mc_wflush();
}

/*
 * Clear entire line.
 */
void mc_wclrel(WIN *w)
{
  int ocurx = w->curx;

  w->curx = 0;
  _wclreol(w);
  w->curx = ocurx;
  mc_wlocate(w, ocurx, w->cury);
}

/*
 * Clear to end of line.
 */
void mc_wclreol(WIN *w)
{
  if (_wclreol(w) && w->direct)
    _gotoxy(w->x1 + w->curx, w->y1 + w->cury);
  if (dirflush)
    mc_wflush();
}

/*
 * Clear to begin of line
 */
void mc_wclrbol(WIN *w)
{
  int x, y, n;

#ifdef SMOOTH
  curwin = w;
#endif

  y = w->cury + w->y1;

  if (w->direct)
    _gotoxy(w->x1, y);

  n = w->x1 + w->curx;
  if (n > w->x2)
    n = w->x2;
  for (x = w->x1; x <= n; x++)
    _write(' ', w->direct, x, y, w->attr, w->color);
  if (w->direct) {
    _gotoxy(n, y);
    if (dirflush)
      mc_wflush();
  }
}

/*
 * Clear to end of screen
 */
void mc_wclreos(WIN *w)
{
  int y;
  int ocurx, ocury;

  ocurx = w->curx;
  ocury = w->cury;

  w->curx = 0;

  for (y = w->cury + 1; y <= w->y2 - w->y1; y++) {
    w->cury = y;
    _wclreol(w);
  }
  w->curx = ocurx;
  w->cury = ocury;
  if (_wclreol(w) && w->direct)
    _gotoxy(w->x1 + w->curx, w->y1 + w->cury);
  if (dirflush && w->direct)
    mc_wflush();
}

/*
 * Clear to begin of screen.
 */
void mc_wclrbos(WIN *w)
{
  int ocurx, ocury;
  int y;

  ocurx = w->curx;
  ocury = w->cury;

  w->curx = 0;

  for (y = 0; y < ocury; y++) {
    w->cury = y;
    _wclreol(w);
  }
  w->curx = ocurx;
  w->cury = ocury;
  mc_wclrbol(w);
}

/*
 * Clear a window.
 */
void mc_winclr(WIN *w)
{
  int y;
  int olddir = w->direct;
  ELM *e, *f;
  int i;
  int m;

  /* If this window has history, save the image. */
  if (w->histbuf) {
    /* MARK updated 02/17/95 - Scan backwards from the bottom of the */
    /* window for the first non-empty line.  We should save all other */
    /* blank lines inside screen, since some nice BBS ANSI menus */
    /* contains them for cosmetic purposes or as separators. */
    for (m = w->y2; m >= w->y1; m--) {
      /* Start of this line in the global map. */
      e = gmap + m * COLS + w->x1;

      /* Quick check to see if line is empty. */
      for (i = 0; i < w->xs; i++)
        if (e[i].value != ' ')
          break;

      if (i != w->xs)
        break; /* Non empty line */
    }

    /* Copy window into history buffer line-by-line. */
    for (y = w->y1; y <= m; y++) {
      /* Start of this line in the global map. */
      e = gmap + y * COLS + w->x1;

      /* Now copy this line. */
      f = w->histbuf + (w->xs * w->histline); /* History buffer */
      memcpy((char *)f, (char *)e, w->xs * sizeof(ELM));
      w->histline++;
      if (w->histline >= w->histlines)
        w->histline = 0;
    }
  }

  _setattr(w->attr, w->color);
  w->curx = 0;

  if (CL && w->y1 == 0 && w->y2 == LINES-1 && w->x1 == 0 && w->x2 == COLS-1) {
    w->direct = 0;
    curx = 0;
    cury = 0;
    outstr(CL);
  }
  for (y = w->ys - 1; y >= 0; y--) {
    w->cury = y;
    _wclreol(w);
  }
  w->direct = olddir;
  _gotoxy(w->x1, w->y1);
  if (dirflush)
    mc_wflush();
}

/* ==== Insert / Delete functions ==== */

void mc_winsline(WIN *w)
{
  int osy1, osy2;

  osy1 = w->sy1;
  osy2 = w->sy2;

  w->sy1 = w->y1 + w->cury;
  w->sy2 = w->y2;
  if (w->sy1 < osy1)
    w->sy1 = osy1;
  if (w->sy2 > osy2)
    w->sy2 = osy2;
  mc_wscroll(w, S_DOWN);

  w->sy1 = osy1;
  w->sy2 = osy2;
}

void mc_wdelline(WIN *w)
{
  int osy1, osy2;
  int ocury;

  ocury = w->cury;
  osy1 = w->sy1;
  osy2 = w->sy2;

  w->sy1 = w->y1 + w->cury;
  w->sy2 = w->y2;
  if (w->sy1 < osy1)
    w->sy1 = osy1;
  if (w->sy2 > osy2)
    w->sy2 = osy2;

  _intern = 1;
  mc_wscroll(w, S_UP);
  _intern = 0;
  mc_wlocate(w, 0, ocury);

  w->sy1 = osy1;
  w->sy2 = osy2;
}

/*
 * Insert a space at cursor position.
 */
void mc_winschar2(WIN *w, wchar_t c, int move)
{
  int y;
  int x;
  int doit = 1;
  ELM *buf, *e;
  int len, odir;
  int oldx;

#ifdef SMOOTH
  curwin = w;
#endif

  /* See if we need to insert. */
  if (c == 0 || wcschr(L"\r\n\t\b\007", c) || w->curx >= w->xs - 1) {
    mc_wputc(w, c);
    return;
  }

  odir = w->direct;
  if (w->xs == COLS && IC != NULL) {
    /* We can use the insert character capability. */
    if (w->direct)
      outstr(IC);

    /* No need to draw the character if it's a space. */
    if (c == ' ')
      w->direct = 0;

    /* We don't need to draw the new line at all. */
    doit = 0;
  }

  /* Get the rest of line into buffer */
  y = w->y1 + w->cury;
  x = w->x1 + w->curx;
  oldx = w->curx;
  len = w->xs - w->curx;

  buf = malloc(sizeof(ELM) * len);
  if (!buf)
    return; /* Umm... */
  memcpy(buf, gmap + COLS * y + x, sizeof(ELM) * len);

  /* Now, put the new character on screen. */
  mc_wputc(w, c);
  if (!move)
    w->curx = oldx;

  /* Write buffer to screen */
  e = buf;
  for (++x; x <= w->x2; x++) {
    _write(e->value, doit && w->direct, x, y, e->attr, e->color);
    e++;
  }
  free(buf);

  w->direct = odir;
  mc_wlocate(w, w->curx, w->cury);
}

void mc_winschar(WIN *w)
{
  mc_winschar2(w, ' ', 0);
}

/*
 * Delete character under the cursor.
 */
void mc_wdelchar(WIN *w)
{
  int x, y;
  int doit = 1;
  ELM *e;

#ifdef SMOOTH
  curwin = w;
#endif

  x = w->x1 + w->curx;
  y = w->y1 + w->cury;

  if (w->direct && w->xs == COLS && DC != NULL) {
    /*_gotoxy(x - 1, y);*/
    _gotoxy(x, y);
    outstr(DC);
    doit = 0;
  }

  e = gmap + y * COLS + x + 1;

  for (; x < w->x2; x++) {
    _write(e->value, doit && w->direct, x, y, e->attr, e->color);
    e++;
  }
  _write(' ', doit && w->direct, x, y, w->attr, w->color);
  mc_wlocate(w, w->curx, w->cury);
}

/* ============= Support: edit a line on the screen. ============ */

/* Redraw the line we are editting. */
static void lredraw(WIN *w, int x, int y, wchar_t *s, int len)
{
  int i, f;

  i = 0;
  mc_wlocate(w, x, y);
  for (f = 0; f < len; f++) {
    if (s[f] == 0)
      i++;
    mc_wputc(w, i ? ' ' : s[f]);
  }
}

#if MAC_LEN > 256
#  define BUFLEN MAC_LEN
#else
#  define BUFLEN 256
#endif

/* mc_wgetwcs - edit one line in a window. */
int mc_wgetwcs(WIN *w, wchar_t *s, int linelen, int maxlen)
{
  int c;
  int idx;
  int offs = 0;
  int f, st = 0, i;
  wchar_t buf[BUFLEN];
  int quit = 0;
  int once = 1;
  int x, y, r;
  int direct = dirflush;
  int delete = 1;

  x = w->curx;
  y = w->cury;

  i = w->xs - x;
  if (linelen >= i - 1)
    linelen = i - 1;

  /* We assume the line has already been drawn on the screen. */
  if ((idx = wcslen(s)) > linelen)
    idx = linelen;
  wcsncpy(buf, s, sizeof(buf) / sizeof(*buf));
  mc_wlocate(w, x + idx, y);
  dirflush = 0;
  mc_wflush();

  while (!quit) {
    if (once) {
      c = K_END;
      once--;
    } else {
      c = wxgetch();
      if (c > 255 || c == K_BS || c == K_DEL)
        delete = 0;
    }
    switch(c) {
      case '\r':
      case '\n':
        st = 0;
        quit = 1;
        break;
      case K_ESC: /* Exit without changing. */
        mc_wlocate(w, x, y);
        lredraw(w, x, y, s, linelen);
        mc_wflush();
        st = -1;
        quit = 1;
        break;
      case K_HOME: /* Home */
        r = offs > 0;
        offs = 0;
        idx = 0;
        if (r)
          lredraw(w, x, y, buf, linelen);
        mc_wlocate(w, x, y);
        mc_wflush();
        break;
      case K_END: /* End of line. */
        idx = wcslen(buf);
        r = 0;
        while (idx - offs > linelen) {
          r = 1;
          offs += 4;
        }
        if (r)
          lredraw(w, x, y, buf + offs, linelen);
        mc_wlocate(w, x + idx - offs, y);
        mc_wflush();
        break;
      case K_LT: /* Cursor left. */
      case K_BS: /* Backspace is first left, then DEL. */
        if (idx == 0)
          break;
        idx--;
        if (idx < offs) {
          offs -= 4;
          /*if (c == K_LT) FIXME? */
          lredraw(w, x, y, buf + offs, linelen);
        }
        if (c == K_LT) {
          mc_wlocate(w, x + idx - offs, y);
          mc_wflush();
          break;
        }
        /*FALLTHRU*/
      case K_DEL: /* Delete character under cursor. */
        if (buf[idx] == 0)
          break;
        for (f = idx; buf[f]; f++)
          buf[f] = buf[f+1];
        lredraw(w, x + idx - offs, y, buf + idx, linelen - (idx - offs));
        mc_wlocate(w, x + idx - offs, y);
        mc_wflush();
        break;
      case K_RT:
        if (buf[idx] == 0)
          break;
        idx++;
        if (idx - offs > linelen) {
          offs += 4;
          lredraw(w, x, y, buf + offs, linelen);
        }
        mc_wlocate(w, x + idx - offs, y);
        mc_wflush();
        break;
      default:
        /* If delete == 1, delete the buffer. */
        if (delete) {
          if ((i = wcslen(buf)) > linelen)
            i = linelen;
          buf[0] = 0;
          idx = 0;
          offs = 0;
          mc_wlocate(w, x, y);
          for (f = 0; f < i; f++)
            mc_wputc(w, ' ');
          delete = 0;
        }

        /* Insert character at cursor position. */
        if (c < 32 || c > 255)
          break;
	f = wcslen(buf) + 2;
	if (f >= maxlen)
	  break;
	while (f > idx) {
          buf[f] = buf[f-1];
	  f--;
	}
        buf[idx] = c;
        if (idx - offs >= linelen) {
          offs += 4;
          lredraw(w, x, y, buf + offs, linelen);
        } else
          lredraw(w, x + idx - offs, y, buf + idx, linelen - (idx - offs));
        idx++;
        mc_wlocate(w, x + idx - offs, y);
        mc_wflush();
        break;
    }
  }
  if (st == 0)
    wcscpy(s, buf);
  dirflush = direct;
  return st;
}

/* mc_wgets - edit one line in a window. */
int mc_wgets(WIN *w, char *s, int linelen, int maxlen)
{
  int st;
  wchar_t buf[BUFLEN];
  size_t i;
  char *sptr;

  sptr = s;
  for (i = 0; *sptr != 0 && i < sizeof (buf) - 1; i++)
    sptr += one_mbtowc(buf + i, sptr, MB_LEN_MAX);
  buf[i] = 0;
  st = mc_wgetwcs(w, buf, linelen, maxlen);
  if (st == 0) {
    sptr = s;
    for (i = 0; buf[i] != 0; i++)
      {
       char tmp[MB_LEN_MAX];
       size_t l;

       /* This truncates data; too bad */
       l = one_wctomb(tmp, buf[i]);
       if (sptr + l >= s + maxlen)
         break;
       memcpy(sptr, tmp, l);
       sptr += l;
      }
    *sptr = 0;
  }
  return st;
}

/* ==== Initialization code ==== */

static char tbuf[1024];
static char cbuf[2048];

/* Map AC characters. */
static int acmap(int c)
{
  const char *p;

  for (p = AC; *p; p += 2)
    if (*p == c)
      return *++p;
  return '.';
}

/*
 * Initialize the window system
 */
#ifdef BBS
/* Code for the BBS system.. */
int win_init(char *term, int lines)
{
  int fg = WHITE;
  int bg = BLACK;
  int attr = XA_NORMAL;
#else
/* Code for other applications (minicom!) */
int win_init(int fg, int bg, int attr)
{
  char *term;
#endif
  static WIN _stdwin;
  int f, olduseattr;

  if (w_init)
    return 0;

#ifndef BBS
  if ((term = getenv("TERM")) == NULL) {
    fprintf(stderr, _("Environment variable TERM not set\n"));
    return -1;
  }
#endif
  switch ((f = tgetent(cbuf, term))) {
    case 0:
      fprintf(stderr, _("No termcap entry for %s\n"), term);
      return -1 ;
    case -1:
      fprintf(stderr, _("No termcap database present!\n"));
      return -1 ;
    default:
      break;
  }
  _tptr = tbuf;

  if ((CM = tgetstr("cm", &_tptr)) == NULL) {
    fprintf(stderr, _("No cursor motion capability (cm)\n"));
    return -1;
  }
  LINES = COLS = 0;
  getrowcols(&LINES, &COLS);
#ifdef BBS
  LINES = lines;
#endif
  if (LINES == 0 && (LINES = tgetnum("li")) <= 0) {
    fprintf(stderr, _("Number of terminal lines unknown\n"));
    return -1;
  }
  if (COLS == 0 && (COLS = tgetnum("co")) <= 0) {
    fprintf(stderr, _("Number of terminal columns unknown\n"));
    return -1;
  }

  /* Terminal Capabilities */
  ME = tgetstr("me", &_tptr);
  SE = tgetstr("se", &_tptr);
  UE = tgetstr("ue", &_tptr);
  AS = tgetstr("as", &_tptr);
  AE = tgetstr("ae", &_tptr);
  MB = tgetstr("mb", &_tptr);
  MD = tgetstr("md", &_tptr);
  MR = tgetstr("mr", &_tptr);
  SO = tgetstr("so", &_tptr);
  US = tgetstr("us", &_tptr);
  CE = tgetstr("ce", &_tptr);
  Al = tgetstr("al", &_tptr);
  Dl = tgetstr("dl", &_tptr);
  AL = tgetstr("AL", &_tptr);
  DL = tgetstr("DL", &_tptr);
  CS = tgetstr("cs", &_tptr);
  SF = tgetstr("sf", &_tptr);
  SR = tgetstr("sr", &_tptr);
  VB = tgetstr("vb", &_tptr);
  BL = tgetstr("bl", &_tptr);
  VE = tgetstr("ve", &_tptr);
  VI = tgetstr("vi", &_tptr);
  IS = tgetstr("is", &_tptr);
  RS = tgetstr("rs", &_tptr);
  KS = tgetstr("ks", &_tptr);
  KE = tgetstr("ke", &_tptr);
  CD = tgetstr("cd", &_tptr);
  CL = tgetstr("cl", &_tptr);
  IC = tgetstr("ic", &_tptr);
  DC = tgetstr("dc", &_tptr);
  BC = tgetstr("bc", &_tptr);
  CR = tgetstr("cr", &_tptr);
  NL = tgetstr("nl", &_tptr);
  AC = tgetstr("ac", &_tptr);
  EA = tgetstr("eA", &_tptr);
#ifdef ST_LINE
  TS = tgetstr("ts", &_tptr);
  FS = tgetstr("fs", &_tptr);
  DS = tgetstr("ds", &_tptr);
#endif

  if (MR == NULL)
    MR = SO;         /* Try standout */
  if (MR == NULL)
    MR = US;         /* Try underline */
  if (MR == NULL)
    MR = MD;         /* Try bold */
  if (SF == NULL)
    SF = "\n";
  if (AC == NULL || *AC == 0)
    AC = def_ac;     /* Standard vt100 mappings. */

  /* cr and nl are often not defined but result in great optimization.
   * I only hope that minicom does not break on terminals where this
   * really does not work..
   */
  if (CR == NULL)
    CR = "\r";
  if (NL == NULL)
    NL = "\n";

#ifdef ST_LINE
  /* See if we can use the status line. */
  if (!tgetflag("hs") || !tgetflag("es") || !TS || !FS)
    use_status = 0;
#else
  use_status = 0;
#endif

  if (IS != NULL)
    outstr(IS); /* Initialization string */

  /* Reset attributes */
  olduseattr = useattr;
  useattr = 1;
  _setattr(XA_NORMAL, COLATTR(WHITE, BLACK));
  useattr = olduseattr;

  /* No reverse? don't use attributes at all. */
  if (MR == NULL)
    useattr = 0;

  /* If we have the "ug" flag, don't allow attributes to be displayed. */
  if (tgetnum("ug") > 0)
    useattr = 0;

  _has_am = tgetflag("am");
  _mv_standout = tgetflag("ms");
  if (tgetflag("bs")) {
    if (BC == NULL)
      BC = "\b";
  }
  else
    BC = NULL;

  /* Special IBM box-drawing characters */
  D_UL  = 201;
  D_HOR = 205;
  D_UR  = 187;
  D_LL  = 200;
  D_VER = 186;
  D_LR  = 188;

  S_UL  = 218;
  S_HOR = 240;
  S_UR  = 191;
  S_LL  = 192;
  S_VER = 179;
  S_LR  = 217;

  if (AS != NULL && !screen_ibmpc) {
    /* Try to find AC mappings. */
    D_UL  = S_UL  = acmap('l');
    D_HOR = S_HOR = acmap('q');
    D_UR  = S_UR  = acmap('k');
    D_LL  = S_LL  = acmap('m');
    D_VER = S_VER = acmap('x');
    D_LR  = S_LR  = acmap('j');
  }

  if (screen_iso) {
    /* Try to find AC mappings. */
    D_UL  = S_UL  = '+';
    D_HOR = S_HOR = '-';
    D_UR  = S_UR  = '+';
    D_LL  = S_LL  = '+';
    D_VER = S_VER = '|';
    D_LR  = S_LR  = '+';
  }


  /* Memory for global map */
  if ((gmap = malloc(sizeof(ELM) * (LINES + 1) * COLS)) == NULL) {
    fprintf(stderr, "Not enough memory\n");
    return -1;
  };
  _buffend = _bufstart + BUFFERSIZE;

  /* Initialize stdwin */
  stdwin = &_stdwin;

  stdwin->wrap     = 1;
  stdwin->cursor   = CNORMAL;
  stdwin->autocr   = 1;
  stdwin->doscroll = 1;
  stdwin->x1       = 0;
  stdwin->sy1      = stdwin->y1 = 0;
  stdwin->x2       = COLS - 1;
  stdwin->sy2      = stdwin->y2 = LINES - 1;
  stdwin->xs       = COLS;
  stdwin->ys       = LINES;
  stdwin->attr     = attr;
  stdwin->color    = COLATTR(fg, bg);
  stdwin->direct   = 1;
  stdwin->histbuf  = NULL;

  if (EA != NULL)
    outstr(EA);          /* Graphics init. */
  if (KS != NULL)
    outstr(KS);          /* Keypad mode */

  setcbreak(1);          /* Cbreak, no echo */

  mc_winclr(stdwin);
  w_init = 1;
  return 0;
}

void win_end(void)
{
  if (gmap == NULL || w_init == 0)
    return;
  setcbreak(0); /* Reset */
  stdwin->attr = XA_NORMAL;
  stdwin->color = COLATTR(WHITE, BLACK);
  _setattr(stdwin->attr, stdwin->color);
  mc_winclr(stdwin);
#ifdef ST_LINE
  if (DS)
    outstr(DS);
#endif
  mc_wcursor(stdwin, CNORMAL);
  if (KE != NULL)
    outstr(KE);
  if (RS != NULL)
    outstr(RS);
  else if (IS != NULL)
    outstr(IS);

  mc_wflush();
  free(gmap);
  gmap = NULL;
  stdwin = NULL;
  w_init = 0;
}
