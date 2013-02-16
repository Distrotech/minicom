/*
**
**  Do shell-style pattern matching for ?, \, [], and * characters.
**  Might not be robust in face of malformed patterns; e.g., "foo[a-"
**  could cause a segmentation violation.  It is 8bit clean.
**
**  Written by Rich $alz, mirror!rs, Wed Nov 26 19:03:17 EST 1986.
**  Special thanks to Lars Mathiesen for the ABORT code.  This can greatly
**  speed up failing wildcard patterns.  For example:
**	pattern: -*-*-*-*-*-*-12-*-*-*-m-*-*-*
**	text 1:	 -adobe-courier-bold-o-normal--12-120-75-75-m-70-iso8859-1
**	text 2:	 -adobe-courier-bold-o-normal--12-120-75-75-p-70-iso8859-1
**  Text 1 matches with 51 calls, while text 2 fails with 54 calls.  Without
**  the ABORT, then it takes 22310 calls to fail.  Ugh.
**
**  bernie    613-01 91/01/04 19:34
**  Fixed problem with terminating * not matching with (null)
**
**  bernie    597-00 91/01/08 11:24
**  Fixed shell glob negate from '^' to '!'
**
**  bernie    597-02 91/01/21 13:43
**	Fixed . matching * or ? on first char.
**
**  jseymour  91/02/05 22:56
**	Fixed problems with ill-formed sets in pattern yielding false
**	matches.  Should now be robust in such cases (not all possibilities
**	tested - standard disclaimer).  Added stand-alone debug code.
**
**  jseymour  91/03/28 20:50
**	Re-fixed problems with ill-formed sets in pattern yielding false
**	matches - one hopes correctly this time.
**
**  jseymour  1998/04/04 17:45 EST
**	Before adding this to minicom (as part of my "getsdir()" addition),
**	I emailed a request to Rich $alz, asking if I could "GPL" it.  Here
**	is his reply:
**
**	    Date: Mon, 30 Mar 1998 10:50:06 -0500 (EST)
**	    Message-Id: <199803301550.KAA14018@<anonymized>.com>
**	    From: Rich Salz <salzr@<anonymized>.com>
**	    To: jseymour@jimsun.LinxNet.com
**	    Subject: Re: A Little Thing Named "Wildmat"
**
**	    Wildmat is in the public domain -- enjoy it.
**
**	    I would rather it not get encumbered with various licenses,
**	    but since it is in the public domain you can do what you want...
**
**	So there you go.  (I anonymized his email address as I don't know
**	that he was particularly interested in having it "advertised".)
*/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "minicom.h"

#ifndef TRUE
#define TRUE	 1
#endif
#ifndef FALSE
#define FALSE	 0
#endif
#define ABORT	-1

static int Star(const char *, const char *);
static int DoMatch(const char *, const char *);

static int Star(const char *s, const char *p)
{
  int retval;

  while ((retval = DoMatch(s, p)) == FALSE)	/* gobble up * match */
    if (*++s == '\0')
      return ABORT;
  return retval;
}

/* match string "s" to pattern "p" */
static int DoMatch(const char *s, const char *p)
{
  register int last;
  register int matched;
  register int reverse;
  const char *ss;
  int escaped;

  for (; *p; s++, p++) {                        /* parse the string to end */

    if (*s == '\0')
      return *p == '*' && *++p == '\0' ? TRUE : ABORT;

    switch (*p) {			        /* parse pattern */

      case '\\':
        /* Literal match with following character. */
        p++;
        /* FALLTHROUGH */

      default:		/*literal match*/
        if (*s != *p)
          return FALSE;
        continue;

      case '?':
        /* Match anything. */
        continue;

      case '*':
        /* Trailing star matches everything. */
        return *++p ? Star(s, p) : TRUE;

      case '[':
        /* [!....] means inverse character class. */
        if ((reverse = (p[1] == '!')))
          p++;
        ss = p + 1;	/* set start point */

        for (last = 0400, escaped = matched = FALSE; *++p; last = *p) {
          if (*p == ']' && !(escaped || p == ss))
            break;
          if (escaped)
            escaped = FALSE;
          else if (*p == '\\') {
            escaped = TRUE;
            continue;
          }

          /* This next line requires a good C compiler.	    */
          /*     range?	(in bounds)                 (equal) */
          if ((*p == '-') ? (*s <= *++p && *s >= last) : (*s == *p))
            matched = TRUE;
        }

        if (matched == reverse)
          return FALSE;
        continue;
    }
  }

  return *s == '\0';
}


/*
 * usage: wildmat(string, pattern)
 *
 * returns: non-0 on match
 */
int wildmat(const char *s, const char *p)
{
  if ((*p == '?' || *p == '*') && *s == '.') {
    return FALSE;
  } else {
    return DoMatch(s, p) == TRUE;
  }
}

#ifdef STAND_ALONE_TEST
#include <stdio.h>

/*
 * usage: wildmat <pattern> <test arg(s)>
 */

int main(int argc, char **argv)
{
  int index;
  int status = FALSE;

  for (index = 2; index < argc; ++index) {
    if (wildmat(argv[index], argv[1])) {
      if (status)
        fputs(" ", stdout);
      printf("%s", argv[index]);
      status = TRUE;
    }
  }

  printf("%s\n", status ? "" : argv[1]);
  return 0;
}

#endif
