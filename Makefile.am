
EXTRA_DIST = config.rpath FILE_ID.DIZ minicom.spec autogen.sh

SUBDIRS = doc extras man po lib src

ACLOCAL_AMFLAGS = -I m4

AM_CTAGSFLAGS = --globals -l c

## make rpms
rpm:	Makefile
	$(MAKE) dist
	rpm -ta --clean $(PACKAGE)-$(VERSION).tar.gz

## make a Debian binary package
deb: configure
	dpkg-buildpackage -B -rfakeroot -tc -uc

## make a Debian source package
deb-src: configure
	dpkg-buildpackage -S -i -rfakeroot -tc -uc -us
