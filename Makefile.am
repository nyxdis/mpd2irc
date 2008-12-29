AM_CPPFLAGS = @confuse_CFLAGS@
bin_PROGRAMS = mpd2irc
mpd2irc_SOURCES = src/mpd2irc.c
mpd2irc_LDADD = @confuse_LIBS@
DEFS += -DSYSCONFDIR=\"$(sysconfdir)\"
EXTRA_DIST = mpd2irc.conf.example
