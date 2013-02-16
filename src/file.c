/*
 * file.c	Functions to handle file selector.
 *
 *	This file is part of the minicom communications package,
 *	Copyright 1991-1995 Miquel van Smoorenburg.
 *
 *	This file created from code mostly cadged from "dial.c"
 *	by Miquel van Smoorenburg.  Written by James S. Seymour.
 *	Copyright (c) 1998 by James S. Seymour (jseymour@jimsun.LinxNet.com)
 *      Some mods for i18n
 *      Copyright (c) 1998 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <limits.h>

#include "port.h"
#include "minicom.h"
#include "intl.h"
#include "getsdir.h"

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#define FILE_MWTR 1	/* main window top row */
#define SUBM_OKAY 5	/* last entry in sub-menu */

static int nrents = 1;

static void file_tell(const char *s);
static void dhili(int k);
static void prdir(WIN *dirw, int top, int cur, GETSDIR_ENTRY *dirdat, int longest);
static void prone(WIN *dirw, GETSDIR_ENTRY *dirdat, int longest, int inverse);
static void *set_work_dir(void *existing, size_t min_len);
static int  new_filedir(GETSDIR_ENTRY *dirdat, int flushit);
static void goto_filedir(char *new_dir, int absolut);
static int  tag_untag(char *pat, int tag);
static char *concat_list(GETSDIR_ENTRY *d);

static WIN *dsub;
static const char *const what[] =
  {
    /* TRANSLATORS: Translation of each of these menu items should not be
     * longer than 7 characters. The upper-case letter is a shortcut,
     * so keep them unique and ASCII; 'h', 'j', 'k', 'l' are reserved */
    N_("[Goto]"), N_("[Prev]"), N_("[Show]"), N_("[Tag]"), N_("[Untag]"),
    N_("[Okay]")
  };
#define WHAT_NR_OPTIONS (sizeof (what) / sizeof (*what))
#define WHAT_WIDTH 8 /* Width of one entry */
/* Number of bytes for <= 7 characters */
static int what_lens[WHAT_NR_OPTIONS];
/* Number of ' ' padding entries at left and right, left is >= 1 */
static int what_padding[WHAT_NR_OPTIONS][2];
static int dprev;

/* Little menu. */
static const char *d_yesno[] = { N_("   Yes  "), N_("   No   "), NULL };


/*
 * Tell a little message
 */
static void file_tell(const char *s)
{
  WIN *w;

  w = mc_tell("%s", s);
  sleep(1);
  mc_wclose(w, 1);
}

/* Draw an entry in the horizontal menu */
static void horiz_draw(size_t k, char start_attr, char end_attr)
{
  static const char spaces[] = "        ";

  mc_wprintf(dsub, "%.*s", what_padding[k][0], spaces);
  mc_wsetattr(dsub, start_attr);
  mc_wprintf(dsub, "%.*s", what_lens[k], _(what[k]));
  mc_wsetattr(dsub, end_attr);
  mc_wprintf(dsub, "%.*s", what_padding[k][1], spaces);
}

/*
 * Highlight a choice in the horizontal menu.
 */
static void dhili(int k)
{
  int initial_y = (76 - (WHAT_NR_OPTIONS * WHAT_WIDTH >= 76
	           ? 74 : WHAT_NR_OPTIONS * WHAT_WIDTH)) / 2;

  if (k == dprev)
    return;

  if (dprev >= 0) {
    mc_wlocate(dsub, initial_y + WHAT_WIDTH * dprev, 0);
    if (!useattr)
      mc_wputs(dsub, " ");
    else
      horiz_draw(dprev, stdattr, stdattr);
  }
  dprev = k;

  mc_wlocate(dsub, initial_y + WHAT_WIDTH * k, 0);
  if (!useattr)
    mc_wputs(dsub, ">");
  else
    horiz_draw(k, XA_REVERSE | stdattr, stdattr);
}

static inline GETSDIR_ENTRY *getno(int no, GETSDIR_ENTRY *d)
{
  if (no >= nrents)
    return NULL;
  return d + no;
}

/*
 * Print the directory. Only draw from "cur" to bottom.
 */
static void prdir(WIN *dirw, int top, int cur,
                  GETSDIR_ENTRY *dirdat, int longest)
{
  int f, start;
  char f_str[BUFSIZ];
  char t_str[BUFSIZ];

  start = cur - top;
  dirflush = 0;
  sprintf(f_str, " %%-%ds", longest + 2);
  mc_wlocate(dirw, 0, start + FILE_MWTR);
  for (f = start; f < dirw->ys - (1 + FILE_MWTR); f++) {
    GETSDIR_ENTRY *d;
    if (!(d = getno(f + top, dirdat)))
      break;
    if (d->cflags & FL_TAG)
      mc_wsetattr(dirw, XA_REVERSE | stdattr);
    if (S_ISDIR(d->mode)) {
      snprintf(t_str, sizeof(t_str), "[%s]", d->fname);
      mc_wprintf(dirw, f_str, t_str);
    } else
      mc_wprintf(dirw, f_str, d->fname);
    mc_wsetattr(dirw, XA_NORMAL | stdattr);
    mc_wputc(dirw, '\n');
  }
  dirflush = 1;
  mc_wflush();
}

/*
 * Print one directory entry.
 */
static void prone(WIN *dirw, GETSDIR_ENTRY *dirdat, int longest, int inverse)
{
  char f_str[BUFSIZ];
  char t_str[BUFSIZ];

  dirflush = 0;
  sprintf(f_str, " %%-%ds", longest + 2);
  /*
  if (dirdat->cflags & FL_TAG)
    mc_wsetattr(dirw, XA_REVERSE | stdattr);
     */
  if (inverse)
    mc_wsetattr(dirw, XA_REVERSE | stdattr);
  if (S_ISDIR(dirdat->mode)) {
    snprintf(t_str, sizeof(t_str),  "[%s]", dirdat->fname);
    mc_wprintf(dirw, f_str, t_str);
  } else
    mc_wprintf(dirw, f_str, dirdat->fname);
  mc_wsetattr(dirw, XA_NORMAL | stdattr);
  dirflush = 1;
  mc_wflush();
}

static WIN *main_w;
static GETSDIR_ENTRY *global_dirdat;
static int cur = 0;
static int ocur = 0;
static int subm = SUBM_OKAY;
static int quit = 0;
static int top = 0;
static int c = 0;
static int pgud = 0;
static int first = 1;
static char *s;
static int longest;
static char file_title[BUFSIZ];
static char cwd_str[BUFSIZ];
static char *prev_dir = NULL;
static char *work_dir = NULL;
static char *d_work_dir = NULL;
static char *u_work_dir = NULL;
static int min_len = 1;
static char wc_str[128] = "";
static char wc_mem[128] = "";
static int tag_cnt = 0;
static int how_many = 0;
static int down_loading = 0;
static char *ret_buf = NULL;


/* Init up/down work directories to make sure we start from
 * the configuration defaults on the next up/download. jl 6/2000
 */
void init_dir(char dir)
{
  char *p = NULL;

  switch (dir) {
  case 'u':
    p = u_work_dir;
    u_work_dir = NULL;
    break;
  case 'd':
    p = d_work_dir;
    d_work_dir = NULL;
    break;
  }
  free((void *) p);
  return;
}

static void *set_work_dir(void *existing, size_t min_len)
{
  void *vp = realloc(existing, min_len);

  if (down_loading)
    d_work_dir = vp;
  else
    u_work_dir = vp;

  return vp;
}


/*
 * Initialize new file directory.
 *
 * Sets the current working directory.  Non-0 return = no change.
 */
static int new_filedir(GETSDIR_ENTRY *dirdat, int flushit)
{
  static size_t dp_len = 0;
  static char cwd_str_fmt[BUFSIZ] = "";
  size_t new_dp_len, fmt_len;
  char disp_dir[80];
  int initial_y = (76 - (WHAT_NR_OPTIONS * WHAT_WIDTH >= 76
                   ? 74 : WHAT_NR_OPTIONS * WHAT_WIDTH)) / 2;
  size_t i;
  char * new_prev_dir;

  cur      =  0;
  ocur     =  0;
  subm     =  SUBM_OKAY;
  quit     =  0;
  top      =  0;
  c        =  0;
  pgud     =  0;
  first    =  1;
  min_len  =  1;
  dprev    = -1;
  tag_cnt  =  0;

  /*
   * get last directory
   */
  work_dir = down_loading ? d_work_dir : u_work_dir;

  /*
   * init working directory to default?
   */
  if (work_dir == NULL) {
    char *s = down_loading? P_DOWNDIR : P_UPDIR;
    min_len = 1;

    if (*s != '/')
      min_len += strlen(homedir) + 1;
    min_len += strlen(s);
    if (min_len < BUFSIZ)
      min_len = BUFSIZ;

    work_dir = set_work_dir(NULL, min_len);

    if (*s == '/')
      strncpy(work_dir, s, min_len);
    else
      snprintf(work_dir, min_len, "%s/%s", homedir, s);
  }
  /* lop-off trailing "/" for consistency */
  if (strlen(work_dir) > 1 && work_dir[strlen(work_dir) - 1] == '/')
    work_dir[strlen(work_dir) - 1] = (char)0;

  /* get the current working directory, which will become the prev_dir, on success */
  new_prev_dir = getcwd(NULL, BUFSIZ);
  if (!new_prev_dir)
    return -1;

  if (!access(work_dir, R_OK | X_OK) && !chdir(work_dir)) {
    /* was able to change to new working directory */
    free(prev_dir);
    prev_dir = new_prev_dir;
  }
  else {
    /* Could not change to the new working directory */
    mc_wbell();
    werror(
        _("Could not change to directory %s (%s)"), 
        work_dir,
        strerror(errno));

    /* restore the previous working directory */
    free(work_dir);
    work_dir = set_work_dir(new_prev_dir, strlen(new_prev_dir));
  }

  /* All right, draw the file directory! */

  if (flushit) {
    dirflush = 0;
    mc_winclr(main_w);
    mc_wredraw(main_w, 1);
  }

  mc_wcursor(main_w, CNORMAL);

  {
    char *s;

    if (down_loading) {
      if (how_many < 0)
        s = _("Select one or more files for download");
      else if (how_many)
	s = _("Select a file for download");
      else
	s = _("Select a directory for download");
    } else {
      if (how_many < 0)
        s = _("Select one or more files for upload");
      else if (how_many)
	s = _("Select a file for upload");
      else
	s = _("Select a directory for upload");
    }
    snprintf(file_title, sizeof(file_title), "%s", s);
  }

  mc_wtitle(main_w, TMID, file_title);
  if ((new_dp_len = strlen(work_dir)) > dp_len) {
    dp_len = new_dp_len;
    snprintf(cwd_str_fmt, sizeof(cwd_str_fmt),
             _("Directory: %%-%ds"), (int)dp_len);
  }
  new_dp_len = mbslen (work_dir);
  if (new_dp_len + (fmt_len = mbslen(cwd_str_fmt)) > 75) {
    size_t i;
    char *tmp_dir = work_dir;

    /* We want the last 73 characters */
    for (i = 0; 73 + i < new_dp_len + fmt_len; i++) {
      wchar_t wc;

      tmp_dir += one_mbtowc(&wc, work_dir, MB_LEN_MAX);
    }
    snprintf(disp_dir, sizeof(disp_dir), "...%s", tmp_dir);
    snprintf(cwd_str, sizeof(cwd_str), cwd_str_fmt, disp_dir);
  } else
    snprintf(cwd_str, sizeof(cwd_str), cwd_str_fmt, work_dir);

  mc_wlocate(main_w, 0, 0);
  mc_wputs(main_w, cwd_str);

  for (i = 0; i < WHAT_NR_OPTIONS; i++) {
    const char *str, *c;
    size_t j;

    str = _(what[i]);
    c = str;
    for (j = 0; j < WHAT_WIDTH - 1 && *c != 0; j++) {
      wchar_t wc;
      c += one_mbtowc (&wc, c, MB_LEN_MAX);
    }
    what_lens[i] = c - str;
    j = WHAT_WIDTH - j; /* Characters left for padding */
    what_padding[i][1] = j / 2; /* Rounding down */
    what_padding[i][0] = j - what_padding[i][1]; /* >= 1 */
  }
  mc_wlocate(dsub, initial_y, 0);
  for (i = 0; i < WHAT_NR_OPTIONS; i++)
    horiz_draw(i, mc_wgetattr(dsub), mc_wgetattr(dsub));

  mc_wsetregion(main_w, 1, main_w->ys - FILE_MWTR);
  main_w->doscroll = 0;

  /* old dir to discard? */
  free(dirdat);
  dirdat = NULL;

  /* get sorted directory */
  if ((nrents = getsdir(".", wc_str,
                        GETSDIR_PARNT|GETSDIR_NSORT|GETSDIR_DIRSF,
                        0, &dirdat, &longest)) < 0) {
    /* we really want to announce the error here!!! */
    mc_wclose(main_w, 1);
    mc_wclose(dsub, 1);
    free(dirdat);
    dirdat = NULL;
    return -1;
  }

  global_dirdat = dirdat; // Hmm...

  prdir(main_w, top, top, dirdat, longest);
  mc_wlocate(main_w, initial_y, main_w->ys - FILE_MWTR);
  mc_wputs(main_w, _("( Escape to exit, Space to tag )"));
  dhili(subm);
  /* this really needs to go in dhili !!!*/
  mc_wlocate(main_w, 0, cur + FILE_MWTR - top);
  if (flushit) {
    dirflush = 1;
    mc_wredraw(dsub, 1);
  }

  return 0;
}


/*
 * Goto a new directory
 */
static void goto_filedir(char *new_dir, int absolut)
{
  if (strcmp(new_dir, "..") == 0) {
    if (strcmp(work_dir, "/")) {
      char *sp = strrchr(work_dir, '/');
      *sp = (char)0;
      if (strlen(work_dir) == 0)
        strcpy(work_dir, "/");
    } else {
      file_tell(_("Can't back up!"));
      return;
    }
  } else if (!absolut) {
    int new_len = strlen(work_dir) + 1;	/* + '/' */
    if ((new_len += strlen(new_dir) + 1) > min_len) {
      min_len = new_len;
      work_dir = set_work_dir(work_dir, min_len);
    }
    if (strcmp(work_dir, "/") != 0)
      strcat(work_dir, "/");
    strcat(work_dir, new_dir);
  } else {
    int new_len = 1;
    if (*new_dir != '/')
      new_len += strlen(homedir) + 1;
    new_len += strlen(new_dir);
    if (min_len < new_len)
      min_len = new_len;

    work_dir = set_work_dir(work_dir, min_len);

    if (*new_dir == '/')
      strncpy(work_dir, new_dir, min_len);
    else
      snprintf(work_dir, min_len, "%s/%s", homedir, new_dir);
  }
  new_filedir(global_dirdat, 1);
}


/*
 * Initialize the file directory.
 */
static int init_filedir(void)
{
  int x1, x2;
  int retstat = -1;

  dirflush = 0;
  x1 = (COLS / 2) - 37;
  x2 = (COLS / 2) + 37;
  dsub = mc_wopen(x1 - 1, LINES - 3, x2 + 1, LINES - 3, BNONE, 
               stdattr, mfcolor, mbcolor, 0, 0, 1);
  main_w = mc_wopen(x1, 2, x2, LINES - 6, BSINGLE, stdattr, mfcolor,
                 mbcolor, 0, 0, 1);

  if (ret_buf != NULL ||
      (retstat = ((ret_buf = (char *)malloc(BUFSIZ)) == NULL)? -1 : 0) == 0) {
    retstat = new_filedir(NULL, 0);
    dirflush = 1;
    mc_wredraw(dsub, 1);
  }
  return retstat;
}


static int tag_untag(char *pat, int tag)
{
  GETSDIR_ENTRY *d = global_dirdat;
  int indxr, cntr;

  if (nrents < 1)
    return 0;

  for (indxr = nrents, cntr = 0; indxr; --indxr, ++d)
    if (S_ISREG(d->mode) && wildmat(d->fname, pat)) {
      if (tag) {
        d->cflags |= FL_TAG;
        ++cntr;
      } else if (d->cflags & FL_TAG) {
        d->cflags &= ~FL_TAG;
        ++cntr;
      }
    }

  return cntr;
}


/*
 * concatenate tagged files into a buffer
 */
static char *concat_list(GETSDIR_ENTRY *dirdat)
{
  GETSDIR_ENTRY *d;
  int indxr, len;
  int i;
  char *j;

  d = dirdat;
  for (indxr = nrents, len = 0; indxr; --indxr, ++d)
    if (d->cflags & FL_TAG)
      len += strlen(d->fname) + 1;

  if (len) {
    if (len > BUFSIZ) {
      if ((ret_buf = (char *)realloc(ret_buf, len)) == NULL) {
        file_tell(_("Too many files tagged - buffer would overflow"));
        return NULL;
      }
    }

    *ret_buf = (char)0;
    d = dirdat;
    for (indxr = nrents; indxr; --indxr, ++d)
      if (d->cflags & FL_TAG) {
        /* this could be *much* more efficient */
        for (i = strlen(ret_buf), j = d->fname; *j; j++) {
          if (*j == ' ') {
            if ((ret_buf = (char*)realloc(ret_buf, ++len)) == NULL) {
              file_tell(_("Too many files tagged - buffer would overflow"));
              return(NULL);
            }
            ret_buf[i++] = '\\';
          }
          ret_buf[i++] = *j;
        }
        ret_buf[i++] = ' ';
        ret_buf[i]   = '\0';
      }

    ret_buf[strlen(ret_buf) - 1] = (char)0;
    return ret_buf;
  }

  return NULL;
}


/*
 * Draw the file directory.
 *
 *      howmany - How many files can be selected
 *		      0 = none (for directory selection only, as in "rz")
 *		      1 = one  (for single-file up-/down-loads, as in "rx")
 *		     -1 = any number (for multiple files, as in "sz")
 *
 *    downloading - Is this for download selection?
 *		      0 = no
 *		      1 = yes - when single file selected, see if it exists
 */
char * filedir(int howmany, int downloading)
{
  time_t click_time = (time_t) 0;
  size_t i;

  how_many = howmany;
  down_loading = downloading;
  init_filedir();

again:
  mc_wlocate(main_w, 0, cur + FILE_MWTR - top);
  if (first) {
    mc_wredraw(main_w, 1);
    first = 0;
  }
  while (!quit) {
    GETSDIR_ENTRY *d = getno(cur, global_dirdat);
    /*
       if(S_ISDIR(d->mode))
       prone(main_w, d, longest, 0);	
       */
    switch (c = wxgetch()) {
      case K_UP:
      case 'k':
        /*
         if(S_ISDIR(d->mode))
         prone(main_w, d, longest, 1);	
         */
        cur -= cur > 0;
        break;
      case K_DN:
      case 'j':
        /*
         if(S_ISDIR(d->mode))
         prone(main_w, d, longest, 1);
         */
        cur += cur < nrents - 1;
        break;
      case K_LT:
      case 'h':
        subm--;
        if (subm < 0)
          subm = SUBM_OKAY;
        break;
      case K_RT:
      case 'l':
        subm = (subm + 1) % 6;
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
      case ' ':    /* Tag if not directory */
        if (S_ISDIR(d->mode)) {
          time_t this_time = time((time_t *)NULL);
          if (this_time - click_time < 2) {
            GETSDIR_ENTRY *d2 = getno(cur, global_dirdat);
            goto_filedir(d2->fname, 0);
            click_time = (time_t)0;
          } else
            click_time = this_time;
        }
        else {
          if (how_many) {
            if ((d->cflags ^= FL_TAG) & FL_TAG) {
              if (tag_cnt && how_many == 1) {
                d->cflags &= ~FL_TAG;
                file_tell(_("Can select only one!"));
                break;
              }
              ++tag_cnt;
            } else
              --tag_cnt;
            mc_wlocate(main_w, 0, cur + FILE_MWTR - top);
            prone(main_w, d, longest, d->cflags & FL_TAG);
            mc_wputc(main_w, '\n');
            cur += cur < nrents - 1;
          }
        }
        break;

      case '\033':
      case '\r':
      case '\n':
        quit = 1;
        break;

      default:
	for (i = 0; i < WHAT_NR_OPTIONS; i++) {
	  if (strchr (_(what[i]), toupper (c)) != NULL) {
	    subm = i;
	    c = '\n';
	    quit = 1;
	    break;
	  }
	 }

        break;
    }

    if (c != ' ')
      click_time = (time_t)0;

    if (cur < top) {
      top--;
      prdir(main_w, top, top, global_dirdat, longest);
    }
    if (cur - top > main_w->ys - (2 + FILE_MWTR)) {
      top++;
      prdir(main_w, top, top, global_dirdat, longest);
    }
    /*
     if(cur != ocur)
     mc_wlocate(main_w, 0, cur + FILE_MWTR - top);
     */

    ocur = cur;
    dhili(subm);
    /* this really needs to go in dhili !!!*/
    mc_wlocate(main_w, 0, cur + FILE_MWTR - top);
  }

  quit = 0;
  /* ESC means quit */
  if (c == '\033') {
    mc_wclose(main_w, 1);
    mc_wclose(dsub, 1);
    free(global_dirdat);
    global_dirdat = NULL;
    return NULL;
  }
  /* Page up or down ? */
  if (pgud == 1) { /* Page up */
    ocur = top;
    top -= main_w->ys - (1 + FILE_MWTR);
    if (top < 0)
      top = 0;
    cur = top;
    pgud = 0;
    if (ocur != top)
      prdir(main_w, top, cur, global_dirdat, longest);
    ocur = cur;
    goto again;
  }
  if (pgud == 2) { /* Page down */
    ocur = top;
    if (top < nrents - main_w->ys + (1 + FILE_MWTR)) {
      top += main_w->ys - (1 + FILE_MWTR);
      if (top > nrents - main_w->ys + (1 + FILE_MWTR)) {
        top = nrents - main_w->ys + (1 + FILE_MWTR);
      }
      cur = top;
    } else
      cur = nrents - 1;
    pgud = 0;
    if (ocur != top)
      prdir(main_w, top, cur, global_dirdat, longest);
    ocur = cur;
    goto again;
  }

  if (c =='\r' || c == '\n') {
    switch(subm) {
      case 0:
        /* Goto directory */
        {
          char buf[128];
          char *s;
          strncpy(buf, down_loading? P_DOWNDIR : P_UPDIR, sizeof(buf) -1);
          s = input(_("Goto directory:"), buf);
          /* if(s == NULL || *s == (char) 0) */
          if (s == NULL)
            break;
          goto_filedir(buf, 1);
        }
        break;
      case 1:
        /* Previous directory */
        goto_filedir(prev_dir, 1);
        break;
      case 2:
        /* File (wildcard) spec */
        {
          char *s = input(_("Filename pattern:"), wc_mem);
          if (s == NULL || *s == (char) 0)
            break;
          strcpy(wc_str, wc_mem);
          new_filedir(global_dirdat, 1);
          wc_str[0] = (char)0;
        }
        break;
      case 3:
        /* Tag */
        if (how_many == 1)
          file_tell(_("Can select only one!"));
        else if (how_many == -1) {
          char tag_buf[128];
          char *s;
          strncpy(tag_buf, wc_mem, 128);

          s = input(_("Tag pattern:"), tag_buf);
          if (s != NULL && *s != (char)0) {
            int newly_tagged;
            if ((newly_tagged = tag_untag(tag_buf, 1)) == 0) {
              file_tell(_("No file(s) tagged"));
              goto tag_end;
            }
            tag_cnt += newly_tagged;
            prdir(main_w, top, top, global_dirdat, longest);  
          }
        }
tag_end:
        break;
      case 4:
        /* Untag */
        {
          char tag_buf[128];
          char *s;
          int untagged;
          strncpy(tag_buf, wc_mem, 128);

          s = input(_("Untag pattern:"), tag_buf);
          if (s == NULL || *s == (char)0)
            goto untag_end;
          if ((untagged = tag_untag(tag_buf, 0)) == 0) {
            file_tell(_("No file(s) untagged"));
            goto untag_end;
          }
          tag_cnt -= untagged;
          prdir(main_w, top, top, global_dirdat, longest);  
        }
untag_end:
        break;
      case 5:
        {
          /* Done */
          char *ret_ptr = NULL;	/* failsafe: assume failure */

          if (how_many != 0 && !tag_cnt) {

            while (1) {
              s = input(_("No file selected - enter filename:"),
                        ret_buf);
              if (s != NULL && *s != (char) 0) {
                int f_exist = access(ret_buf, F_OK);
                if (down_loading) {
                  if (f_exist != -1) {
                    /* ask 'em if they're *sure* */
                    char buf[BUFSIZ];

                    snprintf(buf, sizeof(buf), 
                             _("File: \"%s\" exists! Overwrite?"), ret_buf);
                    if (ask(buf, d_yesno) == 0) {
                      ret_ptr = ret_buf;
                      break;
                    }
                  } else {
                    ret_ptr = ret_buf;
                    break;
                  }
                } else {
                  if (f_exist == -1)
                    file_tell(_("no such file!"));
                  else {
                    ret_ptr = ret_buf;
                    break;
                  }
                }
              } else {
                /* maybe want to ask: "abort?", here */
                goto again;
              }
            }
          }
          else {
            /* put 'em in a buffer for return */
            if (how_many == 0) {
              /* current working directory */
              ret_ptr = work_dir;
            } else {
              ret_ptr = concat_list(global_dirdat);
            }
          }

          mc_wclose(main_w, 1);
          mc_wclose(dsub, 1);
          free(global_dirdat);
	  global_dirdat = NULL;
          return ret_ptr;
        }
        break;
      default:
        /* should "beep", I guess (? shouldn't get here) */
        file_tell("BEEP!");
        break;
    } /* switch */
  }

  goto again;
}
