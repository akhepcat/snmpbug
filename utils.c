/* Utility functions
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

#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif
#include <limits.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>

#include "snmpbug.h"

void *allocate(size_t len)
{
	char *buf = malloc(len);

	if (!buf) {
		logit(LOG_DEBUG, errno, "Failed allocating memory");
		return NULL;
	}

	return buf;
}


int ticks_since(const struct timeval *tv_last, struct timeval *tv_now)
{
	float ticks;

	if (gettimeofday(tv_now, NULL) == -1) {
		logit(LOG_WARNING, errno, "could not get ticks");
		return -1;
	}

	if (tv_now->tv_sec < tv_last->tv_sec || (tv_now->tv_sec == tv_last->tv_sec && tv_now->tv_usec < tv_last->tv_usec)) {
		logit(LOG_WARNING, 0, "could not get ticks: time running backwards");
		return -1;
	}

	ticks = (float)(tv_now->tv_sec - 1 - tv_last->tv_sec) * 100.0 + (float)((tv_now->tv_usec + 1000000 - tv_last->tv_usec) / 10000);
	if (ticks < INT_MIN)
		return INT_MIN;
	if (ticks > INT_MAX)
		return INT_MAX;

	return ticks;
}

char *oid_ntoa(const oid_t *oid)
{
	size_t i, len = 0;
	static char buf[MAX_NR_SUBIDS * 10 + 2];

	buf[0] = '\0';
	for (i = 0; i < oid->subid_list_length; i++) {
		len += snprintf(buf + len, sizeof(buf) - len, ".%u", oid->subid_list[i]);
		if (len >= sizeof(buf))
			break;
	}

	return buf;
}

oid_t *oid_aton(const char *str)
{
	static oid_t oid;
	char *ptr = (char *)str;

	if (!str)
		return NULL;

	oid.subid_list_length = 0;
	while (*ptr != 0) {
		if (oid.subid_list_length >= MAX_NR_SUBIDS)
			return NULL;

		if (*ptr != '.')
			return NULL;

		ptr++;
		if (*ptr == 0)
			return NULL;

		oid.subid_list[oid.subid_list_length++] = strtoul(ptr, &ptr, 0);
	}

	if (oid.subid_list_length < 2 || (oid.subid_list[0] * 40 + oid.subid_list[1]) > 0xFF)
		return NULL;

	return &oid;
}

int oid_cmp(const oid_t *oid1, const oid_t *oid2)
{
	int subid1, subid2;
	size_t i;

	for (i = 0; i < MAX_NR_OIDS; i++) {
		subid1 = (oid1->subid_list_length > i) ? (int)oid1->subid_list[i] : -1;
		subid2 = (oid2->subid_list_length > i) ? (int)oid2->subid_list[i] : -1;

		if (subid1 == -1 && subid2 == -1)
			return 0;
		if (subid1 > subid2)
			return 1;
		if (subid1 < subid2)
			return -1;
	}

	return 0;
}

int split(const char *str, char *delim, char **list, int max_list_length)
{
	int len = 0;
	char *ptr;
	char *buf = strdup(str);

	if (!buf)
		return 0;

	for (ptr = strtok(buf, delim); ptr; ptr = strtok(NULL, delim)) {
		if (len < max_list_length)
			list[len++] = strdup(ptr);
	}

	free(buf);

	return len;
}

client_t *find_oldest_client(void)
{
	size_t i, found = 0, pos = 0;
	time_t timestamp = (time_t)LONG_MAX;

	for (i = 0; i < g_tcp_client_list_length; i++) {
		if (timestamp > g_tcp_client_list[i]->timestamp) {
			timestamp = g_tcp_client_list[i]->timestamp;
			found = 1;
			pos = i;
		}
	}

	return found ? g_tcp_client_list[pos] : NULL;
}

int logit(int priority, int syserr, const char *fmt, ...)
{
	va_list ap;
	char *buf;
	int len, i;

	if (LOG_PRI(priority) > g_level)
		return 0;

	va_start(ap, fmt);
	len = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (len < 0)
		return -1;

	/* length of ": error-message" */
	len += 3 + (syserr > 0 ? strlen(strerror(syserr)) : 0);
	buf = alloca(len);
	if (!buf)
		return -1;

	va_start(ap, fmt);
	i = vsnprintf(buf, len, fmt, ap);
	va_end(ap);
	if (i < 0)
		return -1;

	if (syserr > 0)
		i += snprintf(&buf[i], len - i, ": %s", strerror(syserr));

	if (g_syslog)
		syslog(priority, "%s", buf);
	else {
		i = fprintf(stdout, "%s\n", buf);
		fflush(stdout);
	}

	return i;
}

/* vim: ts=4 sts=4 sw=4 nowrap
 */
