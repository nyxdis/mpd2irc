/*
 * mpd2irc - MPD->IRC gateway
 *
 * Copyright 2008-2011 Christoph Mende
 * All rights reserved. Released under the 2-clause BSD license.
 */


#ifndef HAVE_MPD_H
#define HAVE_MPD_H

gboolean mpd_connect(void);
void mpd_schedule_reconnect(void);

#endif /* HAVE_MPD_H */
