/*
 * Runscript    Run a login-or-something script.
 *		A basic like "programming language".
 *		This program also looks like a basic interpreter :
 *		a bit messy. (But hey, I'm no compiler writer :-))
 *
 * Author:	Miquel van Smoorenburg, miquels@drinkel.ow.nl
 *
 * Bugs:	The "expect" routine is, unlike gosub, NOT reentrant !
 *
 *		This file is part of the minicom communications package,
 *		Copyright 1991-1995 Miquel van Smoorenburg,
 *		1997-1999 Jukka Lahtinen
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
 * 10.07.98 jl  Added the log command
 * 05.04.99 jl  The logfile name should also be passed as a parameter
 * 04.03.2002 jl Treat the ^ character between quotes as control code prefix
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdarg.h>

#include "port.h"
#include "minicom.h"
#include "intl.h"

#define OK	0
#define ERR	-1
#define RETURN	1
#define BREAK	2

struct line {
  char *line;
  int labelcount;
  int lineno;
  struct line *next;
};

struct var {
  char *name;
  int value;
  struct var *next;
};

/*
 * Structure describing the script we are currently executing.
 */
struct env {
  struct line *lines;		/* Start of all lines */
  struct var *vars;		/* Start of all variables */
  const char *scriptname;	/* Name of this script */
  int verbose;			/* Are we verbose? */
  jmp_buf ebuf;			/* For exit */
  int exstat;			/* For exit */
};

struct env *curenv;		/* Execution environment */
int gtimeout = 120;		/* Global Timeout */
int etimeout = 0;		/* Timeout in expect routine */
jmp_buf ejmp;			/* To jump to if expect times out */
int inexpect = 0;		/* Are we in the expect routine */
const char *newline;		/* What to print for '\n'. */
const char *s_login = "name";	/* User's login name */
const char *s_pass = "password";/* User's password */
struct line *thisline;		/* Line to be executed */
int laststatus = 0;		/* Status of last command */
char homedir[256];		/* Home directory */
char logfname[PARS_VAL_LEN];	/* Name of logfile */

static char inbuf[65];		/* Input buffer. */

/* Forward declarations */
int s_exec(char *);
int execscript(const char *);

/*
 * Walk through the environment, see if LOGIN and/or PASS are present.
 * If so, delete them. (Someone using "ps" might see them!)
 */
void init_env(void)
{
  extern char **environ;
  char **e;

  for (e = environ; *e; e++) {
    if (!strncmp(*e, "LOGIN=", 6)) {
      s_login = *e + 6;
      *e = "LOGIN=";
    }
    if (!strncmp(*e, "PASS=", 5)) {
      s_pass = *e + 5;
      *e = "PASS=";
    }
  }
}


/*
 * Return an environment variable.
 */
const char *mygetenv(char *env)
{
  if (!strcmp(env, "LOGIN"))
    return s_login;
  if (!strcmp(env, "PASS"))
    return s_pass;
  return getenv(env);
}

/*
 * Display a syntax error and exit.
 */
void syntaxerr(const char *s)
{
  fprintf(stderr, _("script \"%s\": syntax error in line %d %s%s\n"),
          curenv->scriptname, thisline->lineno, s, "\r");
  exit(1);
}

/*
 * Skip all space
 */
void skipspace(char **s)
{
  while (**s == ' ' || **s == '\t')
    (*s)++;
}

/*
 * Our clock. This gets called every second.
 */
void myclock(int dummy)
{
  (void)dummy;
  signal(SIGALRM, myclock);
  alarm(1);

  if (--gtimeout == 0) {
    fprintf(stderr, _("script \"%s\": global timeout%s\n"),
            curenv->scriptname,"\r");
    exit(1);
  }
  if (inexpect && etimeout && --etimeout == 0)
    siglongjmp(ejmp, 1);
}

static char *buffer; /* The buffer is only growing and never freed... */
static unsigned buffersize;

static void buf_wr(unsigned idx, char val)
{
  if (idx >= buffersize)
    {
      buffersize += 64;
      buffer = realloc(buffer, buffersize);
    }
  buffer[idx] = val;
}

static inline char buf_rd(unsigned idx)
{
  return buffer[idx];
}

static unsigned bufsize()
{
  return buffersize;
}

static inline char *buf()
{
  return buffer;
}

/*
 * Read a word and advance pointer.
 * Also processes quoting, variable substituting, and \ escapes.
 */
char *getword(char **s)
{
  unsigned int len;
  int f;
  int idx = 0;
  const char *t = *s;
  int sawesc = 0;
  int sawq = 0;
  const char *env;
  char envbuf[32];

  if (**s == 0)
    return NULL;

  for (len = 0; ; len++) {
    if (sawesc && t[len]) {
      sawesc = 0;
      if (t[len] <= '7' && t[len] >= '0') {
        buf_wr(idx, 0);
        for (f = 0; f < 4 && len < bufsize() && t[len] <= '7' &&
             t[len] >= '0'; f++)
          buf_wr(idx, 8 * buf_rd(idx) + t[len++] - '0');
        if (buf_rd(idx) == 0)
          buf_wr(idx, '@');
        idx++;
        len--;
        continue;
      }
      switch (t[len]) {
        case 'r':
          buf_wr(idx++, '\r');
          break;
        case 'n':
          buf_wr(idx++, '\n');
          break;
        case 'b':
          buf_wr(idx++, '\b');
          break;
        case 'a':
          buf_wr(idx++, '\a');
          break;
        case 'f':
          buf_wr(idx++, '\f');
          break;
        case 'c':
          buf_wr(idx++, 255);
          break;
        default:
          buf_wr(idx++, t[len]);
          break;
      }
      sawesc = 0;
      continue;
    }
    if (t[len] == '\\') {
      sawesc = 1;
      continue;
    }
    if (t[len] == '"') {
      if (sawq == 1) {
        sawq = 0;
        len++;
        break;
      }
      sawq = 1;
      continue;
    }
    if (t[len] == '$' && t[len + 1] == '(') {
      for(f = len; t[f] && t[f] != ')'; f++)
        ;
      if (t[f] == ')') {
        strncpy(envbuf, &t[len + 2], f - len - 2);
        envbuf[f - len - 2] = 0;
        len = f;
        env = mygetenv(envbuf);
        if (env == NULL)
          env = "";
        while (*env)
          buf_wr(idx++, *env++);
        continue;
      }
    }
    /* ^ prefix for control chars - jl 3.2002 */
    if (sawq == 1 && t[len] == '^' && t[len + 1] != 0) {
      char c = toupper(t[len + 1]);
      if (c >= 'A' && c <= '_') {
        len++;
        buf_wr(idx++, c - 'A' + 1);
        continue;
      }
    }
    if ((!sawq && (t[len] == ' ' || t[len] == '\t')) || t[len] == 0)
      break;
    buf_wr(idx++, t[len]);
  }
  buf_wr(idx, 0);
  *s += len;
  skipspace(s);
  if (sawesc || sawq)
    syntaxerr(_("(word contains ESC or quote)"));
  return buf();
}

/*
 * Save a string to memory. Strip trailing '\n'.
 */
char *strsave(char *s)
{
  char *t;
  int len;

  len = strlen(s);
  if (len && s[len - 1] == '\n')
    s[--len] = 0;
  if (!(t = malloc(len + 1)))
    return t;
  strcpy(t, s);
  return t;
}

/*
 * Throw away all malloced memory.
 */
void freemem(void)
{
  struct line *l, *nextl;
  struct var *v, *nextv;

  for (l = curenv->lines; l; l = nextl) {
    nextl = l->next;
    free(l->line);
    free(l);
  }
  for (v = curenv->vars; v; v = nextv) {
    nextv = v->next;
    free(v->name);
    free(v);
  }
}

/*
 * Read a script into memory.
 */
static int readscript(const char *s)
{
  FILE *fp;
  struct line *tl, *prev = NULL;
  char *t;
  char buf[500]; /* max length of a line - this should be dynamically! */
  int lineno = 0;

  if ((fp = fopen(s, "r")) == NULL) {
    fprintf(stderr, _("runscript: couldn't open \"%s\"%s\n"), s, "\r");
    exit(1);
  }

  /* Read all the lines into a linked list in memory. */
  while ((t = fgets(buf, sizeof(buf), fp)) != NULL) {
    lineno++;
    if (strlen(t) == sizeof(buf) - 1) {
      /* Wow, this is really braindead, once upon a time buf was 81 chars
       * big and triggered nice errors for too long input lines, now
       * we just enlarge the buffer and add a sanity check. This code
       * needs to allocate memory dynamically... */
      fprintf(stderr, "Input line %u too long, aborting (and fix me!)!\n",
	      lineno);
      exit(1);
    }
    skipspace(&t);
    if (*t == '\n' || *t == '#')
      continue;
    if (((tl = (struct line *)malloc(sizeof (struct line))) == NULL) ||
        ((tl->line = strsave(t)) == NULL)) {
      fprintf(stderr, _("script \"%s\": out of memory%s\n"), s, "\r");
      exit(1);
    }
    if (prev)
      prev->next = tl;
    else
      curenv->lines = tl;
    tl->next = NULL;
    tl->labelcount = 0;
    tl->lineno = lineno;
    prev = tl;
  }
  fclose(fp);
  return 0;
}

/* Read one character, and store it in the buffer. */
void readchar(void)
{
  char c;
  int n;

  while ((n = read(0, &c, 1)) != 1)
    if (errno != EINTR)
      break;

  if (n <= 0)
    return;

  /* Shift character into the buffer. */
#ifdef _SYSV
  memcpy(inbuf, inbuf + 1, 63);
#else
#  ifdef _BSD43
  bcopy(inbuf + 1, inbuf, 63);
#  else
  /* This is Posix, I believe. */
  memmove(inbuf, inbuf + 1, 63);
#  endif
#endif
  if (curenv->verbose)
    fputc(c, stderr);
  inbuf[63] = c;
}

/* See if a string just came in. */
int expfound(const char *word)
{
  int len;

  if (word == NULL) {
    fprintf(stderr, "NULL paramenter to %s!", __func__);
    exit(1);
  }

  len = strlen(word);
  if (len > 64)
    len = 64;

  return !strcmp(inbuf + 64 - len, word);
}

/*
 * Send text to a file (stdout or stderr).
 */
int output(char *text, FILE *fp)
{
  unsigned char *w;
  int first = 1;
  int donl = 1;

  while ((w = (unsigned char *)getword(&text)) != NULL) {
    if (!first)
      fputc(' ', fp);
    first = 0;
    for(; *w; w++) {
      if (*w == 255) {
        donl = 0;
        continue;
      }
      if (*w == '\n')
        fputs(newline, fp);
      else
        fputc(*w, fp);
    }
  }
  if (donl)
    fputs(newline, fp);
  fflush(fp);
  return OK;
}

/*
 * Find a variable in the list.
 * If it is not there, create it.
 */
struct var *getvar(char *name, int cr)
{
  struct var *v, *end = NULL;

  for (v = curenv->vars; v; v = v->next) {
    end = v;
    if (!strcmp(v->name, name))
      return v;
  }
  if (!cr) {
    fprintf(stderr, _("script \"%s\" line %d: unknown variable \"%s\"%s\n"),
            curenv->scriptname, thisline->lineno, name, "\r");
    exit(1);
  }
  if ((v = (struct var *)malloc(sizeof(struct var))) == NULL ||
      (v->name = strsave(name)) == NULL) {
    fprintf(stderr, _("script \"%s\": out of memory%s\n"),
            curenv->scriptname, "\r");
    exit(1);
  }
  if (end)
    end->next = v;
  else
    curenv->vars = v;
  v->next = NULL;
  v->value = 0;
  return v;
}

/*
 * Read a number or variable.
 */
int getnum(char *text)
{
  int val;

  if (!strcmp(text, "$?"))
    return laststatus;
  if ((val = atoi(text)) != 0 || *text == '0')
    return val;
  return getvar(text, 0)->value;
}


/*
 * Get the lines following "expect" into memory.
 */
struct line **buildexpect(void)
{
  static struct line *seq[17];
  int f;
  char *w, *t;

  for(f = 0; f < 16; f++) {
    if (thisline == NULL) {
      fprintf(stderr, _("script \"%s\": unexpected end of file%s\n"),
              curenv->scriptname, "\r");
      exit(1);
    }
    t = thisline->line;
    w = getword(&t);
    if (!strcmp(w, "}")) {
      if (*t)
        syntaxerr(_("(garbage after })"));
      seq[f] = NULL;
      return seq;
    }
    seq[f] = thisline;
    thisline = thisline->next;
  }
  if (f == 16)
    syntaxerr(_("(too many arguments)"));
  return seq;
}

/*
 * Our "expect" function.
 */
int expect(char *text)
{
  char *s, *w;
  struct line **volatile seq;
  struct line oneline;
  struct line *dflseq[2];
  char *volatile toact = "exit 1";
  volatile int found = 0;
  int f, val, c;
  char *action = NULL;

  if (inexpect) {
    fprintf(stderr, _("script \"%s\" line %d: nested expect%s\n"),
            curenv->scriptname, thisline->lineno, "\r");
    exit(1);
  }
  etimeout = 120;
  inexpect = 1;

  s = getword(&text);
  if (!strcmp(s, "{")) {
    if (*text)
      syntaxerr(_("(garbage after {)"));
    thisline = thisline->next;
    seq = buildexpect();
  } else {
    oneline.line = s;
    oneline.next = NULL;
    dflseq[0] = &oneline;
    dflseq[1] = NULL;
    seq = dflseq;
  }
  /* Seek a timeout command */
  for (f = 0; seq[f]; f++) {
    if (!strncmp(seq[f]->line, "timeout", 7)) {
      c = seq[f]->line[7];
      if (c == 0 || (c != ' ' && c != '\t'))
        continue;
      s = seq[f]->line + 7;
      /* seq[f] = NULL; */
      skipspace(&s);
      w = getword(&s);
      if (w == NULL)
        syntaxerr(_("(argument expected)"));
      val = getnum(w);
      if (val == 0)
        syntaxerr(_("(invalid argument)"));
      etimeout = val;
      skipspace(&s);
      if (*s != 0)
        toact = s;
      break;
    }
  }
  if (sigsetjmp(ejmp, 1) != 0) {
    f = s_exec(toact);
    inexpect = 0;
    return f;
  }

  /* Allright. Now do the expect. */
  c = OK;
  while (!found) {
    action = NULL;
    readchar();
    for (f = 0; seq[f]; f++) {
      s = seq[f]->line;
      w = getword(&s);
      if (expfound(w)) {
        action = s;
        found = 1;
        break;
      }
    }
    if (action != NULL && *action) {
      found = 0;
      /* Maybe BREAK or RETURN */
      if ((c = s_exec(action)) != OK)
        found = 1;
    }
  }
  inexpect = 0;
  etimeout = 0;
  return c;
}

/*
 * Jump to a shell and run a command.
 */
int shell(char *text)
{
  laststatus = system(text);
  return OK;
}

/*
 * Send output to stdout ( = modem)
 */
int dosend(char *text)
{
#ifdef HAVE_USLEEP
  /* 200 ms delay. */
  usleep(200000);
#endif

  /* Before we send anything, flush input buffer. */
  m_flush(0);
  memset(inbuf, 0, sizeof(inbuf));

  newline = "\r";
  return output(text, stdout);
}

/*
 * Exit from the script, possibly with a value.
 */
int doexit(char *text)
{
  char *w;
  int ret = 0;

  w = getword(&text);
  if (w != NULL)
    ret = getnum(w);
  curenv->exstat = ret;
  longjmp(curenv->ebuf, 1);
  return 0;
}

/*
 * Goto a specific label.
 */
int dogoto(char *text)
{
  char *w;
  struct line *l;
  char buf[32];
  int len;

  w = getword(&text);
  if (w == NULL || *text)
    syntaxerr(_("(in goto/gosub label)"));
  snprintf(buf, sizeof(buf), "%s:", w);
  len = strlen(buf);
  for (l = curenv->lines; l; l = l->next)
    if (!strncmp(l->line, buf, len))
      break;
  if (l == NULL) {
    fprintf(stderr, _("script \"%s\" line %d: label \"%s\" not found%s\n"),
            curenv->scriptname, thisline->lineno, w, "\r");
    exit(1);
  }
  thisline = l;
  /* We return break, to automatically break out of expect loops. */
  return BREAK;
}

/*
 * Goto a subroutine.
 */
int dogosub(char *text)
{
  struct line *oldline;
  int ret = OK;

  oldline = thisline;
  dogoto(text);

  while (ret != ERR) {
    if ((thisline = thisline->next) == NULL) {
      fprintf(stderr, _("script \"%s\": no return from gosub%s\n"),
              curenv->scriptname, "\r");
      exit(1);
    }
    ret = s_exec(thisline->line);
    if (ret == RETURN) {
      ret = OK;
      thisline = oldline;
      break;
    }
  }
  return ret;
}

/*
 * Return from a subroutine.
 */
int doreturn(char *text)
{
  (void)text;
  return RETURN;
}

/*
 * Print text to stderr.
 */
int print(char *text)
{
  newline = "\r\n";

  return output(text, stderr);
}

/*
 * Declare a variable (integer)
 */
int doset(char *text)
{
  char *w;
  struct var *v;

  w = getword(&text);
  if (w == NULL)
    syntaxerr(_("(missing var name)"));
  v = getvar(w, 1);
  if (*text)
    v->value = getnum(getword(&text));
  return OK;
}

/*
 * Lower the value of a variable.
 */
int dodec(char *text)
{
  char *w;
  struct var *v;

  w = getword(&text);
  if (w == NULL)
    syntaxerr(_("(expected variable)"));
  v = getvar(w, 0);
  v->value--;
  return OK;
}

/*
 * Increase the value of a variable.
 */
int doinc(char *text)
{
  char *w;
  struct var *v;

  w = getword(&text);
  if (w == NULL)
    syntaxerr(_("(expected variable)"));
  v = getvar(w, 0);
  v->value++;
  return OK;
}

/*
 * If syntax: if n1 [><=] n2 command.
 */
int doif(char *text)
{
  char *w;
  int n1;
  int n2;
  char op;

  if ((w = getword(&text)) == NULL)
    syntaxerr("(if)");
  n1 = getnum(w);
  if ((w = getword(&text)) == NULL)
    syntaxerr("(if)");
  if (strcmp(w, "!=") == 0)
    op = '!';
  else {
    if (*w == 0 || w[1] != 0)
      syntaxerr("(if)");
    op = *w;
  }
  if ((w = getword(&text)) == NULL)
    syntaxerr("(if)");
  n2 = getnum(w);
  if (!*text)
    syntaxerr(_("(expected command after if)"));

  if (op == '=') {
    if (n1 != n2)
      return OK;
  } else if (op == '!') {
    if (n1 == n2)
      return OK;
  } else if (op == '>') {
    if (n1 <= n2)
      return OK;
  } else if (op == '<') {
    if (n1 >= n2)
      return OK;
  } else
    syntaxerr(_("(unknown operator)"));

  return s_exec(text);
}

/*
 * Set the global timeout-time.
 */
int dotimeout(char *text)
{
  char *w;
  int val;

  w = getword(&text);
  if (w == NULL)
    syntaxerr(_("(argument expected)"));
  if ((val = getnum(w)) == 0)
    syntaxerr(_("(invalid argument)"));
  gtimeout = val;
  return OK;
}

/*
 * Turn verbose on/off (= echo stdin to stderr)
 */
int doverbose(char *text)
{
  char *w;

  curenv->verbose = 1;

  if ((w = getword(&text)) != NULL) {
    if (!strcmp(w, "on"))
      return OK;
    if (!strcmp(w, "off")) {
      curenv->verbose = 0;
      return OK;
    }
  }
  syntaxerr(_("(unexpected argument)"));
  return ERR;
}

/*
 * Sleep for a certain number of seconds.
 */
int dosleep(char *text)
{
  int foo, tm;

  tm = getnum(text);
  foo = gtimeout - tm;

  /* The alarm goes off every second.. */
  while (gtimeout != foo)
    pause();
  return OK;
}

/*
 * Break out of an expect loop.
 */
int dobreak(char *dummy)
{
  (void)dummy;
  if (!inexpect) {
    fprintf(stderr, _("script \"%s\" line %d: break outside of expect%s\n"),
            curenv->scriptname, thisline->lineno, "\r");
    exit(1);
  }
  return BREAK;
}

/*
 * Call another script!
 */
int docall(char *text)
{
  struct line *oldline;
  struct env *oldenv;
  int er;

  if (*text == 0)
    syntaxerr(_("(argument expected)"));

  if (inexpect) {
    fprintf(stderr, _("script \"%s\" line %d: call inside expect%s\n"),
            curenv->scriptname, thisline->lineno, "\r");
    exit(1);
  }

  oldline = thisline;
  oldenv = curenv;
  if ((er = execscript(text)) != 0)
    exit(er);
  /* freemem(); */
  thisline = oldline;
  curenv = oldenv;
  return 0;
}

static int do_log_wrapper(char *s)
{
  do_log("%s", s);
  return 0;
}

/* KEYWORDS */
struct kw {
  const char *command;
  int (*fn)(char *);
} keywords[] = {
  { "expect",	expect },
  { "send",	dosend },
  { "!",	shell },
  { "goto",	dogoto },
  { "gosub",	dogosub },
  { "return",	doreturn },
  { "exit",	doexit },
  { "print",	print },
  { "set",	doset },
  { "inc",	doinc },
  { "dec",	dodec },
  { "if",	doif },
  { "timeout",	dotimeout },
  { "verbose",	doverbose },
  { "sleep",	dosleep },
  { "break",	dobreak },
  { "call",	docall },
  { "log",	do_log_wrapper },
  { NULL,	(int(*)(char *))0 }
};

/*
 * Execute one line.
 */
int s_exec(char *text)
{
  char *w;
  struct kw *k;

  w = getword(&text);

  /* If it is a label or a comment, skip it. */
  if (w == NULL || *w == '#' || w[strlen(w) - 1] == ':')
    return OK;

  /* See which command it is. */
  for (k = keywords; k->command; k++)
    if (!strcmp(w, k->command))
      break;

  /* Command not found? */
  if (k->command == NULL) {
    fprintf(stderr, _("script \"%s\" line %d: unknown command \"%s\"%s\n"),
            curenv->scriptname, thisline->lineno, w, "\r");
    exit(1);
  }
  return (*(k->fn))(text);
}

/*
 * Run the script by continously executing "thisline".
 */
int execscript(const char *s)
{
  volatile int ret = OK;

  curenv = (struct env *)malloc(sizeof(struct env));
  curenv->lines = NULL;
  curenv->vars  = NULL;
  curenv->verbose = 1;
  curenv->scriptname = s;

  if (readscript(s) < 0) {
    freemem();
    free(curenv);
    return ERR;
  }

  signal(SIGALRM, myclock);
  alarm(1);
  if (setjmp(curenv->ebuf) == 0) {
    thisline = curenv->lines;
    while (thisline != NULL && (ret = s_exec(thisline->line)) != ERR)
      thisline = thisline->next;
  } else
    ret = curenv->exstat ? ERR : 0;
  free(curenv);
  return ret;
}

void do_args(int argc, char **argv)
{
  if (argc > 1 && !strcmp(argv[1], "--version")) {
    printf(_("runscript, part of minicom version %s\n"), VERSION);
    exit(0);
  }

  if (argc < 2) {
    fprintf(stderr, _("Usage: runscript <scriptfile> [logfile [homedir]]%s\n"),"\r");
    exit(1);
  }
}

int main(int argc, char **argv)
{
  char *s;
#if 0 /* Shouldn't need this.. */
  signal(SIGHUP, SIG_IGN);
#endif

  /* initialize locale support */
  setlocale(LC_ALL, "");
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);

  init_env();

  do_args(argc, argv);

  memset(inbuf, 0, sizeof(inbuf));

  if (argc > 2) {
    strncpy(logfname, argv[2], sizeof(logfname));
    if (argc > 3)
      strncpy(homedir, argv[3], sizeof(homedir));
    else if ((s = getenv("HOME")) != NULL)
      strncpy(homedir, s, sizeof(homedir));
    else
      homedir[0] = 0;
  }
  else
    logfname[0] = 0;

  return execscript(argv[1]) != OK;
}
