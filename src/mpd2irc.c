/*
 * mpd2irc - MPD->IRC gateway
 *
 * Copyright 2008 Christoph Mende
 * All rights reserved. Released under the 2-clause BSD license.
 */


#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
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
int parser(const char *origin, char *msg);
void sighandler(int sig);

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

struct mpd_status {
	short repeat;
	short random;
	short xfade;
	enum { PLAY, STOP, PAUSE } state;
} mpd_status;

int irc_sockfd, mpd_sockfd;
unsigned short write_irc = 0, write_mpd = 0;

int main(void)
{
	char buf[1024];
	fd_set read_flags, write_flags;
	int sr;
	struct sigaction sa;
	struct timeval waitd;

	/* Signal handler */
	sa.sa_handler = sighandler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGINT,&sa,NULL);
	sigaction(SIGTERM,&sa,NULL);
	sigaction(SIGUSR1,&sa,NULL);
	sigaction(SIGUSR2,&sa,NULL);
	sigaction(SIGHUP,&sa,NULL);

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

	current_song.file = strdup("");

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
	sprintf(buf,"NICK %s\n",prefs.irc_nick);
	write(irc_sockfd,buf,strlen(buf));
	sprintf(buf,"USER %s 0 * :%s\n",prefs.irc_username,prefs.irc_realname);
	write(irc_sockfd,buf,strlen(buf));

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
			sr = read(mpd_sockfd,buf,sizeof(buf));
			buf[sr] = '\0';
			if(sr > 0)
			{
				parser("mpd",buf);
			}
		}

		if(FD_ISSET(irc_sockfd,&read_flags))
		{
			FD_CLR(irc_sockfd,&read_flags);
			sr = read(irc_sockfd,buf,sizeof(buf));
			buf[sr] = '\0';
			if(sr > 0)
			{
				parser("irc",buf);
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

int parser(const char *origin, char *msg)
{
	unsigned int major, minor;
	unsigned short mpd_new_song = 0;
	char *line, *saveptr, buf[256], tmp[256];

	line = strtok_r(msg,"\n",&saveptr);
	do
	{
		/* mpd events */
		if(strncmp(origin,"mpd",3) == 0)
		{
			if(strncmp(line,"OK MPD",6) == 0)
			{
				sscanf(line,"%*s %*s %d.%d.",&major,&minor);
				if(major == 0 && minor < 14)
				{
					fprintf(stderr,"Your MPD is too old, you need at least MPD 0.14\n");
					exit(EXIT_FAILURE);
				}
				write(mpd_sockfd,"status\n",7);
				write(mpd_sockfd,"currentsong\n",12);
				write(mpd_sockfd,"idle options player\n",20);
				write_mpd = 1;
			}
			else if(strncmp(line,"changed: player",15) == 0)
			{
				write(mpd_sockfd,"currentsong\n",12);
				write(mpd_sockfd,"idle options player\n",20);
				write_mpd = 1;
			}
			else if(strncmp(line,"file: ",6) == 0)
			{
				if(strncmp(current_song.file,&line[6],strlen(&line[6])))
					mpd_new_song = 1;
			}

			if(mpd_new_song == 1)
			{
				if(strncmp(line,"file: ",6) == 0)
					current_song.file = strdup(&line[6]);
				else if(strncmp(line,"Artist: ",8) == 0)
					current_song.artist = strdup(&line[8]);
				else if(strncmp(line,"Title: ",7) == 0)
					current_song.title = strdup(&line[7]);
				else if(strncmp(line,"Album: ",7) == 0)
					current_song.album = strdup(&line[7]);
			}
		}

		/* IRC events */
		else if(strncmp(origin,"irc",3) == 0)
		{
			if(strncmp(line,"ERROR :Closing Link",19) == 0)
			{
				fprintf(stderr,"Disconnected from IRC\n");
				exit(EXIT_FAILURE);
			}

			if(strncmp(line,"PING :",6) == 0)
			{
				sprintf(buf,"PO%s",&buf[2]);
				write(irc_sockfd,buf,strlen(buf));
				write_irc = 1;
				continue;
			}

			sprintf(tmp," 001 %s :Welcome to the ",prefs.irc_nick);
			if(strstr(line,tmp))
			{
				sprintf(buf,"JOIN %s\n",prefs.irc_channel);
				write(irc_sockfd,buf,strlen(buf));
				write_irc = 1;
				continue;
			}

			sprintf(tmp,"PRIVMSG %s :!np\r",prefs.irc_channel);
			if(strstr(line,tmp))
			{
				sprintf(buf,"PRIVMSG %s :Now Playing: %s - %s (From %s)\n",
					prefs.irc_channel,current_song.artist,
					current_song.title,current_song.album);
				write(irc_sockfd,buf,strlen(buf));
				write_irc = 1;
				continue;
			}
		}
		else
		{
			fprintf(stderr,"Error while parsing: Unknown origin '%s'\n",origin);
			exit(EXIT_FAILURE);
		}
	} while((line = strtok_r(NULL,"\n",&saveptr)) != NULL);

	if(mpd_new_song == 1)
	{
		sprintf(buf,"PRIVMSG %s :New song: %s - %s (From %s)\n",
			prefs.irc_channel,current_song.artist,current_song.title,
			current_song.album);
		write(irc_sockfd,buf,strlen(buf));
		write_irc = 1;
		mpd_new_song = 0;
	}
	return 0;
}

void sighandler(int sig)
{
	char buf[256];
	if(sig == SIGUSR1 || sig == SIGUSR2 || sig == SIGHUP)
		printf("Caught signal %d, ignored.\n",sig);
	else
	{
		sprintf(buf,"QUIT :Caught signal: %d, exiting.\n",sig);
		write(irc_sockfd,buf,strlen(buf));
		exit(EXIT_FAILURE);
	}
}
