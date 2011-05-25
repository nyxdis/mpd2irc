/*
 * mpd2irc - MPD->IRC gateway
 *
 * Copyright 2008-2011 Christoph Mende
 * All rights reserved. Released under the 2-clause BSD license.
 */


#ifndef HAVE_PREFERENCES_H
#define HAVE_PREFERENCES_H

struct {
	/* MPD */
	gchar *mpd_server;
	gchar *mpd_password;
	gint mpd_port;

	/* IRC */
	gchar *irc_server;
	gboolean irc_use_ssl;
	gchar *irc_password;
	gchar *irc_channel;
	gchar *irc_nick;
	gchar *irc_realname;
	gchar *irc_username;

	/* IRC auth */
	gchar *irc_auth_serv;
	gchar *irc_auth_string;

	/* general */
	gchar *die_password;

	/* other */
	gboolean announce;
	gboolean foreground;
} prefs;

void parse_config(void);
void parse_args(gint argc, gchar *argv[]);
void prefs_cleanup(void);

#endif /* HAVE_PREFERENCES_H */
