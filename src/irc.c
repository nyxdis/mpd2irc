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
#include "mpd.h"
#include "config.h"

#define IRC_READ_BUF 2048

static void irc_run(const gchar *command);
void irc_say(const gchar *fmt, ...);
static void irc_connected(GSocketClient *client, GAsyncResult *result,
		gpointer user_data);
static gboolean irc_callback(GSocket *socket, GIOCondition condition,
		gpointer user_data);
static void irc_source_attach(void);
static void irc_write(const gchar *fmt, ...);
static void irc_parse(const gchar *buffer);
static void irc_schedule_reconnect(void);

static GSocketConnection *connection;
static gboolean connected = FALSE;
static GOutputStream *ostream;
static GInputStream *istream;

static int reconnect_source = 0;

gboolean irc_connect(G_GNUC_UNUSED gpointer data)
{
	GSocketClient *client;
	gushort default_port = 6667;

	client = g_socket_client_new();
	g_socket_client_set_tls(client, prefs.irc_use_ssl);
	g_socket_client_set_tls_validation_flags(client, 0);
	if (prefs.irc_use_ssl)
		default_port = 6697;
	g_socket_client_connect_to_host_async(client, prefs.irc_server,
			default_port, NULL, (GAsyncReadyCallback) irc_connected,
			NULL);
	g_object_unref(client);

	return FALSE;
}

static void irc_connected(GSocketClient *client, GAsyncResult *result,
		G_GNUC_UNUSED gpointer user_data)
{
	GError *error = NULL;

	if (reconnect_source > 0) {
		g_source_remove(reconnect_source);
		reconnect_source = 0;
	}

	connection = g_socket_client_connect_finish(client, result, &error);
	if (!connection) {
		g_debug("Failed to connect to IRC: %s", error->message);
		irc_schedule_reconnect();
		return;
	}

	ostream = g_io_stream_get_output_stream(G_IO_STREAM(connection));
	istream = g_io_stream_get_input_stream(G_IO_STREAM(connection));

	irc_write("NICK %s", prefs.irc_nick);
	irc_write("USER %s 0 * :%s", prefs.irc_username, prefs.irc_realname);

	irc_source_attach();
}

static void irc_run(const gchar *command)
{
	if (g_ascii_strncasecmp(command, "announce", 8) == 0) {
		if (prefs.announce)
			prefs.announce = FALSE;
		else
			prefs.announce = TRUE;

		irc_say("New song announcement %sabled",
				(prefs.announce ? "en" : "dis"));
	} else if (g_ascii_strncasecmp(command, "next", 4) == 0) {
		mpd_next();
	} else if (g_ascii_strncasecmp(command, "np", 2) == 0) {
		mpd_announce_song();
	} else if (g_ascii_strncasecmp(command, "pause", 5) == 0) {
		/* TODO */
	} else if (g_ascii_strncasecmp(command, "play", 4) == 0) {
		/* TODO */
	} else if (g_ascii_strncasecmp(command, "prev", 4) == 0) {
		/* TODO */
	} else if (g_ascii_strncasecmp(command, "random", 6) == 0) {
		/* TODO */
	} else if (g_ascii_strncasecmp(command, "repeat", 6) == 0) {
		/* TODO */
	} else if (g_ascii_strncasecmp(command, "status", 6) == 0) {
		mpd_say_status();
	} else if (g_ascii_strncasecmp(command, "stop", 4) == 0) {
		/* TODO */
	} else if (g_ascii_strncasecmp(command, "version", 7) == 0) {
		irc_say("This is " PACKAGE_STRING);
	}
}

void irc_say(const gchar *fmt, ...)
{
	va_list ap;
	gchar *msg;

	va_start(ap, fmt);
	msg = g_strdup_vprintf(fmt, ap);
	va_end(ap);

	irc_write("PRIVMSG %s :%s", prefs.irc_channel, msg);
	g_free(msg);
}

static void irc_write(const gchar *fmt, ...)
{
	GError *error = NULL;
	gchar *tmp1, *tmp2;
	va_list ap;

	va_start(ap, fmt);
	tmp1 = g_strdup_vprintf(fmt, ap);
	va_end(ap);

	tmp2 = g_strconcat(tmp1, "\n", NULL);
	g_free(tmp1);

	if (g_output_stream_write(ostream, tmp2, strlen(tmp2),
				NULL, &error) < 0)
		g_warning("Failed to write: %s", error->message);

	g_free(tmp2);
}

static void irc_source_attach(void)
{
	GSocket *socket = g_socket_connection_get_socket(connection);
	GSource *source = g_socket_create_source(socket, G_IO_IN, NULL);
	g_source_set_callback(source, (GSourceFunc) irc_callback, NULL, NULL);
	g_source_attach(source, NULL);
}

static gboolean irc_callback(G_GNUC_UNUSED GSocket *socket,
		G_GNUC_UNUSED GIOCondition condition,
		G_GNUC_UNUSED gpointer user_data)
{
	GError *error = NULL;
	gchar *buf, **lines;

	buf = g_malloc0(IRC_READ_BUF);
	if (g_input_stream_read(istream, buf, IRC_READ_BUF, NULL, &error) < 0) {
		g_free(buf);
		g_warning("Failed to read from IRC: %s", error->message);
		irc_schedule_reconnect();
		return FALSE;
	}
	lines = g_strsplit(buf, "\r\n", 0);
	g_free(buf);
	for (guint i = 0; lines[i] != NULL; i++)
		irc_parse(lines[i]);
	g_strfreev(lines);

	return TRUE;
}

static void irc_parse(const gchar *buffer)
{
	/* TODO:
	 * ERROR: Closing Link
	 * die <die password>
	 */
	gchar *tmp;

	if (!connected) {
		tmp = g_strdup_printf(" 001 %s :", prefs.irc_nick);
		if (strstr(buffer, tmp)) {
			irc_write("JOIN %s", prefs.irc_channel);
			connected = TRUE;
		}
		g_free(tmp);
	}

	if (strstr(buffer, "PING :")) {
		g_debug("Received PING");
		irc_write("PONG :%s", strstr(buffer, "PING :") + 6);
	}

	tmp = g_strdup_printf("PRIVMSG %s :!", prefs.irc_channel);
	if (strstr(buffer, tmp))
		irc_run(strstr(buffer, tmp) + strlen(tmp));
	g_free(tmp);
}

void irc_cleanup(void)
{
	if (connection)
		g_object_unref(connection);
}

static void irc_schedule_reconnect(void)
{
	reconnect_source = g_timeout_add_seconds(30, irc_connect, NULL);
}
