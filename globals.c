/* Global variables
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

#include "snmpbug.h"
 
const struct in_addr inaddr_any = { INADDR_ANY };

int       g_family  = AF_INET6;
int       g_timeout = 1;
int       g_auth    = 1;	/* always enable auth, for logging */
int       g_level   = LOG_INFO;	/* to log that auth info */
volatile sig_atomic_t g_quit = 0;

char     *g_prognm;
char     *g_bind_to_device;
char     *g_user;

char     *g_interface_list[MAX_NR_INTERFACES];
size_t    g_interface_list_length;

int       g_udp_sockfd = -1;
int       g_tcp_sockfd = -1;

client_t  g_udp_client;
client_t *g_tcp_client_list[MAX_NR_CLIENTS];
size_t    g_tcp_client_list_length;

/* vim: ts=4 sts=4 sw=4 nowrap
 */
