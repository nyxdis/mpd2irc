/*
 * mpd2irc - MPD->IRC gateway
 *
 * Copyright 2008-2011 Christoph Mende
 * All rights reserved. Released under the 2-clause BSD license.
 */


#include <string.h>

#include <gio/gio.h>
#include <glib.h>

#include "preferences.h"

static gboolean irc_callback(GSocket *socket, GIOCondition condition,
		gpointer user_data);
static void irc_source_attach(void);
static void irc_write(const gchar *fmt, ...);
static void irc_parse(const gchar *buffer);

static GSocketConnection *connection;
static gboolean connected = FALSE;

gboolean irc_connect(void)
{
	GSocketClient *client;

	client = g_socket_client_new();
	connection = g_socket_client_connect_to_host(client, prefs.irc_server,
			6667, NULL, NULL);
	if (!connection)
		return FALSE;

	irc_write("NICK %s", prefs.irc_nick);
	irc_write("USER %s 0 * :%s", prefs.irc_username, prefs.irc_realname);

	irc_source_attach();

	return TRUE;
}

/*
void irc_parse(const gchar *line)
{
	(void)line;
	* ERROR: Closing Link
	 PING
	 !next
	 !prev
	 !pause
	 !status
	 !np
	 !version
	 !announce
	 !play
	 !stop
	 !random
	 !repeat
	 die <die password> *
	irc_say("foo");
}
*/

void irc_say(const gchar *msg)
{
	irc_write("PRIVMSG %s :%s", prefs.irc_channel, msg);
}

static void irc_write(const gchar *fmt, ...)
{
	GSocket *socket;
	gchar *tmp1, *tmp2;
	va_list ap;

	socket = g_socket_connection_get_socket(connection);

	va_start(ap, fmt);
	tmp1 = g_strdup_vprintf(fmt, ap);
	va_end(ap);

	tmp2 = g_strconcat(tmp1, "\n", NULL);
	g_free(tmp1);

	g_socket_send(socket, tmp2, strlen(tmp2), NULL, NULL);
	g_free(tmp2);
}

static void irc_source_attach(void)
{
	GSocket *socket = g_socket_connection_get_socket(connection);
	GSource *source = g_socket_create_source(socket, G_IO_IN, NULL);
	g_source_set_callback(source, (GSourceFunc) irc_callback, NULL, NULL);
	g_source_attach(source, NULL);
}

static gboolean irc_callback(GSocket *socket,
		G_GNUC_UNUSED GIOCondition condition,
		G_GNUC_UNUSED gpointer user_data)
{
	gchar c;
	GString *buffer = g_string_new("");

	do {
		g_socket_receive(socket, &c, 1, NULL, NULL);
		g_string_append_c(buffer, c);
	} while (c != '\n');

	/* TODO strip CRLF */
	irc_parse(buffer->str);
	g_string_free(buffer, TRUE);

	return TRUE;
}

static void irc_parse(const gchar *buffer)
{
	if (!connected) {
		gchar *tmp = g_strdup_printf(" 001 %s :", prefs.irc_nick);
		if (strstr(buffer, tmp)) {
			irc_write("JOIN %s", prefs.irc_channel);
			connected = TRUE;
		}
	}

	g_message("%s", buffer);
}
