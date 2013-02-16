/*
 * vt100.c	ANSI/VT102 emulator code.
 *		This code was integrated to the Minicom communications
 *		package, but has been reworked to allow usage as a separate
 *		module.
 *
 *		It's not a "real" vt102 emulator - it's more than that:
 *		somewhere between vt220 and vt420 in commands.
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
 * // jl 04.09.97 character map conversions in and out
 *    jl 06.07.98 use conversion tables with the capture file
 *    mark.einon@gmail.com 16/02/11 - Added option to timestamp terminal output
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <time.h>
#include "port.h"
#include "minicom.h"
#include "vt100.h"
#include "config.h"

/*
 * The global variable esc_s holds the escape sequence status:
 * 0 - normal
 * 1 - ESC
 * 2 - ESC [
 * 3 - ESC [ ?
 * 4 - ESC (
 * 5 - ESC )
 * 6 - ESC #
 * 7 - ESC P
 */
static int esc_s = 0;

#define ESC 27

/* Structure to hold escape sequences. */
struct escseq {
  int code;
  const char *vt100_st;
  const char *vt100_app;
  const char *ansi;
};

/* Escape sequences for different terminal types. */
static struct escseq vt_keys[] = {
  { K_F1,	"OP",	"OP",	"OP" },
  { K_F2,	"OQ",	"OQ",	"OQ" },
  { K_F3,	"OR",	"OR",	"OR" },
  { K_F4,	"OS",	"OS",	"OS" },
  { K_F5,	"[16~",	"[16~",	"OT" },
  { K_F6,	"[17~",	"[17~",	"OU" },
  { K_F7,	"[18~",	"[18~",	"OV" },
  { K_F8,	"[19~",	"[19~",	"OW" },
  { K_F9,	"[20~",	"[20~",	"OX" },
  { K_F10,	"[21~",	"[21~",	"OY" },
  { K_F11,	"[23~",	"[23~",	"OY" },
  { K_F12,	"[24~",	"[24~",	"OY" },
  { K_HOME,	"[1~",	"[1~",	"[H" },
  { K_PGUP,	"[5~",	"[5~",	"[V" },
  { K_UP,	"[A",	"OA",	"[A" },
  { K_LT,	"[D",	"OD",	"[D" },
  { K_RT,	"[C",	"OC",	"[C" },
  { K_DN,	"[B",	"OB",	"[B" },
  { K_END,	"[4~",	"[4~",	"[Y" },
  { K_PGDN,	"[6~",	"[6~",	"[U" },
  { K_INS,	"[2~",	"[2~",	"[@" },
  { K_DEL,	"[3~",	"[3~",	"\177" },
  { 0,		NULL,	NULL,	NULL }
};

/* Two tables for user-defined character map conversions.
 * defmap.h should contain all characters 0-255 in ascending order
 * to default to no conversion.    jl 04.09.1997
 */

unsigned char vt_inmap[256] = {
#include "defmap.h"
};
unsigned char vt_outmap[256] = {
#include "defmap.h"
};

#if TRANSLATE
/* Taken from the Linux kernel source: linux/drivers/char/console.c */
static char * vt_map[] = {
/* 8-bit Latin-1 mapped to the PC character set: '.' means non-printable */
  "................"
  "................"
  " !\"#$%&'()*+,-./0123456789:;<=>?"
  "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
  "`abcdefghijklmnopqrstuvwxyz{|}~."
  "................"
  "................"
  "\377\255\233\234\376\235\174\025\376\376\246\256\252\055\376\376"
  "\370\361\375\376\376\346\024\371\376\376\247\257\254\253\376\250"
  "\376\376\376\376\216\217\222\200\376\220\376\376\376\376\376\376"
  "\376\245\376\376\376\376\231\376\350\376\376\376\232\376\376\341"
  "\205\240\203\376\204\206\221\207\212\202\210\211\215\241\214\213"
  "\376\244\225\242\223\376\224\366\355\227\243\226\201\376\376\230",
/* vt100 graphics */
  "................"
  "\0\0\0\0\0\0\0\0\0\0\376\0\0\0\0\0"
  " !\"#$%&'()*+,-./0123456789:;<=>?"
  "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^ "
  "\004\261\007\007\007\007\370\361\007\007\331\277\332\300\305\304"
  "\304\304\137\137\303\264\301\302\263\363\362\343\330\234\007\0"
  "................"
  "................"
  "\377\255\233\234\376\235\174\025\376\376\246\256\252\055\376\376"
  "\370\361\375\376\376\346\024\371\376\376\247\257\254\253\376\250"
  "\376\376\376\376\216\217\222\200\376\220\376\376\376\376\376\376"
  "\376\245\376\376\376\376\231\376\376\376\376\376\232\376\376\341"
  "\205\240\203\376\204\206\221\207\212\202\210\211\215\241\214\213"
  "\376\244\225\242\223\376\224\366\376\227\243\226\201\376\376\230"
};
static char *vt_trans[2];
static int vt_charset;          /* Character set. */
#endif

static int vt_echo;		/* Local echo on/off. */
int vt_nl_delay;		/* Delay after CR key */
int vt_ch_delay;		/* Delay between characters */
static int vt_type = ANSI;	/* Terminal type. */
static int vt_wrap;             /* Line wrap on/off */
static int vt_addlf;            /* Add linefeed on/off */
static int vt_addcr;            /* Add carriagereturn on/off */
static int vt_fg;		/* Standard foreground color. */
static int vt_bg;		/* Standard background color. */
static int vt_keypad;		/* Keypad mode. */
static int vt_cursor;		/* cursor key mode. */
static int vt_asis;		/* 8bit clean mode. */
static int vt_line_timestamp;	/* Timestamp each line. */
static int vt_bs = 8;		/* Code that backspace key sends. */
static int vt_insert;           /* Insert mode */
static int vt_crlf;		/* Return sends CR/LF */
static int vt_om;		/* Origin mode. */
WIN *vt_win;                    /* Output window. */
static int vt_docap;		/* Capture on/off. */
static void (*vt_keyb)(int, int);/* Gets called for NORMAL/APPL switch. */
static void (*termout)(const char *, int);/* Gets called to output a string. */

static int escparms[8];		/* Cumulated escape sequence. */
static int ptr;                 /* Index into escparms array. */
static long vt_tabs[5];		/* Tab stops for max. 32*5 = 160 columns. */

static short newy1 = 0;		/* Current size of scrolling region. */
static short newy2 = 23;

/* Saved color and posistions */
static short savex, savey, saveattr = XA_NORMAL, savecol = 112;

#if TRANSLATE
static short savecharset;
static char *savetrans[2];
#endif

/*
 * Initialize the emulator once.
 */
void vt_install(void (*fun1)(const char *, int), void (*fun2)(int, int),
                WIN *win)
{
  termout = fun1;
  vt_keyb = fun2;
  vt_win = win;
}

/* Partial init (after screen resize) */
void vt_pinit(WIN *win, int fg, int bg)
{
  vt_win = win;
  newy1 = 0;
  newy2 = vt_win->ys - 1;
  mc_wresetregion(vt_win);
  if (fg > 0)
    vt_fg = fg;
  if (bg > 0)
    vt_bg = bg;
  mc_wsetfgcol(vt_win, vt_fg);
  mc_wsetbgcol(vt_win, vt_bg);
}

/* Set characteristics of emulator. */
void vt_init(int type, int fg, int bg, int wrap, int add_lf, int add_cr)
{
  vt_type = type;
  if (vt_type == ANSI) {
	vt_fg = WHITE;
	vt_bg = BLACK;
  } else {
	vt_fg = fg;
	vt_bg = bg;
  }
  if (wrap >= 0)
    vt_win->wrap = vt_wrap = wrap;
  vt_addlf = add_lf;
  vt_addcr = add_cr;
  vt_insert = 0;
  vt_crlf = 0;
  vt_om = 0;

  newy1 = 0;
  newy2 = vt_win->ys - 1;
  mc_wresetregion(vt_win);
  vt_keypad = NORMAL;
  vt_cursor = NORMAL;
  vt_echo = local_echo;
  vt_tabs[0] = 0x01010100;
  vt_tabs[1] =
  vt_tabs[2] =
  vt_tabs[3] =
  vt_tabs[4] = 0x01010101;
#if TRANSLATE
  vt_charset = 0;
  vt_trans[0] = savetrans[0] = vt_map[0];
  vt_trans[1] = savetrans[1] = vt_map[1];
#endif
  ptr = 0;
  memset(escparms, 0, sizeof(escparms));
  esc_s = 0;

  if (vt_keyb)
    (*vt_keyb)(vt_keypad, vt_cursor);
  mc_wsetfgcol(vt_win, vt_fg);
  mc_wsetbgcol(vt_win, vt_bg);
}

/* Change some things on the fly. */
void vt_set(int addlf, int wrap, int docap, int bscode,
            int echo, int cursor, int asis, int timestamp,
            int addcr)
{
  if (addlf >= 0)
    vt_addlf = addlf;
  if (wrap >= 0)
    vt_win->wrap = vt_wrap = wrap;
  if (docap >= 0)
    vt_docap = docap;
  if (bscode >= 0)
    vt_bs = bscode;
  if (echo >= 0)
    vt_echo = echo;
  if (cursor >= 0)
    vt_cursor = cursor;
  if (asis >=0)
    vt_asis = asis;
  if (timestamp >= 0)
    vt_line_timestamp = timestamp;
  if (addcr >= 0)
    vt_addcr = addcr;
}

/* Output a string to the modem. */
static void v_termout(const char *s, int len)
{
  const char *p;

  if (vt_echo) {
    for (p = s; *p; p++) {
      vt_out(*p);
      if (!vt_addlf && *p == '\r')
        vt_out('\n');
    }
    mc_wflush();
  }

  (*termout)(s, len);
}

/*
 * Escape code handling.
 */

/*
 * ESC was seen the last time. Process the next character.
 */
static void state1(int c)
{
  short x, y, f;

  switch(c) {
    case '[': /* ESC [ */
      esc_s = 2;
      return;
    case '(': /* ESC ( */
      esc_s = 4;
      return;
    case ')': /* ESC ) */
      esc_s = 5;
      return;
    case '#': /* ESC # */
      esc_s = 6;
      return;
    case 'P': /* ESC P (DCS, Device Control String) */
      esc_s = 7;
      return;
    case 'D': /* Cursor down */
    case 'M': /* Cursor up */
      x = vt_win->curx;
      if (c == 'D') { /* Down. */
        y = vt_win->cury + 1;
        if (y == newy2 + 1)
          mc_wscroll(vt_win, S_UP);
        else if (vt_win->cury < vt_win->ys)
          mc_wlocate(vt_win, x, y);
      }
      if (c == 'M')  { /* Up. */
        y = vt_win->cury - 1;
        if (y == newy1 - 1)
          mc_wscroll(vt_win, S_DOWN);
        else if (y >= 0)
          mc_wlocate(vt_win, x, y);
      }
      break;
    case 'E': /* CR + NL */
      mc_wputs(vt_win, "\r\n");
      break;
    case '7': /* Save attributes and cursor position */
    case 's':
      savex = vt_win->curx;
      savey = vt_win->cury;
      saveattr = vt_win->attr;
      savecol = vt_win->color;
#if TRANSLATE
      savecharset = vt_charset;
      savetrans[0] = vt_trans[0];
      savetrans[1] = vt_trans[1];
#endif
      break;
    case '8': /* Restore them */
    case 'u':
#if TRANSLATE
      vt_charset = savecharset;
      vt_trans[0] = savetrans[0];
      vt_trans[1] = savetrans[1];
#endif
      vt_win->color = savecol; /* HACK should use mc_wsetfgcol etc */
      mc_wsetattr(vt_win, saveattr);
      mc_wlocate(vt_win, savex, savey);
      break;
    case '=': /* Keypad into applications mode */
      vt_keypad = APPL;
      if (vt_keyb)
        (*vt_keyb)(vt_keypad, vt_cursor);
      break;
    case '>': /* Keypad into numeric mode */
      vt_keypad = NORMAL;
      if (vt_keyb)
        (*vt_keyb)(vt_keypad, vt_cursor);
      break;
    case 'Z': /* Report terminal type */
      if (vt_type == VT100)
        v_termout("\033[?1;0c", 0);
      else
        v_termout("\033[?c", 0);
      break;
    case 'c': /* Reset to initial state */
      f = XA_NORMAL;
      mc_wsetattr(vt_win, f);
      vt_win->wrap = (vt_type != VT100);
      if (vt_wrap != -1)
        vt_win->wrap = vt_wrap;
      vt_crlf = vt_insert = 0;
      vt_init(vt_type, vt_fg, vt_bg, vt_win->wrap, 0, 0);
      mc_wlocate(vt_win, 0, 0);
      break;
    case 'H': /* Set tab in current position */
      x = vt_win->curx;
      if (x > 159)
        x = 159;
      vt_tabs[x / 32] |= 1 << (x % 32);
      break;
    case 'N': /* G2 character set for next character only*/
    case 'O': /* G3 "				"    */
    case '<': /* Exit vt52 mode */
    default:
      /* ALL IGNORED */
      break;
  }
  esc_s = 0;
}

/* ESC [ ... [hl] seen. */
static void ansi_mode(int on_off)
{
  int i;

  for (i = 0; i <= ptr; i++) {
    switch (escparms[i]) {
      case 4: /* Insert mode  */
        vt_insert = on_off;
        break;
      case 20: /* Return key mode */
        vt_crlf = on_off;
        break;
    }
  }
}

/*
 * ESC [ ... was seen the last time. Process next character.
 */
static void state2(int c)
{
  short x, y, attr, f;
  char temp[32];

  /* See if a number follows */
  if (c >= '0' && c <= '9') {
    escparms[ptr] = 10*escparms[ptr] + c - '0';
    return;
  }
  /* Separation between numbers ? */
  if (c == ';') {
    if (ptr < 15)
      ptr++;
    return;
  }
  /* ESC [ ? sequence */
  if (escparms[0] == 0 && ptr == 0 && c == '?')
    {
      esc_s = 3;
      return;
    }

  /* Process functions with zero, one, two or more arguments */
  switch (c) {
    case 'A':
    case 'B':
    case 'C':
    case 'D': /* Cursor motion */
      if ((f = escparms[0]) == 0)
        f = 1;
      x = vt_win->curx;
      y = vt_win->cury;
      x += f * ((c == 'C') - (c == 'D'));
      if (x < 0)
        x = 0;
      if (x >= vt_win->xs)
        x = vt_win->xs - 1;
      if (c == 'B') { /* Down. */
        y += f;
        if (y >= vt_win->ys)
          y = vt_win->ys - 1;
        if (y >= newy2 + 1)
          y = newy2;
      }
      if (c == 'A') { /* Up. */
        y -= f;
        if (y < 0)
          y = 0;
        if (y <= newy1 - 1)
          y = newy1;
      }
      mc_wlocate(vt_win, x, y);
      break;
    case 'X': /* Character erasing (ECH) */
      if ((f = escparms[0]) == 0)
        f = 1;
      mc_wclrch(vt_win, f);
      break;
    case 'K': /* Line erasing */
      switch (escparms[0]) {
        case 0:
          mc_wclreol(vt_win);
          break;
        case 1:
          mc_wclrbol(vt_win);
          break;
        case 2:
          mc_wclrel(vt_win);
          break;
      }
      break;
    case 'J': /* Screen erasing */
      x = vt_win->color;
      y = vt_win->attr;
      if (vt_type == ANSI) {
        mc_wsetattr(vt_win, XA_NORMAL);
        mc_wsetfgcol(vt_win, WHITE);
        mc_wsetbgcol(vt_win, BLACK);
      }
      switch (escparms[0]) {
        case 0:
          mc_wclreos(vt_win);
          break;
        case 1:
          mc_wclrbos(vt_win);
          break;
        case 2:
          mc_winclr(vt_win);
          break;
      }
      if (vt_type == ANSI) {
        vt_win->color = x;
        vt_win->attr = y;
      }
      break;
    case 'n': /* Requests / Reports */
      switch(escparms[0]) {
        case 5: /* Status */
          v_termout("\033[0n", 0);
          break;
        case 6:	/* Cursor Position */
          sprintf(temp, "\033[%d;%dR", vt_win->cury + 1, vt_win->curx + 1);
          v_termout(temp, 0);
          break;
      }
      break;
    case 'c': /* Identify Terminal Type */
      if (vt_type == VT100) {
        v_termout("\033[?1;2c", 0);
        break;
      }
      v_termout("\033[?c", 0);
      break;
    case 'x': /* Request terminal parameters. */
      /* Always answers 19200-8N1 no options. */
      sprintf(temp, "\033[%c;1;1;120;120;1;0x", escparms[0] == 1 ? '3' : '2');
      v_termout(temp, 0);
      break;
    case 's': /* Save attributes and cursor position */
      savex = vt_win->curx;
      savey = vt_win->cury;
      saveattr = vt_win->attr;
      savecol = vt_win->color;
#if TRANSLATE
      savecharset = vt_charset;
      savetrans[0] = vt_trans[0];
      savetrans[1] = vt_trans[1];
#endif
      break;
    case 'u': /* Restore them */
#if TRANSLATE
      vt_charset = savecharset;
      vt_trans[0] = savetrans[0];
      vt_trans[1] = savetrans[1];
#endif
      vt_win->color = savecol; /* HACK should use mc_wsetfgcol etc */
      mc_wsetattr(vt_win, saveattr);
      mc_wlocate(vt_win, savex, savey);
      break;
    case 'h':
      ansi_mode(1);
      break;
    case 'l':
      ansi_mode(0);
      break;
    case 'H':
    case 'f': /* Set cursor position */
      if ((y = escparms[0]) == 0)
        y = 1;
      if ((x = escparms[1]) == 0)
        x = 1;
      if (vt_om)
        y += newy1;
      mc_wlocate(vt_win, x - 1, y - 1);
      break;
    case 'g': /* Clear tab stop(s) */
      if (escparms[0] == 0) {
        x = vt_win->curx;
        if (x > 159)
          x = 159;
        vt_tabs[x / 32] &= ~(1 << x % 32);
      }
      if (escparms[0] == 3)
        for(x = 0; x < 5; x++)
          vt_tabs[x] = 0;
      break;
    case 'm': /* Set attributes */
      attr = mc_wgetattr((vt_win));
      for (f = 0; f <= ptr; f++) {
        if (escparms[f] >= 30 && escparms[f] <= 37)
          mc_wsetfgcol(vt_win, escparms[f] - 30);
        if (escparms[f] >= 40 && escparms[f] <= 47)
          mc_wsetbgcol(vt_win, escparms[f] - 40);
        switch (escparms[f]) {
          case 0:
            attr = XA_NORMAL;
            mc_wsetfgcol(vt_win, vt_fg);
            mc_wsetbgcol(vt_win, vt_bg);
            break;
          case 1:
            attr |= XA_BOLD;
            break;
          case 4:
            attr |= XA_UNDERLINE;
            break;
          case 5:
            attr |= XA_BLINK;
            break;
          case 7:
            attr |= XA_REVERSE;
            break;
          case 22: /* Bold off */
            attr &= ~XA_BOLD;
            break;
          case 24: /* Not underlined */
            attr &=~XA_UNDERLINE;
            break;
          case 25: /* Not blinking */
            attr &= ~XA_BLINK;
            break;
          case 27: /* Not reverse */
            attr &= ~XA_REVERSE;
            break;
          case 39: /* Default fg color */
            mc_wsetfgcol(vt_win, vt_fg);
            break;
          case 49: /* Default bg color */
            mc_wsetbgcol(vt_win, vt_bg);
            break;
        }
      }
      mc_wsetattr(vt_win, attr);
      break;
    case 'L': /* Insert lines */
      if ((x = escparms[0]) == 0)
        x = 1;
      for (f = 0; f < x; f++)
        mc_winsline(vt_win);
      break;
    case 'M': /* Delete lines */
      if ((x = escparms[0]) == 0)
        x = 1;
      for (f = 0; f < x; f++)
        mc_wdelline(vt_win);
      break;
    case 'P': /* Delete Characters */
      if ((x = escparms[0]) == 0)
        x = 1;
      for (f = 0; f < x; f++)
        mc_wdelchar(vt_win);
      break;
    case '@': /* Insert Characters */
      if ((x = escparms[0]) == 0)
        x = 1;
      for (f = 0; f < x; f++)
        mc_winschar(vt_win);
      break;
    case 'r': /* Set scroll region */
      if ((newy1 = escparms[0]) == 0)
        newy1 = 1;
      if ((newy2 = escparms[1]) == 0)
        newy2 = vt_win->ys;
      newy1-- ; newy2--;
      if (newy1 < 0)
        newy1 = 0;
      if (newy2 < 0)
        newy2 = 0;
      if (newy1 >= vt_win->ys)
        newy1 = vt_win->ys - 1;
      if (newy2 >= vt_win->ys)
        newy2 = vt_win->ys - 1;
      if (newy1 >= newy2) {
        newy1 = 0;
        newy2 = vt_win->ys - 1;
      }
      mc_wsetregion(vt_win, newy1, newy2);
      mc_wlocate(vt_win, 0, newy1);
      break;
    case 'i': /* Printing */
    case 'y': /* Self test modes */
    default:
      /* IGNORED */
      break;
  }
  /* Ok, our escape sequence is all done */
  esc_s = 0;
  ptr = 0;
  memset(escparms, 0, sizeof(escparms));
  return;
}

/* ESC [? ... [hl] seen. */
static void dec_mode(int on_off)
{
  int i;

  for (i = 0; i <= ptr; i++) {
    switch (escparms[i]) {
      case 1: /* Cursor keys in cursor/appl mode */
        vt_cursor = on_off ? APPL : NORMAL;
        if (vt_keyb)
          (*vt_keyb)(vt_keypad, vt_cursor);
        break;
      case 6: /* Origin mode. */
        vt_om = on_off;
        mc_wlocate(vt_win, 0, newy1);
        break;
      case 7: /* Auto wrap */
        vt_win->wrap = on_off;
        break;
      case 25: /* Cursor on/off */
        mc_wcursor(vt_win, on_off ? CNORMAL : CNONE);
        break;
      case 67: /* Backspace key sends. (FIXME: vt420) */
        /* setbackspace(on_off ? 8 : 127); */
        break;
      default: /* Mostly set up functions */
        /* IGNORED */
        break;
    }
  }
}

/*
 * ESC [ ? ... seen.
 */
static void state3(int c)
{
  /* See if a number follows */
  if (c >= '0' && c <= '9') {
    escparms[ptr] = 10*escparms[ptr] + c - '0';
    return;
  }
  switch (c) {
    case 'h':
      dec_mode(1);
      break;
    case 'l':
      dec_mode(0);
      break;
    case 'i': /* Printing */
    case 'n': /* Request printer status */
    default:
      /* IGNORED */
      break;
  }
  esc_s = 0;
  ptr = 0;
  memset(escparms, 0, sizeof(escparms));
  return;
}

/*
 * ESC ( Seen.
 */
static void state4(int c)
{
  /* Switch Character Sets. */
#if !TRANSLATE
  /* IGNORED */
  (void)c;
#else
  switch (c) {
    case 'A':
    case 'B':
      vt_trans[0] = vt_map[0];
      break;
    case '0':
    case 'O':
      vt_trans[0] = vt_map[1];
      break;
  }
#endif
  esc_s = 0;
}

/*
 * ESC ) Seen.
 */
static void state5(int c)
{
  /* Switch Character Sets. */
#if !TRANSLATE
  /* IGNORED */
  (void)c;
#else
  switch (c) {
    case 'A':
    case 'B':
      vt_trans[1] = vt_map[0];
      break;
    case 'O':
    case '0':
      vt_trans[1] = vt_map[1];
      break;
  }
#endif
  esc_s = 0;
}

/*
 * ESC # Seen.
 */
static void state6(int c)
{
  int x, y;

  /* Double height, double width and selftests. */
  switch (c) {
    case '8':
      /* Selftest: fill screen with E's */
      vt_win->doscroll = 0;
      vt_win->direct = 0;
      mc_wlocate(vt_win, 0, 0);
      for (y = 0; y < vt_win->ys; y++) {
        mc_wlocate(vt_win, 0, y);
        for (x = 0; x < vt_win->xs; x++)
          mc_wputc(vt_win, 'E');
      }
      mc_wlocate(vt_win, 0, 0);
      vt_win->doscroll = 1;
      mc_wredraw(vt_win, 1);
      break;
    default:
      /* IGNORED */
      break;
  }
  esc_s = 0;
}

/*
 * ESC P Seen.
 */
static void state7(int c)
{
  /*
   * Device dependant control strings. The Minix virtual console package
   * uses these sequences. We can only turn cursor on or off, because
   * that's the only one supported in termcap. The rest is ignored.
   */
  static char buf[17];
  static int pos = 0;
  static int state = 0;

  if (c == ESC) {
    state = 1;
    return;
  }
  if (state == 1) {
    buf[pos] = 0;
    pos = 0;
    state = 0;
    esc_s = 0;
    if (c != '\\')
      return;
    /* Process string here! */
    if (!strcmp(buf, "cursor.on"))
      mc_wcursor(vt_win, CNORMAL);
    if (!strcmp(buf, "cursor.off"))
      mc_wcursor(vt_win, CNONE);
    if (!strcmp(buf, "linewrap.on")) {
      vt_wrap = -1;
      vt_win->wrap = 1;
    }
    if (!strcmp(buf, "linewrap.off")) {
      vt_wrap = -1;
      vt_win->wrap = 0;
    }
    return;
  }
  if (pos > 15)
    return;
  buf[pos++] = c;
}

static void output_s(const char *s)
{
  mc_wputs(vt_win, s);
  if (vt_docap == 1)
    fputs(s, capfp);
}

static void output_c(const char c)
{
  mc_wputc(vt_win, c);
  if (vt_docap == 1)
    fputc(c, capfp);
}

void vt_out(int ch)
{
  static unsigned char last_ch;
  int f;
  unsigned char c;
  int go_on = 0;
  wchar_t wc;

  if (!ch)
    return;

  if (last_ch == '\n'
      && vt_line_timestamp != TIMESTAMP_LINE_OFF)
    {
      struct timeval tmstmp_now;
      static time_t tmstmp_last;
      char s[36];
      struct tm tmstmp_tm;

      gettimeofday(&tmstmp_now, NULL);
      if ((   vt_line_timestamp == TIMESTAMP_LINE_PER_SECOND
           && tmstmp_now.tv_sec != tmstmp_last)
          || vt_line_timestamp == TIMESTAMP_LINE_SIMPLE
          || vt_line_timestamp == TIMESTAMP_LINE_EXTENDED)
        {
          if (   localtime_r(&tmstmp_now.tv_sec, &tmstmp_tm)
              && strftime(s, sizeof(s), "[%F %T", &tmstmp_tm))
            {
              output_s(s);
              switch (vt_line_timestamp)
                {
                case TIMESTAMP_LINE_SIMPLE:
                  output_s("] ");
                  break;
                case TIMESTAMP_LINE_EXTENDED:
                  snprintf(s, sizeof(s), ".%03ld] ", tmstmp_now.tv_usec / 1000);
                  output_s(s);
                  break;
                case TIMESTAMP_LINE_PER_SECOND:
                  output_s("\r\n");
                  break;
                };
            }
          tmstmp_last = tmstmp_now.tv_sec;
        }
    }

  c = (unsigned char)ch;
  last_ch = c;

  if (vt_docap == 2) /* Literal. */
    fputc(c, capfp);

  /* Process <31 chars first, even in an escape sequence. */
  switch (c) {
    case 5: /* AnswerBack for vt100's */
      if (vt_type != VT100) {
        go_on = 1;
        break;
      }
      v_termout(P_ANSWERBACK, 0);
      break;
    case '\r': /* Carriage return */
      mc_wputc(vt_win, c);
      if (vt_addlf)
        output_c('\n');
      break;
    case '\t': /* Non - destructive TAB */
      /* Find next tab stop. */
      for (f = vt_win->curx + 1; f < 160; f++)
        if (vt_tabs[f / 32] & (1 << f % 32))
          break;
      if (f >= vt_win->xs)
        f = vt_win->xs - 1;
      mc_wlocate(vt_win, f, vt_win->cury);
      if (vt_docap == 1)
        fputc(c, capfp);
      break;
    case 013: /* Old Minix: CTRL-K = up */
      mc_wlocate(vt_win, vt_win->curx, vt_win->cury - 1);
      break;
    case '\f': /* Form feed: clear screen. */
      mc_winclr(vt_win);
      mc_wlocate(vt_win, 0, 0);
      break;
#if !TRANSLATE
    case 14:
    case 15:  /* Change character set. Not supported. */
      break;
#else
    case 14:
      vt_charset = 1;
      break;
    case 15:
      vt_charset = 0;
      break;
#endif
    case 24:
    case 26:  /* Cancel escape sequence. */
      esc_s = 0;
      break;
    case ESC: /* Begin escape sequence */
      esc_s = 1;
      break;
    case 128+ESC: /* Begin ESC [ sequence. */
      esc_s = 2;
      break;
    case '\n':
      if(vt_addcr)
        mc_wputc(vt_win, '\r');
      output_c(c);
	  break;
    case '\b':
    case 7: /* Bell */
      output_c(c);
      break;
    default:
      go_on = 1;
      break;
  }
  if (!go_on)
    return;

  /* Now see which state we are in. */
  switch (esc_s) {
    case 0: /* Normal character */
      if (vt_docap == 1)
        fputc(P_CONVCAP[0] == 'Y' ? vt_inmap[c] : c, capfp);
      if (!using_iconv()) {
        c = vt_inmap[c];    /* conversion 04.09.97 / jl */
#if TRANSLATE
        if (vt_type == VT100 && vt_trans[vt_charset] && vt_asis == 0)
          c = vt_trans[vt_charset][c];
#endif
      }
      /* FIXME: This is wrong, but making it right would require
       * removing all the 8-bit mapping features. Assuming the locale
       * is 8-bit, the character should not be changed by mapping to
       * wchar and back; if the locale is multibyte, there is no hope
       * of getting it right anyway. */
      if (!using_iconv()) {
        one_mbtowc (&wc, (char *)&c, 1); /* returns 1 */
        if (vt_insert)
          mc_winschar2(vt_win, wc, 1);
        else
          mc_wputc(vt_win, wc);
      } else {
        mc_wputc(vt_win, c);
      }
      break;
    case 1: /* ESC seen */
      state1(c);
      break;
    case 2: /* ESC [ ... seen */
      state2(c);
      break;
    case 3:
      state3(c);
      break;
    case 4:
      state4(c);
      break;
    case 5:
      state5(c);
      break;
    case 6:
      state6(c);
      break;
    case 7:
      state7(c);
      break;
  }

  /* Flush output to capture file so that all output is visible there
   * immediately. Causes a write syscall for every call though. */
  if (capfp)
    fflush(capfp);
}

/* Translate keycode to escape sequence. */
void vt_send(int c)
{
  char s[3];
  int f;
  int len = 1;

  /* Special key? */
  if (c < 256) {
    /* Translate backspace key? */
    if (c == K_ERA)
      c = vt_bs;
    s[0] = vt_outmap[c];  /* conversion 04.09.97 / jl */
    s[1] = 0;
    /* CR/LF mode? */
    if (c == '\r' && vt_crlf) {
      s[1] = '\n';
      s[2] = 0;
      len = 2;
    }
    v_termout(s, len);
    if (vt_nl_delay > 0 && c == '\r')
      usleep(1000 * vt_nl_delay);
    return;
  }

  /* Look up code in translation table. */
  for (f = 0; vt_keys[f].code; f++)
    if (vt_keys[f].code == c)
      break;
  if (vt_keys[f].code == 0)
    return;

  /* Now send appropriate escape code. */
  v_termout("\033", 0);
  if (vt_type == VT100) {
    if (vt_cursor == NORMAL)
      v_termout(vt_keys[f].vt100_st, 0);
    else
      v_termout(vt_keys[f].vt100_app, 0);
  } else
    v_termout(vt_keys[f].ansi, 0);
}
