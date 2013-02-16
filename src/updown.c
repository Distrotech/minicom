/*
 * updown.c	Routines to do up and downloading by calling external
 *		programs (sz, rz, kermit).
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
 * jl 13.09.97	pass actual terminal lines (LINES - statusline)
 *		to runscript in environment variable TERMLIN
 * jl 16.09.97	logging of sz/rz file transfers
 * jl 29.09.97	fix on the transfer logging
 * hgk&jl 2.98	filename selection window
 * acme 25.02.98 i18n
 * js&jl 04.98	the better filename selection window
 */

#include <poll.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <wchar.h>

#include "port.h"
#include "minicom.h"
#include "intl.h"

/*#define LOG_XFER	  debugging option to log all output of rz/sz
 */
static int udpid;
static int script_running;

/*
 * Change to a directory.
 */
static int mcd(char *dir)
{
  char buf[256];
  char err[50];
  static char odir[256];
  static int init = 0;

  if (!init) {
    if (*dir == 0)
      return 0;
    init = 1;
    if (getcwd(odir, sizeof(odir)) == NULL)
      return -1;
  }
  if (*dir == 0) {
    if (chdir(odir) == -1)
      return -1;
    return 0;
  }

  if (*dir != '/') {
    snprintf(buf, sizeof(buf), "%s/%s", homedir, dir);
    dir = buf;
  }
  if (chdir(dir) < 0) {
    /* This may look safe but you might I8N change the string! so
       snprintf it */
    snprintf(err, sizeof(err),  _("Cannot chdir to %.30s"), dir);
    err[sizeof(err) - 1] = 0;
    werror("%s", err);
    return -1;
  }
  return 0;
}

/*
 * Catch the CTRL-C signal.
 */
static void udcatch(int dummy)
{
  (void)dummy;
  signal(SIGINT, udcatch);
  if (udpid)
    kill((pid_t)udpid, SIGKILL);
  script_running = 0;
}

/*
 * Translate %b to the current bps rate, and
 *           %l to the current tty port.
 *           %f to the serial port file descriptor
 *
 * Caller must free the returned string
 */
static char *translate(char *s)
{
  char * ptr;
  char * translation;
  size_t translation_length;
  char   str_portfd[8];     /* kino */

  /* determine how many bytes we'll need for the translated version */
  translation_length = 0;
  for (ptr = s; *ptr != '\0'; ptr++) {
    if (*ptr != '%') {
      translation_length++;
    }
    else {
      switch(*++ptr) {

        case 'l': /* tty port */
          translation_length += strlen(dial_tty);
          break;

        case 'b': /* baud rate (bbp) */
          translation_length += strlen(P_BAUDRATE);
          break;

        case 'f': /* serial port file descriptor */
          sprintf(str_portfd, "%d", portfd);
          translation_length += strlen(str_portfd);
          break;

        default: /* treat all other escape sequences literally */
          translation_length += 2;
          break;
      }
    }
  }

  translation = malloc(translation_length + 1);
  if (translation == NULL) {
    do_log("out of memory");
    return NULL;
  }

  /* now copy and translate s into the allocated buffer */
  for (ptr = translation; *s != '\0'; s++) {
    if (*s != '%') {
      *ptr++ = *s;
      continue;
    }
    switch(*++s) {
      case 'l': /* tty port */
        strcpy(ptr, dial_tty);
        ptr += strlen(dial_tty);
        break;

      case 'b': /* baud rate (bbp) */
        strcpy(ptr, P_BAUDRATE);
        ptr += strlen(P_BAUDRATE);
        break;

      case 'f': /* serial port file descriptor */
        sprintf(str_portfd, "%d", portfd);
        strcpy(ptr, str_portfd);
        ptr += strlen(str_portfd);
        break;

      default: /* treat all other escape sequences literally */
        *ptr++ = '%';
        *ptr++ = *s;
        break;
    }
  }
  *ptr = '\0';

  return translation;
}

/*
 * Trim the leading & trailing whitespaces from the string
 * jl 15.09.97
 */
char *trim(char *outstring, char *instring, int n)
{
  char *p;
  char *ip;
  char *op;
  char *np;

  ip = instring;
  np = ip + n;
  while ((*ip <= ' ') && (ip < np))
    ip++;

  op = outstring;
  np = op + n;
  while ((*ip >= ' ') && (op <= np)) {
    *op = *ip;
    ip++;
    op++;
  }

  if (op < np)
    *op = 0;

  while ((op > outstring) && (*op <= ' ')) {
    *op = 0;
    op--;
  }

  p = outstring;
  return p;
}
  
/*
 * Choose from numerous up and download protocols!
 */

void updown(int what, int nr)
{
#ifdef LOG_XFER
  #warning LOG_XFER defined!
  FILE *xfl;
#endif
  const char *name[13];
  int idx[13];
  int r, f, g = 0;
  char *t = what == 'U' ? _("Upload") : _("Download");
  char buf[160];
  char buffirst[20];
  char xfrstr[160] = "";
  char trimbuf[160] = "";
  char title[64];
  const char *s  ="";
  int pipefd[2];
  int n, status;
  char * cmdline = NULL;
  char * translated_cmdline = NULL;
  WIN *win = (WIN *)NULL;

  if (mcd(what == 'U' ? P_UPDIR : P_DOWNDIR) < 0)
    return;

  /* Automatic? */
  if (nr == 0) {
    for (f = 0; f < 12; f++) {
      if (P_PNAME(f)[0] && P_PUD(f) == what) {
        name[g] = P_PNAME(f);
        idx[g++] = f;
      }
    }
    name[g] = NULL;
    if (g == 0)
      return;

    r = mc_wselect(30, 7, name, NULL, t, stdattr, mfcolor, mbcolor) - 1;
    if (r < 0)
      return;

    g = idx[r];
  } else
    g = nr;

  buf[0] = 0;

/* jseymour file selector with choice of dir on zmodem, etc. download */
#if 1
  {
    int multiple; /* 0:only directory, 1:one file, -1:any number */
    size_t cmdline_length;

    if (P_MUL(g)=='Y')
      /* need file(s), or just a directory? */
      multiple = what == 'U'? -1 : 0;
    else
      multiple = 1;	/* only one allowed */

    if (P_FSELW[0] == 'Y' && (what == 'U' || P_ASKDNDIR[0] == 'Y')) {
      s = filedir(multiple, what == 'U'? 0 : 1);
      if (s == NULL)
        return;
    }
    else if (P_PNN(g) == 'Y') {
      s = input(_("Please enter file names"), buf);
      if (s == NULL)
        return;
    }

    /* discard directory if "multiple" == 0 */
    cmdline_length = strlen(P_PPROG(g)) + strlen((char*) (multiple == 0 ? "" : s)) + 1; /* + 1 for ' ' */
    cmdline = malloc(cmdline_length + 1); /* + 1 for NUL */
    if (cmdline == NULL) {
      werror(_("Out of memory: could allocate buffer for command line"));
      return;
    }
    snprintf(cmdline, cmdline_length + 1, "%s %s", P_PPROG(g), multiple == 0 ? "" : s);
  }
#endif

  if (P_LOGXFER[0] == 'Y')
    do_log("%s", cmdline);   /* jl 22.06.97 */

  if (P_PFULL(g) == 'N') {
    win = mc_wopen(10, 7, 70, 13, BSINGLE, stdattr, mfcolor, mbcolor, 1, 0, 1);
    snprintf(title, sizeof(title), _("%.30s %s - Press CTRL-C to quit"), P_PNAME(g),
             what == 'U' ? _("upload") : _("download"));
    mc_wtitle(win, TMID, title);
    if (pipe(pipefd) == -1)
      werror("pipe() call failed");
  } else
    mc_wleave();

  m_flush(portfd);

  switch (udpid = fork()) {
    case -1:
      werror(_("Out of memory: could not fork()"));
      if (win) {
        close(pipefd[0]);
        close(pipefd[1]);
        mc_wclose(win, 1);
      } else
        mc_wreturn();
      mcd("");
      if(cmdline)
        free(cmdline);
      return;
    case 0: /* Child */
      if (P_PIORED(g) == 'Y') {
        dup2(portfd, 0);
        dup2(portfd, 1);
      }
      if (win) {
        dup2(pipefd[1], 2);
        close(pipefd[0]);
        if (pipefd[1] != 2)
          close(pipefd[1]);
      }

      lockfile_remove();

      for (n = 1; n < _NSIG; n++)
        signal(n, SIG_DFL);

      translated_cmdline = translate(cmdline);
      if (translated_cmdline != NULL) {
        fastexec(translated_cmdline);
        free(translated_cmdline);
      }
      if(cmdline)
        free(cmdline);
      exit(1);
    default: /* Parent */
      break;
  }
 
  if(cmdline)
    free(cmdline);

  if (win) {
    setcbreak(1);         /* Cbreak, no echo. */
    enab_sig(1, 0);       /* But enable SIGINT */
  }
  signal(SIGINT, udcatch);
  if (P_PIORED(g) == 'Y') {
    close(pipefd[1]);
#ifdef LOG_XFER
    xfl=fopen("xfer.log","wb");
#endif
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
      buf[n] = '\0';
      mc_wputs(win, buf);
      timer_update();
      /* Log the filenames & sizes 	jl 14.09.97 */
      if (P_LOGXFER[0] == 'Y') {
#ifdef LOG_XFER
        if (xfl)
          fprintf(xfl,">%s<\n",buf);
#endif
        if (sscanf(buf, "%19s", buffirst)) { /* if / jl 29.09.97 */
          if (!strncmp (buffirst, "Receiving", 9) ||
              !strncmp (buffirst, "Sending", 7)) {
            if (xfrstr[0]) {
              trim (trimbuf, xfrstr, sizeof(trimbuf));
              do_log ("%s", trimbuf);
              xfrstr[0] = 0;
            }
            trim (trimbuf, buf, sizeof(trimbuf));
            do_log("%s", trimbuf);
          } else if (!strncmp (buffirst, "Bytes", 5)) {
            strncpy (xfrstr, buf, sizeof(xfrstr));
          }
          buffirst[0] = 0;
          trimbuf[0] = 0;
        }
      }
    }
#ifdef LOG_XFER
    if (xfl)
      fclose(xfl);
#endif
  }
  /* Log the last file size	jl 14.09.97 */
  if (P_LOGXFER[0] == 'Y' && xfrstr[0]) {
    trim (trimbuf, xfrstr, sizeof(trimbuf));
    do_log ("%s", trimbuf);
    xfrstr[0] = 0;
  }

  while (udpid != m_wait(&status));
  if (win) {
    enab_sig(0, 0);
    signal(SIGINT, SIG_IGN);
  }

  if (win == (WIN *)0)
    mc_wreturn();

  lockfile_create();

  /* MARK updated 02/17/94 - Flush modem port before displaying READY msg */
  /* because a BBS often displays menu text right after a download, and we */
  /* don't want the modem buffer to be lost while waiting for key to be hit */
  m_flush(portfd);
  port_init();
  setcbreak(2); /* Raw, no echo. */
  if (win)
    close(pipefd[0]);
  mcd("");
  timer_update();

  /* If we got interrupted, status != 0 */
  if (win && (status & 0xFF00) == 0) {
#if VC_MUSIC
    if (P_SOUND[0] == 'Y') {
      mc_wprintf(win, _("\n READY: press any key to continue..."));
      music();
    } else
      sleep(1);
#else
    /* MARK updated 02/17/94 - If there was no VC_MUSIC capability, */
    /* then at least make some beeps! */
    if (P_SOUND[0] == 'Y')
      mc_wprintf(win, "\007\007\007");
    sleep(1);
#endif
  }
  if (win)
    mc_wclose(win, 1);
}

void lockfile_remove(void)
{
  if (portfd_is_socket)
    return;

#if !HAVE_LOCKDEV
  if (lockfile[0])
    unlink(lockfile);
#else
  ttyunlock(dial_tty);
#endif
}

int lockfile_create(void)
{
  int n;

  if (portfd_is_socket)
    return 0;

#if !HAVE_LOCKDEV
  if (!lockfile[0])
    return 0;

  int fd;
  n = umask(022);
  /* Create lockfile compatible with UUCP-1.2 */
  if ((fd = open(lockfile, O_WRONLY | O_CREAT | O_EXCL, 0666)) < 0) {
    werror(_("Cannot create lockfile!"));
  } else {
    // FHS format:
    char buf[12];
    snprintf(buf, sizeof(buf),  "%10d\n", getpid());
    buf[sizeof(buf) - 1] = 0;
    if (write(fd, buf, strlen(buf)) < (ssize_t)strlen(buf))
      fprintf(stderr, _("Failed to write lockfile %s\n"), lockfile);
    close(fd);
  }
  umask(n);
  return 0;
#else
  n = ttylock(dial_tty);
  if (n < 0) {
    fprintf(stderr, _("Cannot create lockfile for %s: %s\n"), dial_tty, strerror(-n));
  } else if (n > 0) {
    fprintf(stderr, _("Device %s is locked.\n"), dial_tty);
  }
  return n;
#endif
}

/*
 * Run kermit. Used to do this in the main window, but newer
 * versions of kermit are too intelligent and just want a tty
 * for themselves or they won't function ok. Shame.
 */
void kermit(void)
{
  int status, pid, n;
  char * translated_cmdline;
  char *kermit_path = P_KERMIT;

  if (!kermit_path || !*kermit_path) {
    werror("No kermit path defined!");
    return;
  }

  /* Clear screen, set keyboard modes etc. */
  mc_wleave();

  switch (pid = fork()) {
    case -1:
      mc_wreturn();
      werror(_("Out of memory: could not fork()"));
      return;
    case 0: /* Child */
      close(portfd);

      /* Remove lockfile */
      lockfile_remove();

      for (n = 0; n < _NSIG; n++)
        signal(n, SIG_DFL);

      translated_cmdline = translate(P_KERMIT);
      if (translated_cmdline != NULL) {
        fastexec(translated_cmdline);
        free(translated_cmdline);
      }
      exit(1);
    default: /* Parent */
      break;
  }

  m_wait(&status);

  /* Restore screen and keyboard modes */
  mc_wreturn();

  /* Re-create lockfile */
  lockfile_create();

  m_flush(portfd);
  port_init();
}

/* ============ Here begins the setenv function ============= */
/*
 * Compare two strings up to '='
 */
static int varcmp(const char *s1, const char *s2)
{
  while (*s1 && *s2) {
    if (*s1 == '=' && *s2 == '=')
      return 1;
    if (*s1++ != *s2++)
      return 0;
  }
  return 1;
}

/*
 * Generate a name=value string.
 */
static char *makenv(const char *name, const char *value)
{
  char *p;

  if ((p = malloc(strlen(name) + strlen(value) + 3)) == NULL)
    return p;
  sprintf(p, "%s=%s", name, value);
  return p;
}

/*
 * Set a environment variable.
 */
int mc_setenv(const char *name, const char *value)
{
  static int init = 0;
  char *p, **e, **newe;
  int count = 0;

  if ((p = makenv(name, value)) == NULL)
    return -1;

  for (e = environ; *e; e++) {
    count++;
    if (varcmp(p, *e)) {
      *e = p;
      return 0;
    }
  }
  count += 2;
  if ((newe = (char **)malloc(sizeof(char *) * count)) == (char **)0) {
    free(p);
    return -1;
  }
  memcpy((char *)newe, (char *)environ , (int) (count * sizeof(char *)));
  if (init)
    free((char *)environ);
  init = 1;
  environ = newe;
  for(e = environ; *e; e++)
    ;
  *e++ = p;
  *e = NULL;
  return 0;
}

/* ============ This is the end of the setenv function ============= */

/*
 * Run an external script.
 * ask = 1 if first ask for confirmation.
 * s = scriptname, l=loginname, p=password.
 */
void runscript(int ask, const char *s, const char *l, const char *p)
{
  int status;
  int n, i;
  int pipefd[2];
  char buf[81];
  char scr_lines[5];
  char cmdline[128];
  struct pollfd fds[2];
  char *translated_cmdline;
  char *ptr;
  WIN *w;
  int done = 0;
  char *msg = _("Same as last");
  char *username = _(" A -   Username        :"),
       *password = _(" B -   Password        :"),
       *name_of_script = _(" C -   Name of script  :"),
       *question = _("Change which setting?     (Return to run, ESC to stop)");


  if (ask) {
    w = mc_wopen(10, 5, 70, 10, BDOUBLE, stdattr, mfcolor, mbcolor, 0, 0, 1);
    mc_wtitle(w, TMID, _("Run a script"));
    mc_wputs(w, "\n");
    mc_wprintf(w, "%s %s\n", username, scr_user[0] ? msg : "");
    mc_wprintf(w, "%s %s\n", password, scr_passwd[0] ? msg : "");
    mc_wprintf(w, "%s %s\n", name_of_script, scr_name);
    mc_wlocate(w, 4, 5);
    mc_wputs(w, question);
    mc_wredraw(w, 1);

    while (!done) {
      mc_wlocate(w, mbslen (question) + 5, 5);
      n = wxgetch();
      if (islower(n))
        n = toupper(n);
      switch (n) {
        case '\r':
        case '\n':
          if (scr_name[0] == '\0') {
            mc_wbell();
            break;
          }
          mc_wclose(w, 1);
          done = 1;
          break;
        case 27: /* ESC */
          mc_wclose(w, 1);
          return;
        case 'A':
          mc_wlocate(w, mbslen (username) + 1, 1);
          mc_wclreol(w);
          scr_user[0] = 0;
          mc_wgets(w, scr_user, 32, 32);
          break;
        case 'B':
          mc_wlocate(w, mbslen (password) + 1, 2);
          mc_wclreol(w);
          scr_passwd[0] = 0;
          mc_wgets(w, scr_passwd, 32, 32);
          break;
        case 'C':
          mc_wlocate(w, mbslen (name_of_script) + 1, 3);
          mc_wgets(w, scr_name, 32, 32);
          break;
        default:
          break;
      }
    }
  } else {
    strncpy(scr_user, l, sizeof(scr_user));
    strncpy(scr_name, s, sizeof(scr_name));
    strncpy(scr_passwd, p, sizeof(scr_passwd));
  }
  sprintf(scr_lines, "%d", (int) lines);  /* jl 13.09.97 */

  /* Throw away status line if temporary */
  if (tempst) {
    mc_wclose(st, 1);
    tempst = 0;
    st = NULL;
  }
  scriptname(scr_name);

  if (pipe(pipefd) < 0)
    return;

  if (mcd(P_SCRIPTDIR) < 0)
    return;

  snprintf(cmdline, sizeof(cmdline), "%s %s %s %s",
           P_SCRIPTPROG, scr_name, logfname, logfname[0]==0? "": homedir);

  switch (udpid = fork()) {
    case -1:
      werror(_("Out of memory: could not fork()"));
      close(pipefd[0]);
      close(pipefd[1]);
      mcd("");
      return;
    case 0: /* Child */
      dup2(portfd, 0);
      dup2(portfd, 1);
      dup2(pipefd[1], 2);
      close(pipefd[0]);
      close(pipefd[1]);

      for (n = 1; n < _NSIG; n++)
	signal(n, SIG_DFL);

      mc_setenv("LOGIN", scr_user);
      mc_setenv("PASS", scr_passwd);
      mc_setenv("TERMLIN", scr_lines);	/* jl 13.09.97 */
      translated_cmdline = translate(cmdline);

      if (translated_cmdline != NULL) {
        fastexec(translated_cmdline);
        free(translated_cmdline);
      }
      exit(1);
    default: /* Parent */
      break;
  }
  setcbreak(1); /* Cbreak, no echo */
  enab_sig(1, 0);	       /* But enable SIGINT */
  signal(SIGINT, udcatch);
  close(pipefd[1]);

  /* pipe output from "runscript" program to terminal emulator */
  fds[0].fd     = pipefd[0]; /* runscript */
  fds[0].events = POLLIN;
  fds[1].fd     = STDIN_FILENO; /* stdin */
  fds[1].events = POLLIN;
  script_running = 1;
  while (script_running && poll(fds, 2, -1) > 0)
    for (i = 0; i < 2; i++) {
      if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
        script_running = 0;
      else if ((fds[i].revents & POLLIN)
               && (n = read(fds[i].fd, buf, sizeof(buf)-1)) > 0) {
        ptr = buf;
        while (n--)
          if (i)
            vt_send(*ptr++);
          else
            vt_out(*ptr++);
        timer_update();
        mc_wflush();
      }
    }

  /* Collect status, and clean up. */
  m_wait(&status);
  enab_sig(0, 0);
  signal(SIGINT, SIG_IGN);
  setcbreak(2); /* Raw, no echo */
  close(pipefd[0]);
  scriptname("");
  mcd("");
}

/*
* Paste text file to console/serial line. Avoid ascii-xfer problem of 
* swallowing up status messages returned via the serial line.
* This is especially useful for Embedded Microprocessor Development Kits
* that use raw file transfer mode (no protocols) to download text encoded
* executable files (eg., in S-Record or Intel Hex formats)
*
* TC Wan <tcwan@cs.usm.my> 2003-10-18
*/
int paste_file(void)
{
  FILE *fp;
  char line[1024];
  char *s;
  const int dotrans = 0;
  const int ldelay = 1;      /* hardcoded 1 ms */
  char buf[128] = "";
  char *ptr;
  int bytes_read;
  unsigned long bdone = 0;
  int x;

  if ((s = filedir(1, 0)) == NULL)
    return 0;
  if ((fp = fopen(s, "r")) == NULL) {
    perror(s);
    return -1;
  }

  while (fgets(line, sizeof(line), fp)) {
    /* Check for I/O or timer. */
    x = check_io(portfd_connected, 0, 1000, buf, sizeof(buf), &bytes_read);

    /*  Send data from the modem to the screen. */
    if ((x & 1)) {
      ptr = buf;
      while (bytes_read-- > 0) {
	if (P_PARITY[0] == 'M' || P_PARITY[0] == 'S')
	  *ptr &= 0x7f;
	vt_out(*ptr++);
      }
      mc_wflush();
    }

    if (dotrans && (s = strrchr(line, '\n')) != NULL) {
      if (s > line && *(s - 1) == '\r')
	s--;
      *s = 0;
      s = line;
      for (s = line; *s; s++)
	vt_send(*s);
      vt_send('\r');
      vt_send('\n');
      bdone += strlen(line) + 2;
    } else {
      for (s = line; *s; s++)
	vt_send(*s);
      bdone += strlen(s);
    }
    if (ldelay) {
#ifdef HAVE_USLEEP
      usleep(ldelay * 1000);
#endif
    }
  }
  fclose(fp);
  return 0;
}
