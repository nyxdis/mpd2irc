/*
 * mpd2irc - MPD->IRC gateway
 *
 * Copyright 2008 Christoph Mende
 * All rights reserved. Released under the 2-clause BSD license.
 */


#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

int server_connect_unix(const char *path);
int server_connect_tcp(const char *host, int port);
int server_connect(const char *host, int port);
int irc_connect(void);

struct preferences {
	char *irc_server;
	char *irc_nick;
	char *irc_realname;
	char *irc_username;
	char *irc_channel;
	int irc_port;

	char *irc_authserv;
	char *irc_authcmd;
	char *irc_authnick;
	char *irc_authpass;

	char *mpd_server;
	char *mpd_password;
	int mpd_port;
} prefs;

struct song_info {
	char *artist;
	char *title;
	char *album;
	char *file;
} current_song;

int main(int argc, char *argv[])
{
	int irc_sockfd, mpd_sockfd, sr;
	unsigned int major, minor;
	unsigned short write_irc = 0, write_mpd = 0;
	char wbuf[256], rbuf[1024], *saveptr, *line;
	struct timeval waitd;
	fd_set read_flags, write_flags;

	/* set default values */
	prefs.irc_server = strdup("irc.dronf.org");
	prefs.irc_nick = strdup(PACKAGE_NAME);
	prefs.irc_realname = strdup(PACKAGE_STRING);
	prefs.irc_username = strdup(PACKAGE);
	prefs.irc_channel = strdup("#penismeister");
	prefs.irc_port = 6667;

	prefs.irc_authserv = NULL;
	prefs.irc_authcmd = strdup("");
	prefs.irc_authnick = strdup("");
	prefs.irc_authpass = strdup("");

	prefs.mpd_server = strdup("localhost");
	prefs.mpd_password = NULL;
	prefs.mpd_port = 6600;

	mpd_sockfd = server_connect(prefs.mpd_server, prefs.mpd_port);
	if(mpd_sockfd < 0)
	{
		perror("MPD");
		exit(EXIT_FAILURE);
	}
	irc_sockfd = server_connect(prefs.irc_server, prefs.irc_port);
	if(irc_sockfd < 0)
	{
		perror("IRC");
		exit(EXIT_FAILURE);
	}

	write_irc = 1;
	sprintf(wbuf,"NICK %s\n",prefs.irc_nick);
	write(irc_sockfd,wbuf,strlen(wbuf));
	sprintf(wbuf,"USER %s 0 * :%s\n",prefs.irc_username,prefs.irc_realname);
	write(irc_sockfd,wbuf,strlen(wbuf));

	while(1)
	{
		waitd.tv_sec = 1;
		waitd.tv_usec = 0;
		FD_ZERO(&read_flags);
		FD_ZERO(&write_flags);
		FD_SET(mpd_sockfd,&read_flags);
		FD_SET(irc_sockfd,&read_flags);
		if(write_irc > 0) FD_SET(irc_sockfd,&write_flags);
		if(write_mpd > 0) FD_SET(mpd_sockfd,&write_flags);
		if(irc_sockfd > mpd_sockfd)
			sr = irc_sockfd;
		else
			sr = mpd_sockfd;

		if(select(sr+1,&read_flags,&write_flags,NULL,&waitd) < 0)
			continue;

		if(FD_ISSET(irc_sockfd,&write_flags))
		{
			FD_CLR(irc_sockfd,&write_flags);
			write_irc = 0;
		}

		if(FD_ISSET(mpd_sockfd,&write_flags))
		{
			FD_CLR(mpd_sockfd,&write_flags);
			write_mpd = 0;
		}

		if(FD_ISSET(mpd_sockfd,&read_flags))
		{
			FD_CLR(mpd_sockfd,&read_flags);
			sr = read(mpd_sockfd,rbuf,sizeof(rbuf));
			rbuf[sr] = '\0';
			if(sr > 0)
			{
				if(strncmp(rbuf,"OK MPD",6) == 0)
				{
					sscanf(rbuf,"%*s %*s %d.%d.",&major,&minor);
					if(major == 0 && minor < 14)
					{
						fprintf(stderr,"Your MPD is too old, you need at least MPD 0.14\n");
						exit(EXIT_FAILURE);
					}
					write(mpd_sockfd,"idle player\n",12);
					write_mpd = 1;
				}
				else if(strncmp(rbuf,"changed: player",15) == 0)
				{
					write(mpd_sockfd,"currentsong\n",12);
					write(mpd_sockfd,"idle player\n",12);
					write_mpd = 1;
				}
				else if(strncmp(rbuf,"file: ",6) == 0)
				{
					line = strtok_r(rbuf,"\n",&saveptr);			
					do
					{
						if(strncmp(line,"file: ",6) == 0)
						{
							if(strncmp(current_song.file,strdup(&line[6]),strlen(current_song.file)) == 0)
								printf("file unchanged\n");
							current_song.file = strdup(&line[6]);
						}
						if(strncmp(line,"Artist: ",8) == 0)
							current_song.artist = strdup(&line[8]);
						if(strncmp(line,"Title: ",7) == 0)
							current_song.title = strdup(&line[7]);
						if(strncmp(line,"Album: ",7) == 0)
							current_song.album = strdup(&line[7]);
					} while((line = strtok_r(NULL,"\n",&saveptr)) != NULL);
					sprintf(wbuf,"PRIVMSG %s :New song: %s - %s (From %s)\n",
						prefs.irc_channel,current_song.artist,current_song.title,
						current_song.album);
					write(irc_sockfd,wbuf,strlen(wbuf));
					write_irc = 1;
				}
			}
		}

		if(FD_ISSET(irc_sockfd,&read_flags))
		{
			FD_CLR(irc_sockfd,&read_flags);
			sr = read(irc_sockfd,rbuf,sizeof(rbuf));
			rbuf[sr] = '\0';
			if(sr > 0)
			{
				if(strstr(rbuf,"ERROR :Closing Link"))
				{
					fprintf(stderr,"Disconnected from IRC\n");
					exit(EXIT_FAILURE);
				}
				if(strncmp(rbuf,"PING :",6) == 0)
				{
					sprintf(wbuf,"PO%s",&rbuf[2]);
					write(irc_sockfd,wbuf,strlen(wbuf));
					write_irc = 1;
				}
			}
		}
	}

	close(irc_sockfd);
	close(mpd_sockfd);

	exit(EXIT_SUCCESS);
}

int server_connect_unix(const char *path)
{
	int sockfd;
	struct sockaddr_un addr;
	unsigned int len;

	sockfd = socket(AF_UNIX,SOCK_STREAM,0);
	if(sockfd < 0)
		return -1;

	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path,path,strlen(path) + 1);
	len = strlen(addr.sun_path) + sizeof(addr.sun_family);
	if(connect(sockfd,(struct sockaddr *)&addr,len) < 0)
		return -1;
	
	return sockfd;
}

int server_connect_tcp(const char *host, int port)
{
	int sockfd;
	struct sockaddr_in addr;
	struct hostent *he;
	unsigned int len;

	sockfd = socket(AF_INET,SOCK_STREAM,0);
	if(sockfd < 0)
		return -1;
	
	he = gethostbyname(host);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	memcpy((char *)&addr.sin_addr.s_addr,(char *)he->h_addr,he->h_length);
	len = sizeof(addr);
	if(connect(sockfd,(struct sockaddr *)&addr,len) < 0)
		return -1;

	return sockfd;
}

int server_connect(const char *host, int port)
{
	int sockfd;

	if(strncmp(host,"/",1) == 0)
		sockfd = server_connect_unix(host);
	else
		sockfd = server_connect_tcp(host, port);

	if(sockfd < 0)
		return -1;

	if(fcntl(sockfd,F_SETFL,fcntl(sockfd,F_GETFL,0) | O_NONBLOCK) < 0)
		return -1;

	return sockfd;
}
