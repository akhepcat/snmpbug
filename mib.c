/*
 * Copyright (C) 2008-2010  Robert Ernst <robert.ernst@linux-solutions.at>
 * Copyright (C) 2011       Javier Palacios <javiplx@gmail.com>
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

#include <limits.h>
#include <sys/time.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>		/* intptr_t/uintptr_t */
#include <errno.h>
#include <time.h>

#include "snmpbug.h"

/*
 * Module variables
 *
 * To extend the MIB, add the definition of the SNMP table here. Note that the
 * variables use OIDs that have two subids more, which both are specified in the
 * mib_build_entry() and mib_build_entries() function calls. For example, the
 * system table uses the OID .1.3.6.1.2.1.1, the first system table variable,
 * system.sysDescr.0 (using OID .1.3.6.1.2.1.1.1.0) is appended to the MIB using
 * the function call mib_build_entry(&m_system_oid, 1, 0, ...).
 *
 * The first parameter is the array containing the list of subids (up to 14 here),
 * the next is the number of subids. The last parameter is the length that this
 * OID will need encoded in SNMP packets (including the BER type and length fields).
 */


struct in_sort {
	int pos;
	unsigned int addr;
} sorted_interface_list[MAX_NR_INTERFACES];

int in_cmp(const void *p1, const void *p2)
{
	struct in_sort *a = (struct in_sort *)p1;
	struct in_sort *b = (struct in_sort *)p2;

	return (int)(a->addr - b->addr);
}

void sort_addr(netinfo_t *netinfo)
{
	size_t i;

	for (i = 0; i < g_interface_list_length; i++) {
		sorted_interface_list[i].pos  = i;
		sorted_interface_list[i].addr = netinfo->in_addr[i];
	}

	qsort(sorted_interface_list, g_interface_list_length, sizeof(struct in_sort), in_cmp);
}


/* -----------------------------------------------------------------------------
 * Interface functions
 *
 * To extend the MIB, add the relevant mib_build_entry() calls (to add one MIB
 * variable) or mib_build_entries() calls (to add a column of a MIB table) in
 * the mib_build() function. Note that building the MIB must be done strictly in
 * ascending OID order or the SNMP getnext/getbulk functions will not work as
 * expected!
 *
 * To extend the MIB, add the relevant mib_update_entry() calls (to update one
 * MIB variable or one cell in a MIB table) in the mib_update() function. Note
 * that the MIB variables must be added in the correct order (i.e. ascending).
 * How to get the value for that variable is up to you, but bear in mind that
 * the mib_update() function is called between receiving the request from the
 * client and sending back the response; thus you should avoid time-consuming
 * actions!
 *
 * The variable types supported up to now are OCTET_STRING, INTEGER (32 bit
 * signed), COUNTER (32 bit unsigned), TIME_TICKS (32 bit unsigned, in 1/10s)
 * and OID.
 *
 * Note that the maximum number of MIB variables is restricted by the length of
 * the MIB array, (see snmpbug.h for the value of MAX_NR_VALUES).
 */

int mib_build(void)
{
	return 0;
}

/* Find the OID in the MIB that is exactly the given one or a subid */
value_t *mib_find(const oid_t *oid, size_t *pos)
{
	while (*pos < g_mib_length) {
		value_t *curr = &g_mib[*pos];
		size_t len = oid->subid_list_length * sizeof(oid->subid_list[0]);

		if (curr->oid.subid_list_length >= oid->subid_list_length &&
		    !memcmp(curr->oid.subid_list, oid->subid_list, len))
			return curr;
		*pos = *pos + 1;
	}

	return NULL;
}

/* Find the OID in the MIB that is the one after the given one */
value_t *mib_findnext(const oid_t *oid)
{
	size_t pos;

	for (pos = 0; pos < g_mib_length; pos++) {
		if (oid_cmp(&g_mib[pos].oid, oid) > 0)
			return &g_mib[pos];
	}

	return NULL;
}

/* vim: ts=4 sts=4 sw=4 nowrap
 */
