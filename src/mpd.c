/*
 * mpd2irc - MPD->IRC gateway
 *
 * Copyright 2008-2011 Christoph Mende
 * All rights reserved. Released under the 2-clause BSD license.
 */


#include <glib.h>
#include <mpd/client.h>

#include "mpd.h"
#include "preferences.h"

static gboolean mpd_parse(GIOChannel *channel, GIOCondition condition,
		gboolean user_data);
static void mpd_disconnect(void);
static gboolean mpd_reconnect(G_GNUC_UNUSED gpointer data);

static struct {
	struct mpd_connection *conn;
	struct mpd_status *status;
	struct mpd_song *song;
	guint idle_source;
	guint reconnect_source;
} mpd;

gboolean mpd_connect(void)
{
	mpd.conn = mpd_connection_new(prefs.mpd_server, prefs.mpd_port, 10000);

	if (mpd_connection_get_error(mpd.conn) != MPD_ERROR_SUCCESS) {
		g_warning("Failed to connect to MPD: %s",
				mpd_connection_get_error_message(mpd.conn));
		return FALSE;
	} else if (mpd_connection_cmp_server_version(mpd.conn, 0, 14, 0) < 0) {
		g_critical("MPD too old, please upgrade to 0.14 or newer");
		return FALSE;
	} else {
		GIOChannel *channel;

		mpd_command_list_begin(mpd.conn, TRUE);
		mpd_send_status(mpd.conn);
		mpd_send_current_song(mpd.conn);
		mpd_command_list_end(mpd.conn);

		mpd.status = mpd_recv_status(mpd.conn);
		mpd_response_next(mpd.conn);
		mpd.song = mpd_recv_song(mpd.conn);
		mpd_response_finish(mpd.conn);

		g_message("Connected to MPD");

		mpd_send_idle_mask(mpd.conn, MPD_IDLE_PLAYER);

		channel = g_io_channel_unix_new(
				mpd_connection_get_fd(mpd.conn));
		mpd.idle_source = g_io_add_watch(channel, G_IO_IN,
				(GIOFunc) mpd_parse, NULL);
		g_io_channel_unref(channel);

		return TRUE;
	}
}

static gboolean mpd_parse(G_GNUC_UNUSED GIOChannel *channel,
		G_GNUC_UNUSED GIOCondition condition,
		G_GNUC_UNUSED gboolean user_data)
{
	mpd_recv_idle(mpd.conn, FALSE);

	if (!mpd_response_finish(mpd.conn)) {
		g_warning("Failed to read from MPD: %s",
				mpd_connection_get_error_message(mpd.conn));
		mpd_disconnect();
		mpd_schedule_reconnect();
		return FALSE;
	}

	/* TODO parse response */

	mpd_send_idle_mask(mpd.conn, MPD_IDLE_PLAYER);
	return TRUE;
}

static void mpd_disconnect(void)
{
	if (mpd.conn)
		mpd_connection_free(mpd.conn);
	mpd.conn = NULL;
}

void mpd_schedule_reconnect(void)
{
	mpd.reconnect_source = g_timeout_add_seconds(30, mpd_reconnect, NULL);
}

static gboolean mpd_reconnect(G_GNUC_UNUSED gpointer data)
{
	if (!mpd_connect()) {
		mpd_disconnect();
		return TRUE; // try again
	}

	mpd.reconnect_source = 0;
	return FALSE; // remove event
}
