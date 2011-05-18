bin_PROGRAMS = mpd2irc

mpd2irc_SOURCES = src/m2i.c \
		  src/irc.c src/irc.h \
		  src/mpd.c src/mpd.h \
		  src/preferences.c src/preferences.h

mpd2irc_LDADD = $(glib_LIBS) \
		$(gio_LIBS) \
		$(libmpdclient_LIBS)

mpd2irc_CFLAGS = $(glib_CFLAGS) \
		 $(gio_CFLAGS) \
		 $(libmpdclient_CFLAGS)

DEFS += -DSYSCONFDIR=\"$(sysconfdir)\"

EXTRA_DIST = mpd2irc.conf.example
