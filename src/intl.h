/* $Id: intl.h,v 1.2 2007-01-07 15:47:14 al-guest Exp $ */

#ifdef HAVE_LOCALE_H
# include <locale.h>
#endif

#define N_(Str) (Str)

#ifdef ENABLE_NLS
# include <libintl.h>
# define _(Text) gettext (Text)
#else
# undef bindtextdomain
# define bindtextdomain(Domain, Directory) /* empty */
# undef textdomain
# define textdomain(Domain) /* empty */
# define _(Text) Text
#endif
