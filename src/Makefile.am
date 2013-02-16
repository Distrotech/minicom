
EXTRA_PROGRAMS = minicom.keyserv

bin_PROGRAMS = minicom runscript ascii-xfr @KEYSERV@

dist_bin_SCRIPTS = xminicom

minicom_SOURCES = minicom.c vt100.c config.c help.c updown.c \
	util.c dial.c window.c wkeys.c ipc.c \
	windiv.c sysdep1.c sysdep1_s.c sysdep2.c rwconf.c main.c \
	file.c getsdir.c wildmat.c common.c

noinst_HEADERS = configsym.h defmap.h \
	getsdir.h intl.h keyboard.h minicom.h \
	port.h vt100.h window.h sysdep.h

runscript_SOURCES = script.c sysdep1_s.c common.c port.h minicom.h

ascii_xfr_SOURCES = ascii-xfr.c

minicom_keyserv_SOURCES = keyserv.c wkeys.c sysdep2.c

MINICOM_LIBPORT = $(top_builddir)/lib/libport.a

LDADD = $(MINICOM_LIBPORT)

confdir = @MINICOM_CONFDIR@

AM_CPPFLAGS = @CPPFLAGS@ -I$(top_srcdir)/lib -DCONFDIR=\"$(confdir)\" \
              -DLOCALEDIR=\"$(datadir)/locale\"

minicom_LDADD = @LIBINTL@ $(LIBICONV) $(MINICOM_LIBPORT) $(LOCKDEV_LIBS)
minicom_keyserv_LDADD = @LIBINTL@ $(MINICOM_LIBPORT)
runscript_LDADD = @LIBINTL@ $(MINICOM_LIBPORT)

EXTRA_DIST = functions.c
