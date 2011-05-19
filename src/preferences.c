/*
 * mpd2irc - MPD->IRC gateway
 *
 * Copyright 2008-2011 Christoph Mende
 * All rights reserved. Released under the 2-clause BSD license.
 */


#include <stdio.h>
#include <stdlib.h>

#include <glib.h>

#include "preferences.h"
#include "config.h"

static void print_version(void);

void parse_config(void)
{
	GError *error = NULL;
	GKeyFile *config = g_key_file_new();

	if (!g_key_file_load_from_file(config, "mpd2irc.conf", 0, &error)) {
		g_warning("Failed to parse configuration file: %s",
				error->message);
		return;
	}

	/* MPD */
	prefs.mpd_server = g_key_file_get_string(config, "mpd", "server", NULL);
	if (!prefs.mpd_server)
		prefs.mpd_server = g_strdup("localhost");

	prefs.mpd_password = g_key_file_get_string(config, "mpd", "password",
			NULL);
	prefs.mpd_port = g_key_file_get_integer(config, "mpd", "port", NULL);
	if (!prefs.mpd_port)
		prefs.mpd_port = 6600;

	/* IRC */
	prefs.irc_server = g_key_file_get_string(config, "irc", "server", NULL);
	prefs.irc_use_ssl = g_key_file_get_boolean(config, "irc", "use_ssl",
			NULL);
	prefs.irc_password = g_key_file_get_string(config, "irc", "password",
			NULL);
	prefs.irc_channel = g_key_file_get_string(config, "irc", "channel",
			NULL);
	prefs.irc_nick = g_key_file_get_string(config, "irc", "nick", NULL);
	if (!prefs.irc_nick)
		prefs.irc_nick = g_strdup(PACKAGE_NAME);

	prefs.irc_realname = g_key_file_get_string(config, "irc", "realname",
			NULL);
	if (!prefs.irc_realname)
		prefs.irc_realname = g_strdup(PACKAGE_STRING);

	prefs.irc_username = g_key_file_get_string(config, "irc", "username",
			NULL);
	if (!prefs.irc_username)
		prefs.irc_username = g_strdup(PACKAGE_NAME);

	/* IRC auth */
	prefs.irc_auth_serv = g_key_file_get_string(config, "irc", "authserv",
			NULL);
	prefs.irc_auth_string = g_key_file_get_string(config, "irc", "string",
			NULL);

	/* general */
	prefs.die_password = g_key_file_get_string(config, "general",
			"die_password", NULL);

	g_key_file_free(config);

	prefs.announce = TRUE;
}

void parse_args(gint argc, gchar *argv[])
{
	GError *error = NULL;
	gchar *config;
	gboolean version;
	GOptionContext *context;
	GOptionEntry entries[] = {
		{ "config", 'c', 0, G_OPTION_ARG_FILENAME, &config,
			"filename of the config file", "path" },
		{ "version", 'v', 0, G_OPTION_ARG_NONE, &version,
			"print the program version", NULL },
		{ NULL, 0, 0, 0, NULL, NULL, NULL }
	};
	context = g_option_context_new("- the interface between IRC and MPD");
	g_option_context_add_main_entries(context, entries, NULL);
	if (!g_option_context_parse(context, &argc, &argv, &error))
		g_warning("Parsing command line options failed: %s",
				error->message);
	g_option_context_free(context);

	if (version)
		print_version();
}

static void print_version(void)
{
	puts(PACKAGE_STRING);
	puts("The interface between IRC and MPD\n");
	puts("Copyright 2008-2011 Christoph Mende <mende.christoph@gmail.com>");
	puts("All rights reserved. Released under the 2-clause BSD license.");
	exit(EXIT_SUCCESS);
}

void prefs_cleanup(void)
{
	g_free(prefs.mpd_server);
	g_free(prefs.mpd_password);
	g_free(prefs.irc_server);
	g_free(prefs.irc_password);
	g_free(prefs.irc_channel);
	g_free(prefs.irc_nick);
	g_free(prefs.irc_realname);
	g_free(prefs.irc_username);
	g_free(prefs.irc_auth_serv);
	g_free(prefs.irc_auth_string);
	g_free(prefs.die_password);
}
