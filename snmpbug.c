/* Main program
 *
 * Copyright (C) 2008-2010  Robert Ernst <robert.ernst@linux-solutions.at>
 * Copyright (C) 2015-2020  Joachim Nilsson <troglobit@gmail.com>
 *
 * This file may be distributed and/or modified under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation and appearing in the file LICENSE.GPL included in the
 * packaging of this file.
 *
 * This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
 * WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See COPYING for GPL licensing information.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <net/if.h>
#include <arpa/inet.h>

#define SYSLOG_NAMES		/* Expose syslog.h:prioritynames[] */
#include "snmpbug.h"

in_port_t g_udp_port=0;
in_port_t g_tcp_port=0;

static int usage(int rc)
{
	printf("Usage: %s [options]\n"
	       "\n"
	       "  -h, --help             This help text\n"
	       "  -i, --interfaces IFACE Network interfaces to monitor, default: none\n"
	       "  -I, --listen IFACE     Network interface to listen, default: all\n"
	       "  -p, --udp-port PORT    UDP port to bind to, default: 161\n"
	       "  -P, --tcp-port PORT    TCP port to bind to, default is equal to udp port\n"
	       "  -u, --drop-privs USER  Drop privileges after opening sockets to USER, default: no\n"
	       "  -v, --version          Show program version and exit\n"
	       "\n", g_prognm
#ifdef HAVE_LIBCONFUSE
	       , PACKAGE_NAME
#endif
		);
	printf("Bug report address: %s\n", PACKAGE_BUGREPORT);
#ifdef PACKAGE_URL
	printf("Project homepage: %s\n", PACKAGE_URL);
#endif

	return rc;
}

static void handle_signal(int UNUSED(signo))
{
	g_quit = 1;
}

static void handle_udp_client(void)
{
	const char *req_msg = "Failed UDP request from";
	const char *snd_msg = "Failed UDP response to";
	my_sockaddr_t sockaddr;
	my_socklen_t socklen;
	ssize_t rv;
	char straddr[my_inet_addrstrlen] = { 0 };

	memset(&sockaddr, 0, sizeof(sockaddr));

	/* Read the whole UDP packet from the socket at once */
	socklen = sizeof(sockaddr);
	rv = recvfrom(g_udp_sockfd, g_udp_client.packet, sizeof(g_udp_client.packet),
		      0, (struct sockaddr *)&sockaddr, &socklen);
	if (rv == -1) {
		logit(LOG_WARNING, errno, "Failed receiving UDP request on port %d", g_udp_port);
		return;
	}

	g_udp_client.timestamp = time(NULL);
	g_udp_client.sockfd = g_udp_sockfd;
	g_udp_client.addr = sockaddr.my_sin_addr;
	g_udp_client.port = sockaddr.my_sin_port;
	g_udp_client.size = rv;
	g_udp_client.outgoing = 0;

	/* Call the protocol handler which will prepare the response packet */
	inet_ntop(my_af_inet, &sockaddr.my_sin_addr, straddr, sizeof(straddr));
	if (strncmp(straddr,"::ffff:",7)==0) {	/* ipv4-in-ipv6 representation */
		for(i=0; i < (strlen(straddr) - 7); i++) {
			straddr[i] = straddr[(i+7)];  /* shift the IPv4 addr to the beginning of the string */
		}
		straddr[i]='\0';  /* set the new termination point */
	}
	if (snmp(&g_udp_client) == -1) {
		logit(LOG_WARNING, errno, "%s %s:%d", req_msg, straddr, sockaddr.my_sin_port);
		return;
	}
	if (g_udp_client.size == 0) {
		logit(LOG_WARNING, 0, "%s %s:%d: ignored", req_msg, straddr, sockaddr.my_sin_port);
		return;
	}
	g_udp_client.outgoing = 1;

	/* Send the whole UDP packet to the socket at once */
	rv = sendto(g_udp_sockfd, g_udp_client.packet, g_udp_client.size,
		    MSG_DONTWAIT, (struct sockaddr *)&sockaddr, socklen);
	inet_ntop(my_af_inet, &sockaddr.my_sin_addr, straddr, sizeof(straddr));
	if (rv == -1)
		logit(LOG_WARNING, errno, "%s %s:%d", snd_msg, straddr, sockaddr.my_sin_port);
	else if ((size_t)rv != g_udp_client.size)
		logit(LOG_WARNING, 0, "%s %s:%d: only %zd of %zu bytes sent", snd_msg, straddr, sockaddr.my_sin_port, rv, g_udp_client.size);
}

static void handle_tcp_connect(void)
{
	const char *msg = "Could not accept TCP connection";
	my_sockaddr_t tmp_sockaddr;
	my_sockaddr_t sockaddr;
	my_socklen_t socklen;
	client_t *client;
	char straddr[my_inet_addrstrlen] = "";
	int rv;

	memset(&tmp_sockaddr, 0, sizeof(tmp_sockaddr));
	memset(&sockaddr, 0, sizeof(sockaddr));

	/* Accept the new connection (remember the client's IP address and port) */
	socklen = sizeof(sockaddr);
	rv = accept(g_tcp_sockfd, (struct sockaddr *)&sockaddr, &socklen);
	if (rv == -1) {
		logit(LOG_ERR, errno, "%s", msg);
		return;
	}
	if (rv >= FD_SETSIZE) {
		logit(LOG_ERR, 0, "%s: FD set overflow", msg);
		close(rv);
		return;
	}

	/* Create a new client control structure or overwrite the oldest one */
	if (g_tcp_client_list_length >= MAX_NR_CLIENTS) {
		client = find_oldest_client();
		if (!client) {
			logit(LOG_ERR, 0, "%s: internal error", msg);
			exit(EXIT_SYSCALL);
		}

		tmp_sockaddr.my_sin_addr = client->addr;
		tmp_sockaddr.my_sin_port = client->port;
		inet_ntop(my_af_inet, &tmp_sockaddr.my_sin_addr, straddr, sizeof(straddr));
		if (strncmp(straddr,"::ffff:",7)==0) {	/* ipv4-in-ipv6 representation */
			for(i=0; i < (strlen(straddr) - 7); i++) {
				straddr[i] = straddr[(i+7)];  /* shift the IPv4 addr to the beginning of the string */
			}
			straddr[i]='\0';  /* set the new termination point */
		}
		logit(LOG_WARNING, 0, "Maximum number of %d clients reached, kicking out %s:%d",
		      MAX_NR_CLIENTS, straddr, tmp_sockaddr.my_sin_port);
		close(client->sockfd);
	} else {
		client = allocate(sizeof(client_t));
		if (!client)
			exit(EXIT_SYSCALL);

		g_tcp_client_list[g_tcp_client_list_length++] = client;
	}

	/* Now fill out the client control structure values */
	inet_ntop(my_af_inet, &sockaddr.my_sin_addr, straddr, sizeof(straddr));
	if (strncmp(straddr,"::ffff:",7)==0) {	/* ipv4-in-ipv6 representation */
		for(i=0; i < (strlen(straddr) - 7); i++) {
			straddr[i] = straddr[(i+7)];  /* shift the IPv4 addr to the beginning of the string */
		}
		straddr[i]='\0';  /* set the new termination point */
	}
	logit(LOG_DEBUG, 0, "Connected TCP client %s:%d", straddr, sockaddr.my_sin_port);
	client->timestamp = time(NULL);
	client->sockfd = rv;
	client->addr = sockaddr.my_sin_addr;
	client->port = sockaddr.my_sin_port;
	client->size = 0;
	client->outgoing = 0;
}

static void handle_tcp_client_write(client_t *client)
{
	const char *msg = "Failed TCP response to";
	ssize_t rv;
	char straddr[my_inet_addrstrlen] = "";
	my_sockaddr_t sockaddr;

	/* Send the packet atomically and close socket if that did not work */
	sockaddr.my_sin_addr = client->addr;
	sockaddr.my_sin_port = client->port;
	rv = send(client->sockfd, client->packet, client->size, 0);
	inet_ntop(my_af_inet, &sockaddr.my_sin_addr, straddr, sizeof(straddr));
	if (strncmp(straddr,"::ffff:",7)==0) {	/* ipv4-in-ipv6 representation */
		for(i=0; i < (strlen(straddr) - 7); i++) {
			straddr[i] = straddr[(i+7)];  /* shift the IPv4 addr to the beginning of the string */
		}
		straddr[i]='\0';  /* set the new termination point */
	}
	if (rv == -1) {
		logit(LOG_WARNING, errno, "%s %s:%d", msg, straddr, sockaddr.my_sin_port);
		close(client->sockfd);
		client->sockfd = -1;
		return;
	}
	if ((size_t)rv != client->size) {
		logit(LOG_WARNING, 0, "%s %s:%d: only %zd of %zu bytes written",
		      msg, straddr, sockaddr.my_sin_port, rv, client->size);
		close(client->sockfd);
		client->sockfd = -1;
		return;
	}

	/* Put the client into listening mode again */
	client->size = 0;
	client->outgoing = 0;
}

static void handle_tcp_client_read(client_t *client)
{
	const char *req_msg = "Failed TCP request from";
	int rv;
	char straddr[my_inet_addrstrlen] = "";
	my_sockaddr_t sockaddr;

	/* Read from the socket what arrived and put it into the buffer */
	sockaddr.my_sin_addr = client->addr;
	sockaddr.my_sin_port = client->port;
	rv = read(client->sockfd, client->packet + client->size, sizeof(client->packet) - client->size);
	inet_ntop(my_af_inet, &sockaddr.my_sin_addr, straddr, sizeof(straddr));
	if (strncmp(straddr,"::ffff:",7)==0) {	/* ipv4-in-ipv6 representation */
		for(i=0; i < (strlen(straddr) - 7); i++) {
			straddr[i] = straddr[(i+7)];  /* shift the IPv4 addr to the beginning of the string */
		}
		straddr[i]='\0';  /* set the new termination point */
	}
	if (rv == -1) {
		logit(LOG_WARNING, errno, "%s %s:%d", req_msg, straddr, sockaddr.my_sin_port);
		close(client->sockfd);
		client->sockfd = -1;
		return;
	}
	if (rv == 0) {
		logit(LOG_DEBUG, 0, "TCP client %s:%d disconnected",
		      straddr, sockaddr.my_sin_port);
		close(client->sockfd);
		client->sockfd = -1;
		return;
	}
	client->timestamp = time(NULL);
	client->size += rv;

	/* Check whether the packet was fully received and handle packet if yes */
	rv = snmp_packet_complete(client);
	if (rv == -1) {
		logit(LOG_WARNING, errno, "%s %s:%d", req_msg, straddr, sockaddr.my_sin_port);
		close(client->sockfd);
		client->sockfd = -1;
		return;
	}
	if (rv == 0) {
		return;
	}
	client->outgoing = 0;

	/* Call the protocol handler which will prepare the response packet */
	if (snmp(client) == -1) {
		logit(LOG_WARNING, errno, "%s %s:%d", req_msg, straddr, sockaddr.my_sin_port);
		close(client->sockfd);
		client->sockfd = -1;
		return;
	}
	if (client->size == 0) {
		logit(LOG_WARNING, 0, "%s %s:%d: ignored", req_msg, straddr, sockaddr.my_sin_port);
		close(client->sockfd);
		client->sockfd = -1;
		return;
	}

	client->outgoing = 1;
}

static char *progname(char *arg0)
{
       char *nm;

       nm = strrchr(arg0, '/');
       if (nm)
	       nm++;
       else
	       nm = arg0;

       return nm;
}

int main(int argc, char *argv[])
{
	static const char short_options[] = "hi:p:P:u:vI:46";
	static const struct option long_options[] = {
		{ "use-ipv4",    0, 0, '4' },
		{ "use-ipv6",    0, 0, '6' },
		{ "help",        0, 0, 'h' },
		{ "interfaces",  1, 0, 'i' },
		{ "listen",      1, 0, 'I' },
		{ "udp-port",    1, 0, 'p' },
		{ "tcp-port",    1, 0, 'P' },
		{ "drop-privs",  1, 0, 'u' },
		{ "version",     0, 0, 'v' },
		{ NULL, 0, 0, 0 }
	};
	int ticks, nfds, c, option_index = 1;
	size_t i;
	fd_set rfds, wfds;
	struct sigaction sig;
	struct ifreq ifreq;
	struct timeval tv_last;
	struct timeval tv_now;
	struct timeval tv_sleep;
	my_socklen_t socklen;
	union {
		struct sockaddr_in sa;
		struct sockaddr_in6 sa6;
	} sockaddr;
#ifdef HAVE_LIBCONFUSE
	char path[256] = "";
	char *config = NULL;
#endif

	g_prognm = progname(argv[0]);

	/* Parse commandline options */
	while (1) {
		c = getopt_long(argc, argv, short_options, long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case '4':
			g_family = AF_INET;
			break;

		case '6':
			g_family = AF_INET6;
			break;
		case 'h':
			return usage(0);

		case 'i':
			g_interface_list_length = split(optarg, ",;", g_interface_list, MAX_NR_INTERFACES);
			break;
#ifndef __FreeBSD__
		case 'I':
			g_bind_to_device = strdup(optarg);
			break;
#endif
		case 'p':
			g_udp_port = atoi(optarg);
			break;

		case 'P':
			g_tcp_port = atoi(optarg);
			break;

		case 'u':
			g_user = optarg;
			break;

		case 'v':
			printf("v" PACKAGE_VERSION "\n");
			return 0;


		default:
			return usage(EXIT_ARGS);
		}
	}

	if (!g_udp_port)
		g_udp_port = 161;		/* standard port, but don't override the user */
	if (!g_tcp_port)
		g_tcp_port = g_udp_port;	/* don't override if it's already set */

	logit(LOG_NOTICE, 0, PROGRAM_IDENT " starting");
	g_timeout *= 100;

	/* Store the starting time since we need it for MIB updates */
	if (gettimeofday(&tv_last, NULL) == -1) {
		memset(&tv_last, 0, sizeof(tv_last));
		memset(&tv_sleep, 0, sizeof(tv_sleep));
	} else {
		tv_sleep.tv_sec = g_timeout / 100;
		tv_sleep.tv_usec = (g_timeout % 100) * 10000;
	}

	/* Prevent TERM and HUP signals from interrupting system calls */
	sig.sa_handler = handle_signal;
	sigemptyset (&sig.sa_mask);
	sig.sa_flags = SA_RESTART;
	sigaction(SIGTERM, &sig, NULL);
	sigaction(SIGINT, &sig, NULL);
	sigaction(SIGHUP, &sig, NULL);

	/* Open the server's UDP port and prepare it for listening */
	g_udp_sockfd = socket((g_family == AF_INET) ? PF_INET : PF_INET6, SOCK_DGRAM, 0);
	if (g_udp_sockfd == -1) {
		logit(LOG_ERR, errno, "could not create UDP socket");
		exit(EXIT_SYSCALL);
	}

	if (g_family == AF_INET) {
		sockaddr.sa.sin_family = g_family;
		sockaddr.sa.sin_port = htons(g_udp_port);
		sockaddr.sa.sin_addr = inaddr_any;
		socklen = sizeof(sockaddr.sa);
	} else {
		sockaddr.sa6.sin6_family = g_family;
		sockaddr.sa6.sin6_port = htons(g_udp_port);
		sockaddr.sa6.sin6_addr = in6addr_any;
		socklen = sizeof(sockaddr.sa6);
	}
	if (bind(g_udp_sockfd, (struct sockaddr *)&sockaddr, socklen) == -1) {
		logit(LOG_ERR, errno, "could not bind UDP socket to port %d", g_udp_port);
		exit(EXIT_SYSCALL);
	}

#ifndef __FreeBSD__
	if (g_bind_to_device) {
		snprintf(ifreq.ifr_ifrn.ifrn_name, sizeof(ifreq.ifr_ifrn.ifrn_name), "%s", g_bind_to_device);
		if (setsockopt(g_udp_sockfd, SOL_SOCKET, SO_BINDTODEVICE, (char *)&ifreq, sizeof(ifreq)) == -1) {
			logit(LOG_WARNING, errno, "could not bind UDP socket to device %s", g_bind_to_device);
			exit(EXIT_SYSCALL);
		}
	}
#endif

	/* Open the server's TCP port and prepare it for listening */
	g_tcp_sockfd = socket((g_family == AF_INET) ? PF_INET : PF_INET6, SOCK_STREAM, 0);
	if (g_tcp_sockfd == -1) {
		logit(LOG_ERR, errno, "could not create TCP socket");
		exit(EXIT_SYSCALL);
	}

#ifndef __FreeBSD__
	if (g_bind_to_device) {
		snprintf(ifreq.ifr_ifrn.ifrn_name, sizeof(ifreq.ifr_ifrn.ifrn_name), "%s", g_bind_to_device);
		if (setsockopt(g_tcp_sockfd, SOL_SOCKET, SO_BINDTODEVICE, (char *)&ifreq, sizeof(ifreq)) == -1) {
			logit(LOG_WARNING, errno, "could not bind TCP socket to device %s", g_bind_to_device);
			exit(EXIT_SYSCALL);
		}
	}
#endif

	c = 1;
	if (setsockopt(g_tcp_sockfd, SOL_SOCKET, SO_REUSEADDR, &c, sizeof(c)) == -1) {
		logit(LOG_WARNING, errno, "could not set SO_REUSEADDR on TCP socket");
		exit(EXIT_SYSCALL);
	}

	if (g_family == AF_INET) {
		sockaddr.sa.sin_family = g_family;
		sockaddr.sa.sin_port = htons(g_udp_port);
		sockaddr.sa.sin_addr = inaddr_any;
		socklen = sizeof(sockaddr.sa);
	} else {
		sockaddr.sa6.sin6_family = g_family;
		sockaddr.sa6.sin6_port = htons(g_udp_port);
		sockaddr.sa6.sin6_addr = in6addr_any;
		socklen = sizeof(sockaddr.sa6);
	}
	if (bind(g_tcp_sockfd, (struct sockaddr *)&sockaddr, socklen) == -1) {
		logit(LOG_ERR, errno, "could not bind TCP socket to port %d", g_tcp_port);
		exit(EXIT_SYSCALL);
	}

	if (listen(g_tcp_sockfd, 128) == -1) {
		logit(LOG_ERR, errno, "could not prepare TCP socket for listening");
		exit(EXIT_SYSCALL);
	}

	/* Print a starting message (so the user knows the args were ok) */
	if (g_bind_to_device)
		logit(LOG_NOTICE, 0, "Listening on port %d/udp and %d/tcp on interface %s",
		      g_udp_port, g_tcp_port, g_bind_to_device);
	else
		logit(LOG_NOTICE, 0, "Listening on port %d/udp and %d/tcp", g_udp_port, g_tcp_port);

	if (g_user && geteuid() == 0) {
		struct passwd *pwd;
		struct group *grp;

		errno = 0;

		pwd = getpwnam(g_user);
		if (pwd == NULL) {
			logit(LOG_ERR, errno, "Unable to get UID for user \"%s\"", g_user);
			exit(EXIT_SYSCALL);
		}

		errno = 0;

		grp = getgrgid(pwd->pw_gid);
		if (grp == NULL) {
			logit(LOG_ERR, errno, "Unable to get GID for group \"%d\"", (unsigned int)pwd->pw_gid);
			exit(EXIT_SYSCALL);
		}

		if (setgid(grp->gr_gid) == -1) {
			logit(LOG_ERR, errno, "Unable to set new group \"%s\"", g_user);
			exit(EXIT_SYSCALL);
		}

		if (setuid(pwd->pw_uid) == -1) {
			logit(LOG_ERR, errno, "Unable to set new user \"%s\"", g_user);
			exit(EXIT_SYSCALL);
		}

		logit(LOG_NOTICE, 0, "Successfully dropped privileges to %s:%s", pwd->pw_name, grp->gr_name);
	}

	/* Handle incoming connect requests and incoming data */
	while (!g_quit) {
		/* Sleep until we get a request or the timeout is over */
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(g_udp_sockfd, &rfds);
		FD_SET(g_tcp_sockfd, &rfds);
		nfds = (g_udp_sockfd > g_tcp_sockfd) ? g_udp_sockfd : g_tcp_sockfd;

		for (i = 0; i < g_tcp_client_list_length; i++) {
			if (g_tcp_client_list[i]->outgoing)
				FD_SET(g_tcp_client_list[i]->sockfd, &wfds);
			else
				FD_SET(g_tcp_client_list[i]->sockfd, &rfds);

			if (nfds < g_tcp_client_list[i]->sockfd)
				nfds = g_tcp_client_list[i]->sockfd;
		}

		if (select(nfds + 1, &rfds, &wfds, NULL, &tv_sleep) == -1) {
			if (g_quit)
				break;

			logit(LOG_ERR, errno, "could not select from sockets");
			exit(EXIT_SYSCALL);
		}

		/* Determine whether to update the MIB and the next ticks to sleep */
		ticks = ticks_since(&tv_last, &tv_now);
		if (ticks < 0 || ticks >= g_timeout) {
			memcpy(&tv_last, &tv_now, sizeof(tv_now));
			tv_sleep.tv_sec = g_timeout / 100;
			tv_sleep.tv_usec = (g_timeout % 100) * 10000;
		} else {
			tv_sleep.tv_sec = (g_timeout - ticks) / 100;
			tv_sleep.tv_usec = ((g_timeout - ticks) % 100) * 10000;
		}

		/* Handle UDP packets, TCP packets and TCP connection connects */
		if (FD_ISSET(g_udp_sockfd, &rfds))
			handle_udp_client();

		if (FD_ISSET(g_tcp_sockfd, &rfds))
			handle_tcp_connect();

		for (i = 0; i < g_tcp_client_list_length; i++) {
			if (g_tcp_client_list[i]->outgoing) {
				if (FD_ISSET(g_tcp_client_list[i]->sockfd, &wfds))
					handle_tcp_client_write(g_tcp_client_list[i]);
			} else {
				if (FD_ISSET(g_tcp_client_list[i]->sockfd, &rfds))
					handle_tcp_client_read(g_tcp_client_list[i]);
			}
		}

		/* If there was a TCP disconnect, remove the client from the list */
		for (i = 0; i < g_tcp_client_list_length; i++) {
			if (g_tcp_client_list[i]->sockfd == -1) {
				g_tcp_client_list_length--;
				if (i < g_tcp_client_list_length) {
					size_t len = (g_tcp_client_list_length - i) * sizeof(g_tcp_client_list[i]);

					free(g_tcp_client_list[i]);
					memmove(&g_tcp_client_list[i], &g_tcp_client_list[i + 1], len);

					/*
					 * list changed, there could be more than
					 * one to remove, start from begining
					 */
					i = -1;
				}
			}
		}
	}

	/* We were signaled, print a message and exit */
	logit(LOG_NOTICE, 0, PROGRAM_IDENT " stopping");

	return EXIT_OK;
}

/* vim: ts=4 sts=4 sw=4 nowrap
 */
