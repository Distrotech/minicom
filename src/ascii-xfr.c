/*
 * ascii-xfr	Ascii file transfer.
 *
 * Usage:	ascii-xfr -s|-r [-ednv] [-c character delay] [-l line delay]
 *
 * 08.03.98 added a patch from Bo Branten <bosse@ing.umu.se>
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>

#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

/*
 *	Externals.
 */
extern int optind;
extern char *optarg;

/*
 *	Global variables.
 */
int cdelay = 0;
int ldelay = 0;
int dotrans = 1;
int eofchar = 26;
int useeof = 0;
int verbose = 0;
time_t start, last;
unsigned long bdone = 0;

/*
 *	Millisecond delay.
 */
void ms_delay(int ms)
{
#ifdef HAVE_USLEEP
  usleep(1000 * ms);
#endif
}

/*
 *	Output a line and delay if needed.
 */
static void lineout(char const *line, int len)
{
  int ret;

  if (!cdelay) {
    do {
      ret = write(STDOUT_FILENO, line, len);
      if (ret < 0) {
	fprintf(stderr, "Error while writing (errno = %d)\n", errno);
	return;
      }
      len -= ret;
      line += ret;
    } while (len);

  } else {
    while (*line) {
      ret = write(STDOUT_FILENO, line, 1);
      if (ret < 0) {
	fprintf(stderr, "Error while writing (errno = %d)\n", errno);
	return;
      }
      if (ret == 1)
	line++;
      ms_delay(cdelay);
    }
  }
}

/*
 *	Show the up/download statistics.
 */
void stats(int force)
{
  time_t now;
  time_t dif;

  if (!verbose)
    return;

  time(&now);
  dif = now - start;

  if (!force && dif < 2)
    return;
  if (dif < 1)
    dif = 1;
  last = now;

  fprintf(stderr, "\r%.1f Kbytes transferred at %d CPS",
          (float)bdone / 1024, (int)(bdone / dif));
  fflush(stderr);
}

void check_answer(void)
{ /* a patch from Bo Branten <bosse@ing.umu.se> */
  char line[1024];
  int  n;
  fd_set rfds;
  struct timeval tv;

  FD_ZERO (&rfds);
  FD_SET (STDIN_FILENO, &rfds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  while (select (STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0) {
    n = read (STDIN_FILENO, line, sizeof(line));
    if (write(STDERR_FILENO, line, n) == -1)
      break;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
  }
}

/*
 *	Send a file in ASCII mode.
 */
int asend(char *file)
{
  FILE *fp;
  char line[1024];
  char *s;
  int first = 1;
  long cur, len;

  if ((fp = fopen(file, "r")) == NULL) {
    perror(file);
    return -1;
  }

  cur = 0;

  while (fgets(line, sizeof(line) - 2, fp) != NULL) {
    long c = ftell(fp);
    len = c - cur;
    cur = c;
    if (dotrans && (s = strrchr(line, '\n')) != NULL) {
      /* s now points to \n */
      /* if there's a \r before, go there */
      if (s > line && *(s - 1) == '\r')
        {
          s--;
	  len--;
	}
      /* end of line */
      *s++ = '\r';
      *s++ = '\n';
      /* terminate string */
      *s = 0;
      len++;
    }
    lineout(line, len);
    bdone += len;
    if (ldelay)
      ms_delay(ldelay);
    stats(first);
    first = 0;
    check_answer();
  }
  if (useeof) 
    putchar(eofchar);
  fflush(stdout);
  if (isatty (STDOUT_FILENO))
    tcdrain (STDOUT_FILENO);
  fclose(fp);

  return 0;
}

/*
 *	Receive a file in ASCII mode.
 */
int arecv(char *file)
{
  FILE *fp;
  char line[1024];
  char *s;
  int n;
  int first = 1;

  if ((fp = fopen(file, "w")) == NULL) {
    perror(file);
    return -1;
  }

  while ((n = read(STDIN_FILENO, line, sizeof(line))) > 0) {
    for (s = line; n-- >0; s++) {
      if (*s == eofchar)
        break;
      if (dotrans && *s == '\r')
        continue;
      bdone++;
      fputc(*s, fp);
    }
    stats(first);
    first = 0;
    if (*s == eofchar)
      break;
  }
  fclose(fp);

  return 0;
}

void usage(void)
{
  fprintf(stderr, "\
Usage: ascii-xfr -s|-r [-dvn] [-l linedelay] [-c character delay] filename\n\
       -s:  send\n\
       -r:  receive\n\
       -e:  send the End Of File character (default is not to)\n\
       -d:  set End Of File character to Control-D (instead of Control-Z)\n\
       -v:  verbose (statistics on stderr output)\n\
       -n:  do not translate CRLF <--> LF\n\
       Delays are in milliseconds.\n");
  exit(1);
}

int main(int argc, char **argv)
{
  int c;
  int what = 0;
  char *file;
  int ret;

  while ((c = getopt(argc, argv, "srdevnl:c:")) != EOF) {
    switch (c) {
      case 's':
      case 'r':
        what = c;
        break;
      case 'd':
        eofchar = 4; /* Unix, CTRL-D */
        break;
      case 'e':
        useeof = 1;
        break;
      case 'v':
        verbose++;
        break;
      case 'n':
        dotrans = 0;
        break;
      case 'l':
        ldelay = atoi(optarg);
        break;
      case 'c':
        cdelay = atoi(optarg);
        break;
      default:
        usage();
        break;
    }
  }
  if (optind != argc - 1 || what == 0)
    usage();
  file = argv[optind];

  time(&start);
  last = start;

  if (what == 's') {
    fprintf(stderr, "ASCII upload of \"%s\"\n", file);
    if (cdelay || ldelay)
      fprintf(stderr, "Line delay: %d ms, character delay %d ms\n",
              ldelay, cdelay);
    fprintf(stderr, "\n");
    fflush(stderr);
    ret = asend(file);
  } else {
    fprintf(stderr, "ASCII download of \"%s\"\n\n", file);
    fflush(stderr);
    ret = arecv(file);
  }
  if (verbose) {
    stats(1);
    fprintf(stderr, "... Done.\n");
    fflush(stdout);
  }

  return ret < 0 ? 1 : 0;
}

