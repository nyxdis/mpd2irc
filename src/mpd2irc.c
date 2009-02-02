/*
 * mpd2irc - MPD->IRC gateway
 *
 * Copyright 2008 Christoph Mende
 * All rights reserved. Released under the 2-clause BSD license.
 */


#include <confuse.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
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

int cleanup(void);
int irc_match(const char *line, const char *msg);
int irc_say(const char *msg);
int m2i_error(const char *msg);
int mpd_write(const char *msg);
int m2i_parser(const char *origin, char *msg);
int server_connect(const char *host, int port);
int server_connect_tcp(const char *host, int port);
int server_connect_unix(const char *path);
void sighandler(int sig);

/* preferences parsed from the config file */
static struct preferences {
	char *irc_server;
	char *irc_password;
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

	char *die_password;
} prefs;

/* information about the song currently played */
static struct song_info {
	char *artist;
	char *title;
	char *album;
	char *file;
} current_song;

/* current mpd status */
static struct mpd_status {
	bool repeat;
	bool random;
	short xfade;
	char *state;
} mpd_status;

static int irc_sockfd, mpd_sockfd;
static bool announce = true;
static cfg_t *cfg;

int main(void)
{
	cfg_t *cfg_mpd, *cfg_irc_conn, *cfg_irc_auth;
	char buf[1024];
	fd_set read_flags;
	int sockfd;
	struct sigaction sa;
	struct timeval waitd;

	/* available options */
	cfg_opt_t mpd_opts[] = {
		CFG_STR("server","localhost",CFGF_NONE),
		CFG_STR("password","",CFGF_NONE),
		CFG_INT("port",6600,CFGF_NONE),
		CFG_END()
	};

	cfg_opt_t irc_conn_opts[] = {
		CFG_STR("server","",CFGF_NONE),
		CFG_STR("password","",CFGF_NONE),
		CFG_STR("nick",PACKAGE,CFGF_NONE),
		CFG_STR("realname",PACKAGE_STRING,CFGF_NONE),
		CFG_STR("username",PACKAGE_NAME,CFGF_NONE),
		CFG_STR("channel","",CFGF_NONE),
		CFG_INT("port",6667,CFGF_NONE),
		CFG_END()
	};

	cfg_opt_t irc_auth_opts[] = {
		CFG_STR("authserv","nickserv",CFGF_NONE),
		CFG_STR("cmd","identify",CFGF_NONE),
		CFG_STR("nick","",CFGF_NONE),
		CFG_STR("password","",CFGF_NONE),
		CFG_END()
	};

	cfg_opt_t opts[] = {
		CFG_SEC("mpd",mpd_opts,CFGF_NONE),
		CFG_SEC("irc_connection",irc_conn_opts,CFGF_NONE),
		CFG_SEC("irc_auth",irc_auth_opts,CFGF_NONE),
		CFG_STR("die_password","secret",CFGF_NONE),
		CFG_END()
	};

	cfg = cfg_init(opts,CFGF_NONE);
	sprintf(buf,"%s/.mpd2irc.conf",getenv("HOME"));
	if(cfg_parse(cfg,buf) == CFG_FILE_ERROR) {
		if(cfg_parse(cfg,"/etc/mpd2irc.conf") == CFG_FILE_ERROR)
			m2i_error("Unable to open configuration file");
	}
	cfg_mpd = cfg_getsec(cfg,"mpd");
	cfg_irc_conn = cfg_getsec(cfg,"irc_connection");
	cfg_irc_auth = cfg_getsec(cfg,"irc_auth");

	/* Signal handler */
	sa.sa_handler = sighandler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGINT,&sa,NULL);
	sigaction(SIGTERM,&sa,NULL);
	sigaction(SIGUSR1,&sa,NULL);
	sigaction(SIGUSR2,&sa,NULL);
	sigaction(SIGHUP,&sa,NULL);

	/* set settings parsed from config file */
	prefs.irc_server = strdup(cfg_getstr(cfg_irc_conn,"server"));
	prefs.irc_password = strdup(cfg_getstr(cfg_irc_conn,"password"));
	prefs.irc_nick = strdup(cfg_getstr(cfg_irc_conn,"nick"));
	prefs.irc_realname = strdup(cfg_getstr(cfg_irc_conn,"realname"));
	prefs.irc_username = strdup(cfg_getstr(cfg_irc_conn,"username"));
	prefs.irc_channel = strdup(cfg_getstr(cfg_irc_conn,"channel"));
	prefs.irc_port = cfg_getint(cfg_irc_conn,"port");

	prefs.irc_authserv = strdup(cfg_getstr(cfg_irc_auth,"authserv"));
	prefs.irc_authcmd = strdup(cfg_getstr(cfg_irc_auth,"cmd"));
	prefs.irc_authnick = strdup(cfg_getstr(cfg_irc_auth,"nick"));
	prefs.irc_authpass = strdup(cfg_getstr(cfg_irc_auth,"password"));

	prefs.mpd_server = strdup(cfg_getstr(cfg_mpd,"server"));
	prefs.mpd_password = strdup(cfg_getstr(cfg_mpd,"password"));
	prefs.mpd_port = cfg_getint(cfg_mpd,"port");

	prefs.die_password = strdup(cfg_getstr(cfg,"die_password"));

	cfg_free(cfg);

	current_song.file = strdup("");

	/* check required settings */
	if(strlen(prefs.irc_server) == 0)
		m2i_error("IRC server undefined");
	if(strlen(prefs.irc_channel) == 0)
		m2i_error("IRC channel undefined");
	if(strncmp(prefs.die_password,"secret",6) == 0)
		puts("Warning: Weak die password");

	/* connect to IRC and MPD */
	mpd_sockfd = server_connect(prefs.mpd_server, prefs.mpd_port);
	if(mpd_sockfd < 0)
	{
		if(errno > 0) perror("MPD");
		cleanup();
		exit(EXIT_FAILURE);
	}
	free(prefs.mpd_server);
	irc_sockfd = server_connect(prefs.irc_server, prefs.irc_port);
	if(irc_sockfd < 0)
	{
		if(errno > 0) perror("IRC");
		exit(EXIT_FAILURE);
	}
	free(prefs.irc_server);

	/* IRC protocol introduction */
	if(strlen(prefs.irc_password) > 0)
	{
		sprintf(buf,"PASS %s\n",prefs.irc_password);
		if(write(irc_sockfd,buf,strlen(buf)) < 0) perror("IRC: write");
	}
	free(prefs.irc_password);
	sprintf(buf,"NICK %s\n",prefs.irc_nick);
	if(write(irc_sockfd,buf,strlen(buf)) < 0) perror("IRC: write");
	sprintf(buf,"USER %s 0 * :%s\n",prefs.irc_username,prefs.irc_realname);
	if(write(irc_sockfd,buf,strlen(buf)) < 0) perror("IRC: write");
	free(prefs.irc_username); free(prefs.irc_realname);

	for(;;)
	{
		waitd.tv_sec = 1;
		waitd.tv_usec = 0;
		FD_ZERO(&read_flags);
		FD_SET(mpd_sockfd,&read_flags);
		FD_SET(irc_sockfd,&read_flags);
		if(irc_sockfd > mpd_sockfd)
			sockfd = irc_sockfd;
		else
			sockfd = mpd_sockfd;

		if(select(sockfd+1,&read_flags,NULL,NULL,&waitd) < 0)
			continue;

		if(FD_ISSET(mpd_sockfd,&read_flags))
		{
			FD_CLR(mpd_sockfd,&read_flags);
			memset(buf,0,sizeof(buf));
			if(read(mpd_sockfd,buf,sizeof(buf)-1) > 0)
				m2i_parser("mpd",buf);
		}

		if(FD_ISSET(irc_sockfd,&read_flags))
		{
			FD_CLR(irc_sockfd,&read_flags);
			memset(buf,0,sizeof(buf));
			if(read(irc_sockfd,buf,sizeof(buf)-1) > 0)
				m2i_parser("irc",buf);
		}
	}

	cleanup();
}

/* Connecting to a UNIX domain socket */
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

	if(fcntl(sockfd,F_SETFL,fcntl(sockfd,F_GETFL,0) | O_NONBLOCK) < 0)
		return -1;

	return sockfd;
}

/* Connecting to a TCP socket */
int server_connect_tcp(const char *host, int port)
{
	char service[5];
	fd_set write_flags;
	int sockfd = 0, valopt, ret;
	socklen_t lon;
	struct addrinfo hints, *result, *rp;
	struct timeval timeout;

	memset(&hints,0,sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	sprintf(service,"%d",port);

	if((ret = getaddrinfo(host,service,&hints,&result)) != 0) {
		fprintf(stderr,"Failed to get address information: %s\n",gai_strerror(ret));
		return -1;
	}

	for(rp = result;rp != NULL;rp = rp->ai_next) {
		if((sockfd = socket(rp->ai_family,rp->ai_socktype,rp->ai_protocol)) >= 0)
			break;
		close(sockfd);
	}

	if(fcntl(sockfd,F_SETFL,fcntl(sockfd,F_GETFL,0) | O_NONBLOCK) < 0)
		return -1;

	if(connect(sockfd,rp->ai_addr,rp->ai_addrlen) < 0) {
		if(errno == EINPROGRESS) {
			timeout.tv_sec = 15;
			timeout.tv_usec = 0;

			FD_ZERO(&write_flags);
			FD_SET(sockfd,&write_flags);
			if(select(sockfd+1,NULL,&write_flags,NULL,&timeout) > 0) {
				lon = sizeof(int);
				getsockopt(sockfd,SOL_SOCKET,SO_ERROR,
					(void*)(&valopt),&lon);
				if(valopt) {
					errno = valopt;
					return -1;
				}
			}
			else {
				errno = ETIMEDOUT;
				return -1;
			}
		}
		else
			return -1;
	}

	freeaddrinfo(result);
	errno = 0;
	return sockfd;
}

/* wrapper around server_connect_tcp and server_connect_unix */
int server_connect(const char *host, int port)
{
	int sockfd;

	if(strncmp(host,"/",1) == 0)
		sockfd = server_connect_unix(host);
	else
		sockfd = server_connect_tcp(host, port);

	if(sockfd < 0)
		return -1;

	return sockfd;
}

/* parser that receives all the mpd/irc data */
int m2i_parser(const char *origin, char *msg)
{
	unsigned int major, minor;
	bool mpd_new_song = false;
	char *line, *saveptr, buf[256], tmp[256];

	line = strtok_r(msg,"\n",&saveptr);
	do
	{
		/* mpd events */
		if(strncmp(origin,"mpd",3) == 0)
		{
			/* mpd error */
			if(strncmp(line,"ACK ",4) == 0)
			{
				sprintf(buf,"MPD error: %s",&line[4]);
				m2i_error(buf);
			}

			/* connected to mpd */
			if(strncmp(line,"OK MPD",6) == 0)
			{
				sscanf(line,"%*s %*s %u.%u.",&major,&minor);
				if(major == 0 && minor < 14)
					m2i_error("Your MPD is too old, "
						"you need at least MPD 0.14.");

				/* authentication */
				if(strlen(prefs.mpd_password) > 0)
				{
					sprintf(buf,"password %s\n",
					prefs.mpd_password);
					if(write(mpd_sockfd,buf,strlen(buf)) < 0) perror("MPD: write");
				}
				free(prefs.mpd_password);
				mpd_write("status\ncurrentsong");
			}

			/* idle events */
			else if(strncmp(line,"changed: player",15) == 0)
				mpd_write("status\ncurrentsong");
			else if(strncmp(line,"changed: options",16) == 0)
				mpd_write("status\n");

			/* song change */
			else if(strncmp(line,"file: ",6) == 0)
			{
				if(strncmp(current_song.file,&line[6],strlen(&line[6])))
					mpd_new_song = true;
			}

			if(mpd_new_song == true)
			{
				if(strncmp(line,"file: ",6) == 0) {
					free(current_song.file);
					current_song.file = strdup(&line[6]);
				} else if(strncmp(line,"Artist: ",8) == 0) {
					free(current_song.artist);
					current_song.artist = strdup(&line[8]);
				} else if(strncmp(line,"Title: ",7) == 0) {
					free(current_song.title);
					current_song.title = strdup(&line[7]);
				} else if(strncmp(line,"Album: ",7) == 0) {
					free(current_song.album);
					current_song.album = strdup(&line[7]);
				}
			}

			/* status change */
			if(strncmp(line,"repeat: ",8) == 0) {
				if(atoi(&line[8]) == 0) mpd_status.repeat = false;
				else mpd_status.repeat = true;
			}
			if(strncmp(line,"random: ",8) == 0) {
				if(atoi(&line[8]) == 0) mpd_status.random = false;
				else mpd_status.random = true;
			}
			if(strncmp(line,"xfade: ",7) == 0)
				sscanf(line,"%*s %hd",&mpd_status.xfade);
			if(strncmp(line,"state: ",7) == 0) {
				free(mpd_status.state);
				mpd_status.state = strdup(&line[7]);
			}
		}

		/* IRC events */
		else if(strncmp(origin,"irc",3) == 0)
		{
			/* IRC error */
			if(strncmp(line,"ERROR :Closing Link",19) == 0)
				m2i_error("Disconnected from IRC");

			else if(strncmp(line,"PING :",6) == 0)
			{
				sprintf(buf,"PO%s",&buf[2]);
				if(write(irc_sockfd,buf,strlen(buf)) < 0) perror("IRC: write");
			}

			else if(irc_match(line,"next") == 0)
				mpd_write("next");

			else if(irc_match(line,"prev") == 0)
				mpd_write("previous");

			else if(irc_match(line,"pause") == 0)
			{
				if(strncmp(mpd_status.state,"play",4) == 0)
					mpd_write("pause 1");
				else
					mpd_write("play");
			}

			else if(irc_match(line,"status") == 0)
			{
				sprintf(buf,"Repeat: %s, Random: %s, "
					"Crossfade: %d sec, State: %s",
					(mpd_status.repeat == 1 ? "on" : "off"),
					(mpd_status.random == 1 ? "on" : "off"),
					mpd_status.xfade,mpd_status.state);
				irc_say(buf);
			}

			else if(irc_match(line,"np") == 0)
			{
				sprintf(buf,"Now Playing: %s - %s (%s)",
					current_song.artist,
					current_song.title,current_song.album);
				irc_say(buf);
			}

			else if(irc_match(line,"version") == 0)
			{
				sprintf(buf,"This is %s",PACKAGE_STRING);
				irc_say(buf);
			}

			else if(irc_match(line,"announce") == 0)
			{
				if(announce == false) announce = true;
				else announce = false;
				sprintf(buf,"Announcements %sabled.",
					(announce == false ? "dis" : "en"));
				irc_say(buf);
			}

			else if(irc_match(line,"play") == 0)
				mpd_write("play");

			else if(irc_match(line,"stop") == 0)
				mpd_write("stop");

			else if(irc_match(line,"random") == 0)
			{
				if(mpd_status.random == 0)
				{
					mpd_write("random 1");
					irc_say("Enabled random");
				}
				else
				{
					mpd_write("random 0");
					irc_say("Disabled random");
				}
			}

			else if(irc_match(line,"repeat") == 0)
			{
				if(mpd_status.repeat == 0)
				{
					mpd_write("repeat 1");
					irc_say("Enabled repeat");
				}
				else
				{
					mpd_write("repeat 0");
					irc_say("Disabled repeat");
				}
			}

			/* received die request */
			sprintf(tmp,"PRIVMSG %s :die %s\r",prefs.irc_nick,
				prefs.die_password);
			if(strstr(line,tmp))
			{
				free(prefs.die_password);
				sprintf(buf,"QUIT :Exiting\n");
				if(write(irc_sockfd,buf,strlen(buf)) < 0) perror("IRC: write");
				if(write(mpd_sockfd,"close\n",6) < 0) perror("MPD: write");
				cleanup();
			}

			/* connected to IRC */
			sprintf(tmp," 001 %s :Welcome to the ",prefs.irc_nick);
			if(strstr(line,tmp))
			{
				if(strlen(prefs.irc_authserv) > 0)
				{
					sprintf(buf,"PRIVMSG %s :%s %s %s\n",
						prefs.irc_authserv,
						prefs.irc_authcmd,
						prefs.irc_authnick,
						prefs.irc_authpass);
					if(write(irc_sockfd,buf,strlen(buf)) < 0) perror("IRC: write");
				}
				free(prefs.irc_authserv); free(prefs.irc_authcmd);
				free(prefs.irc_authnick); free(prefs.irc_authpass);
				sprintf(buf,"JOIN %s\n",prefs.irc_channel);
				if(write(irc_sockfd,buf,strlen(buf)) < 0) perror("IRC: write");
				continue;
			}
		}
		else
		{
			sprintf(buf,"Error while parsing: "
				"Unknown origin '%s'",origin);
			m2i_error(buf);
		}
	} while((line = strtok_r(NULL,"\n",&saveptr)) != NULL);

	if(mpd_new_song == true && announce == true)
	{
		sprintf(buf,"New song: %s - %s (%s)",current_song.artist,
			current_song.title,current_song.album);
		irc_say(buf);
	}
	mpd_new_song = false;
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
		if(write(irc_sockfd,buf,strlen(buf)) < 0) perror("IRC: write");
		cleanup();
	}
}

int mpd_write(const char *msg)
{
	char buf[256];
	sprintf(buf,"noidle\n%s\nidle options player\n",msg);
	if(write(mpd_sockfd,buf,strlen(buf)) < 0) perror("MPD: write");
	return 0;
}

int irc_say(const char *msg)
{
	char buf[256];
	sprintf(buf,"PRIVMSG %s :%s\n",prefs.irc_channel,msg);
	if(write(irc_sockfd,buf,strlen(buf)) < 0) perror("IRC: write");
	return 0;
}

int cleanup(void)
{
	free(current_song.file);
	free(current_song.artist);
	free(current_song.title);
	free(current_song.album);
	free(mpd_status.state);
	free(prefs.irc_nick);
	free(prefs.irc_channel);
	close(irc_sockfd);
	close(mpd_sockfd);
	exit(EXIT_SUCCESS);
}

int irc_match(const char *line, const char *msg)
{
	char buf[256];

	sprintf(buf,"PRIVMSG %s :!%s\r",prefs.irc_channel,msg);
	if(strstr(line,buf))
		return 0;
	return 1;
}

int m2i_error(const char *msg)
{
	fprintf(stderr,"%s\n",msg);
	exit(EXIT_FAILURE);
}
