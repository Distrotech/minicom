/*
 * rwconf.c	Routines to deal with ASCII configuration files.
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
 *	When adding options, remember to add them here in the mpars structure
 *	AND to the macro definitions in configsym.h.
 *
 * // fmg 12/20/93 - kludged in color "support" (hey, it works)
 * // fmg 2/15/94 - added 9 x MAC_LEN char macros for F1 to F10 which can be
 *                  save to a specified file so that old defaults file
 *                  works with these patches. TODO: make these alloc
 *                  memory dynamically... it's nice to have a 15K macro
 *                  _WHEN_ it's being (not like now with -DMAC_LEN) :-)
 * // jl  23.06.97 - changed mdropdtr to numeric
 * // jl  04.09.97 - conversion table filename added to mpars table
 * // jl  22.02.98 - file selection window setting added to mpars table
 * // acme 26.02.98 - i18n
 * // acme 18.03.98 - more i18n
 * // jl  05.04.98 - added the multifile parameter for transfer protocols
 *    jl  06.07.98 - added option P_CONVCAP
 *    jl  28.11.98 - added P_SHOWSPD
 *    jl  05.04.99 - logging options
 *    er  18-Apr-99 - added P_MULTILINE for "multiline"
 *    jl  10.02.2000 - added P_STOPB
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "port.h"
#include "minicom.h"
#include "intl.h"

/* fmg macros stuff */
#define MAX_MACS        10       /* fmg - header files? what's that... */
struct macs mmacs[] = {
  { "",       0,   "pmac1" },
  { "",       0,   "pmac2" },
  { "",       0,   "pmac3" },
  { "",       0,   "pmac4" },
  { "",       0,   "pmac5" },
  { "",       0,   "pmac6" },
  { "",       0,   "pmac7" },
  { "",       0,   "pmac8" },
  { "",       0,   "pmac9" },
  { "",       0,   "pmac10" },

  /* That's all folks */

  { "",       0,        NULL },
};

struct pars mpars[] = {
  /* Protocols */
  /* Warning: minicom assumes the first 12 entries are these proto's ! */
  { "YUNYYzmodem",	0,   "pname1" },
  { "YUNYYymodem",	0,   "pname2" },
  { "YUNYNxmodem",	0,   "pname3" },
  { "NDNYYzmodem",	0,   "pname4" },
  { "NDNYYymodem",	0,   "pname5" },
  { "YDNYNxmodem",	0,   "pname6" },
  { "YUYNNkermit",	0,   "pname7" },
  { "NDYNNkermit",	0,   "pname8" },
  { "YUNYNascii",	0,   "pname9" },
  { "",			0,   "pname10" },
  { "",			0,   "pname11" },
  { "",			0,   "pname12" },
#if defined(__linux__) || defined(__GNU__)
  { "/usr/bin/sz -vv -b",	0,   "pprog1" },
  { "/usr/bin/sb -vv",		0,   "pprog2" },
  { "/usr/bin/sx -vv",		0,   "pprog3" },
  { "/usr/bin/rz -vv -b -E",	0,   "pprog4" },
  { "/usr/bin/rb -vv",		0,   "pprog5" },
  { "/usr/bin/rx -vv",		0,   "pprog6" },
  { "/usr/bin/kermit -i -l %l -b %b -s", 0, "pprog7" },
  { "/usr/bin/kermit -i -l %l -b %b -r", 0, "pprog8" },
#else
  /* Most sites have this in /usr/local, except Linux. */
  { "/usr/local/bin/sz -vv",	0,   "pprog1" },
  { "/usr/local/bin/sb -vv",	0,   "pprog2" },
  { "/usr/local/bin/sx -vv",	0,   "pprog3" },
  { "/usr/local/bin/rz -vv",	0,   "pprog4" },
  { "/usr/local/bin/rb -vv",	0,   "pprog5" },
  { "/usr/local/bin/rx -vv",	0,   "pprog6" },
  { "/usr/local/bin/kermit -i -l %l -s", 0, "pprog7" },
  { "/usr/local/bin/kermit -i -l %l -r", 0, "pprog8" },
#endif
  { "/usr/bin/ascii-xfr -dsv", 0,   "pprog9" },
  { "",			0,   "pprog10" },
  { "",			0,   "pprog11" },
  { "",			0,   "pprog12" },
  /* Serial port & friends */
  { DFL_PORT,		0,  "port" },
  { CALLIN,		0,  "callin" },
  { CALLOUT,		0,  "callout" },
  { UUCPLOCK,		0,  "lock" },
  { DEF_BAUD,		0,   "baudrate" },
  { "8",		0,   "bits" },
  { "N",		0,   "parity" },
  { "1",		0,   "stopbits" },
  /* Kermit the frog */
  { KERMIT,		0,  "kermit" },
  { N_("Yes"),		0,  "kermallow" },
  { N_("No"),		0,  "kermreal" },
  { "3",		0,   "colusage" },
  /* The script program */
  { "runscript",	0,   "scriptprog" },
  /* Modem parameters */
  { "",                 0,   "minit" },
  { "",                 0,   "mreset" },
  { "ATDT",		0,   "mdialpre" },
  { "^M",		0,   "mdialsuf" },
  { "ATDP",		0,   "mdialpre2" },
  { "^M",		0,   "mdialsuf2" },
  { "ATX1DT",		0,   "mdialpre3" },
  { ";X4D^M",		0,   "mdialsuf3" },
  { "CONNECT",		0,   "mconnect" },
  { "NO CARRIER",	0,   "mnocon1" },
  { "BUSY",		0,   "mnocon2" },
  { "NO DIALTONE",	0,   "mnocon3" },
  { "VOICE",		0,   "mnocon4" },
  { "~~+++~~ATH^M",	0,   "mhangup" },
  { "^M",		0,   "mdialcan" },
  { "45",		0,   "mdialtime" },
  { "2",		0,   "mrdelay" },
  { "10",		0,   "mretries" },
  { "1",		0,   "mdropdtr" },   /* jl 23.06.97 */
  { "No",		0,   "mautobaud" },
  { "d",		0,   "showspeed" },  /* d=DTE, l=line speed */
  { "",			0,   "updir" },
  { "",			0,   "downdir" },
  { "",			0,   "scriptdir" },
  { "^A",		0,   "escape-key" },
  { "BS",		0,   "backspace" },
  { N_("enabled"),	0,   "statusline" },
  { N_("Yes"),		0,   "hasdcd" },
  { N_("Yes"),		0,   "rtscts" },
  { N_("No"),		0,   "xonxoff" },
  { "D",		0,   "zauto" },

  /* fmg 1/11/94 colors */
  /* MARK updated 02/17/95 to be more like TELIX. After all its configurable */

  { "YELLOW",           0,   "mfcolor" },
  { "BLUE",             0,   "mbcolor" },
  { "WHITE",            0,   "tfcolor" },
  { "BLACK",            0,   "tbcolor" },
  { "WHITE",            0,   "sfcolor" },
  { "RED",              0,   "sbcolor" },

  /* fmg 2/20/94 macros */

  { ".macros",          0,   "macros" },
  { "",                 0,   "changed" },
  { "Yes",		0,   "macenab" },

  /* Continue here with new stuff. */
  { "Yes",		0,   "sound"  },
  /* MARK updated 02/17/95 - History buffer size */
  { "2000",             0,   "histlines" },

  /* Character conversion table - jl / 04.09.97 */
  { "",			0,    "convf" },
  { "Yes",		0,    "convcap" },
  /* Do you want to use the filename selection window? */
  { "Yes",		0,    "fselw" },
  /* Do you want to be prompted for the download directory? */
  { "No",		0,    "askdndir" },

  /* Logfile options - jl 05.04.99 */
#ifdef LOGFILE
  { LOGFILE,		0,    "logfname" },
#else
  { "/dev/null",	0,    "logfname" },
#endif
  { "Yes",		0,    "logconn" },
  { "Yes",		0,    "logxfer" },

  { "No",		0,    "multiline" },

  /* Terminal behaviour */
  { "No",		0,    "localecho" },
  { "No",		0,    "addlinefeed" },
  { "No",		0,    "linewrap" },
  { "No",		0,    "displayhex" },
  { "No",		0,    "addcarreturn" },

  { "Minicom"VERSION,   0,    "answerback" },

  /* That's all folks */
  { "",                 0,         NULL },
};

/*
 * fmg - Write the macros to a file.
 */
int writemacs(FILE *fp)
{
  struct macs *m;

  for (m = mmacs; m->desc; m++)
    if (m->flags & CHANGED)
      fprintf(fp, "pu %-16.16s %s\n", m->desc, m->value);
  return 0;
}

/*
 * Write the parameters to a file.
 */
int writepars(FILE *fp, int all)
{
  struct pars *p;

  if (all)
    fprintf(fp, _("# Machine-generated file - use \"minicom -s\" to change parameters.\n"));
  else
    fprintf(fp, _("# Machine-generated file - use setup menu in minicom to change parameters.\n"));

  for (p = mpars; p->desc; p++)
    if (p->flags & CHANGED)
      fprintf(fp, "pu %-16.16s %s\n", p->desc, p->value);
  return 0;
}

/*
 * Read the parameters from a file.
 */
int readpars(FILE *fp, enum config_type conftype)
{
  struct pars *p;
  int line_size = 100;
  char *line, *s;
  int dosleep = 0;
  int lineno = 0;
  int matched;

  if (conftype == CONFIG_GLOBAL)
    strcpy(P_SCRIPTPROG, "runscript");

  line = malloc(line_size);
  if (!line) {
    fprintf(stderr, _("Memory allocation failed.\n"));
    return 1;
  }

  while (fgets(line, line_size - 1, fp)) {

    /* Check if whole line went into the buffer */
    if (line[strlen(line) - 1] != '\n') {
      /* Seek back to start of line */
      fseek(fp, -strlen(line), SEEK_CUR);
      /* Increase buffer and try again */
      line_size += 100;
      line = realloc(line, line_size);
      continue;
    }

    lineno++;

    s = line;
    while (isspace(*s))
      s++;

    if (!*s || *s == '#')
      continue;

    /* Skip old 'pr' and 'pu' marks at the beginning of the line */
    if (strlen(s) >= 3
        && (strncmp(s, "pr", 2) == 0
            || strncmp(s, "pu", 2) == 0)
        && (s[2] == ' ' || s[2] == '\t'))
      s += 3;

    matched = 0;
    for (p = mpars; p->desc; p++) {

      /* Matched config option? */
      if (strncmp(p->desc, s, strlen(p->desc)))
        continue;

      /* Whole word matches? */
      if (strlen(s) > strlen(p->desc)
          && !isspace(s[strlen(p->desc)]))
        continue;

      matched = 1;

      /* Move to value */
      s += strlen(p->desc);
      while (isspace(*s))
        s++;

      /* Remove whitespace at end of line */
      while (isspace(s[strlen(s) - 1]))
        s[strlen(s) - 1] = 0;

      /* If the same as default, don't mark as changed */
      if (strcmp(p->value, s) == 0) {
        p->flags &= ~CHANGED;
      } else {
        p->flags |= conftype == CONFIG_GLOBAL ? ADM_CHANGE : USR_CHANGE;
        strncpy(p->value, s, sizeof(p->value) - 1);
        p->value[sizeof(p->value) - 1] = 0;
      }

      /* Done. */
      break;
    }
    if (!matched) {
      fprintf (stderr,
               _("** Line %d of the %s config file is unparsable.\n"),
               lineno, conftype == CONFIG_GLOBAL? _("global") : _("personal"));
      dosleep = 1;
    }
  }

  free(line);

  if (dosleep)
    sleep(3);

  return 0;
}

/*
 * fmg - Read the macros from a file.
 */
int readmacs(FILE *fp, int init)
{
  struct macs *m;
  char   line[MAC_LEN];
  int    public, max_macs=MAX_MACS+1;
  char   *s;

  while (fgets(line, MAC_LEN, fp) != NULL && max_macs--) {
    s = strtok(line, "\n\t ");
    /* Here we have pr for private and pu for public */
    public = 0;
    if (strcmp(s, "pr") == 0) {
      public = 0;
      s = strtok(NULL, "\n\t ");
    }
    if (strcmp(line, "pu") == 0) {
      public = 1;
      s = strtok(NULL, "\n\t ");
    }
    /* Don't read private entries if prohibited */
    if (!init && public == 0)
      continue;

    for (m = mmacs; m->desc != NULL; m++) {
      if (strcmp(m->desc, s) == 0) {
        /* Set value */
        if ((s = strtok(NULL, "\n")) == NULL)
          s = "";
        while (*s && (*s == '\t' || *s == ' '))
          s++;

        /* If the same as default, don't mark as changed */
        if (strcmp(m->value, s) == 0)
          m->flags = 0;
        else {
          if (init)
            m->flags |= ADM_CHANGE;
          else
            m->flags |= USR_CHANGE;
          strcpy(m->value, s);
        }
        break;
      }
    }
  }
  return 0;
}
