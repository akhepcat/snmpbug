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

static const oid_t m_system_oid         = { { 1, 3, 6, 1, 2, 1, 1               },  7, 8  };
static const oid_t m_if_1_oid           = { { 1, 3, 6, 1, 2, 1, 2               },  7, 8  };
static const oid_t m_if_2_oid           = { { 1, 3, 6, 1, 2, 1, 2, 2, 1         },  9, 10 };
static const oid_t m_ip_oid             = { { 1, 3, 6, 1, 2, 1, 4               },  7, 8  };
static const oid_t m_tcp_oid            = { { 1, 3, 6, 1, 2, 1, 6               },  7, 8  };
static const oid_t m_udp_oid            = { { 1, 3, 6, 1, 2, 1, 7               },  7, 8  };
static const oid_t m_host_oid           = { { 1, 3, 6, 1, 2, 1, 25, 1           },  8, 9  };
static const oid_t m_ifxtable_oid       = { { 1, 3, 6, 1, 2, 1, 31, 1, 1, 1     }, 10, 11 };
static const oid_t m_memory_oid         = { { 1, 3, 6, 1, 4, 1, 2021, 4,        },  8, 10 };
static const oid_t m_disk_oid           = { { 1, 3, 6, 1, 4, 1, 2021, 9, 1      },  9, 11 };
static const oid_t m_load_oid           = { { 1, 3, 6, 1, 4, 1, 2021, 10, 1     },  9, 11 };
static const oid_t m_cpu_oid            = { { 1, 3, 6, 1, 4, 1, 2021, 11        },  8, 10 };
#ifdef CONFIG_ENABLE_DEMO
static const oid_t m_demo_oid           = { { 1, 3, 6, 1, 4, 1, 99999           },  7, 10 };
#endif

static const int m_load_avg_times[3] = { 1, 5, 15 };

static int oid_build  (oid_t *oid, const oid_t *prefix, int column, int row);
static int encode_oid_len (oid_t *oid);

static int data_alloc (data_t *data, int type);
static int data_set   (data_t *data, int type, const void *arg);


static int encode_integer(data_t *data, int integer_value)
{
	unsigned char *buffer;
	int length;

	buffer = data->buffer;
	if (integer_value < -8388608 || integer_value > 8388607)
		length = 4;
	else if (integer_value < -32768 || integer_value > 32767)
		length = 3;
	else if (integer_value < -128 || integer_value > 127)
		length = 2;
	else
		length = 1;

	*buffer++ = BER_TYPE_INTEGER;
	*buffer++ = length;
	while (length--)
		*buffer++ = ((unsigned int)integer_value >> (8 * length)) & 0xFF;

	data->encoded_length = buffer - data->buffer;

	return 0;
}

static int encode_byte_array(data_t *data, const char *string, size_t len)
{
	unsigned char *buffer;

	if (!string)
		return 2;

	if ((len + 4) > data->max_length) {
		data->max_length = len + 4;
		data->buffer = realloc(data->buffer, data->max_length);
		if (!data->buffer)
			return 2;
	}

	if (len > 0xFFFF) {
		logit(LOG_ERR, 0, "Failed encoding: OCTET STRING overflow");
		return -1;
	}

	buffer    = data->buffer;
	*buffer++ = BER_TYPE_OCTET_STRING;
	if (len > 255) {
		*buffer++ = 0x82;
		*buffer++ = (len >> 8) & 0xFF;
		*buffer++ = len & 0xFF;
	} else if (len > 127) {
		*buffer++ = 0x81;
		*buffer++ = len & 0xFF;
	} else {
		*buffer++ = len & 0x7F;
	}

	while (len--)
		*buffer++ = *string++;

	data->encoded_length = buffer - data->buffer;

	return 0;
}


static int encode_ipaddress(data_t *data, int ipaddress)
{
	unsigned char *buffer;
	int length = 4;

	buffer = data->buffer;

	*buffer++ = BER_TYPE_IP_ADDRESS;
	*buffer++ = length;
	while (length--)
		*buffer++ = (ipaddress >> (8 * length)) & 0xFF;

	data->encoded_length = buffer - data->buffer;

	return 0;
}

static int encode_string(data_t *data, const char *string)
{
	if (!string)
		return 2;

	return encode_byte_array(data, string, strlen(string));
}

static int encode_oid(data_t *data, const oid_t *oid)
{
	size_t i, len = 1;
	unsigned char *buffer = data->buffer;

	if (!oid)
		return 2;

	for (i = 2; i < oid->subid_list_length; i++) {
		if (oid->subid_list[i] >= (1 << 28))
			len += 5;
		else if (oid->subid_list[i] >= (1 << 21))
			len += 4;
		else if (oid->subid_list[i] >= (1 << 14))
			len += 3;
		else if (oid->subid_list[i] >= (1 << 7))
			len += 2;
		else
			len += 1;
	}

	if (len > 0xFFFF) {
		logit(LOG_ERR, 0, "Failed encoding '%s': OID overflow", oid_ntoa(oid));
		return -1;
	}

	*buffer++ = BER_TYPE_OID;
	if (len > 0xFF) {
		*buffer++ = 0x82;
		*buffer++ = (len >> 8) & 0xFF;
		*buffer++ = len & 0xFF;
	} else if (len > 0x7F) {
		*buffer++ = 0x81;
		*buffer++ = len & 0xFF;
	} else {
		*buffer++ = len & 0x7F;
	}

	*buffer++ = oid->subid_list[0] * 40 + oid->subid_list[1];
	for (i = 2; i < oid->subid_list_length; i++) {
		if (oid->subid_list[i] >= (1 << 28))
			len = 5;
		else if (oid->subid_list[i] >= (1 << 21))
			len = 4;
		else if (oid->subid_list[i] >= (1 << 14))
			len = 3;
		else if (oid->subid_list[i] >= (1 << 7))
			len = 2;
		else
			len = 1;

		while (len--) {
			if (len)
				*buffer++ = ((oid->subid_list[i] >> (7 * len)) & 0x7F) | 0x80;
			else
				*buffer++ = (oid->subid_list[i] >> (7 * len)) & 0x7F;
		}
	}

	data->encoded_length = buffer - data->buffer;

	return 0;
}

static int encode_unsigned(data_t *data, int type, unsigned int ticks_value)
{
	unsigned char *buffer;
	int length;

	buffer = data->buffer;
	if (ticks_value & 0xFF800000)
		length = 4;
	else if (ticks_value & 0x007F8000)
		length = 3;
	else if (ticks_value & 0x00007F80)
		length = 2;
	else
		length = 1;

	*buffer++ = type;
	*buffer++ = length;
	while (length--)
		*buffer++ = (ticks_value >> (8 * length)) & 0xFF;

	data->encoded_length = buffer - data->buffer;

	return 0;
}

static int encode_unsigned64(data_t *data, int type, uint64_t ticks_value)
{
	unsigned char *buffer;
	int length;

	buffer = data->buffer;
	if (ticks_value & 0xFF80000000000000ULL)
		length = 8;
	else if (ticks_value & 0x007F800000000000ULL)
		length = 7;
	else if (ticks_value & 0x00007F8000000000ULL)
		length = 6;
	else if (ticks_value & 0x0000007F80000000ULL)
		length = 5;
	else if (ticks_value & 0x000000007F800000ULL)
		length = 4;
	else if (ticks_value & 0x00000000007F8000ULL)
		length = 3;
	else if (ticks_value & 0x0000000000007F80ULL)
		length = 2;
	else
		length = 1;

	*buffer++ = type;
	*buffer++ = length;
	while (length--)
		*buffer++ = (ticks_value >> (8 * length)) & 0xFF;

	data->encoded_length = buffer - data->buffer;

	return 0;
}

static int mib_build_ip_entry(const oid_t *prefix, int type, const void *arg)
{
	int ret;
	value_t *value;
	const char *msg = "Failed creating MIB entry";
	const char *msg2 = "Failed assigning value to OID";

	/* Create a new entry in the MIB table */
	if (g_mib_length >= MAX_NR_VALUES) {
		logit(LOG_ERR, 0, "%s '%s': table overflow", msg, oid_ntoa(prefix));
		return -1;
	}

	value = &g_mib[g_mib_length++];
	memcpy(&value->oid, prefix, sizeof(value->oid));

	ret  = encode_oid_len(&value->oid);
	ret += data_alloc(&value->data, type);
	if (ret) {
		logit(LOG_ERR, 0, "%s '%s': unsupported type %d", msg,
		      oid_ntoa(&value->oid), type);
		return -1;
	}

	ret = data_set(&value->data, type, arg);
	if (ret) {
		if (ret == 1)
			logit(LOG_ERR, 0, "%s '%s': unsupported type %d", msg2, oid_ntoa(&value->oid), type);
		else if (ret == 2)
			logit(LOG_ERR, 0, "%s '%s': invalid default value", msg2, oid_ntoa(&value->oid));

		return -1;
	}

	return 0;
}

static value_t *mib_alloc_entry(const oid_t *prefix, int column, int row, int type)
{
	int ret;
	value_t *value;
	const char *msg = "Failed creating MIB entry";

	/* Create a new entry in the MIB table */
	if (g_mib_length >= MAX_NR_VALUES) {
		logit(LOG_ERR, 0, "%s '%s.%d.%d': table overflow", msg, oid_ntoa(prefix), column, row);
		return NULL;
	}

	value = &g_mib[g_mib_length++];
	memcpy(&value->oid, prefix, sizeof(value->oid));

	/* Create the OID from the prefix, the column and the row */
	if (oid_build(&value->oid, prefix, column, row)) {
		logit(LOG_ERR, 0, "%s '%s.%d.%d': oid overflow", msg, oid_ntoa(prefix), column, row);
		return NULL;
	}

	ret  = encode_oid_len(&value->oid);
	ret += data_alloc(&value->data, type);
	if (ret) {
		logit(LOG_ERR, 0, "%s '%s.%d.%d': unsupported type %d", msg,
		      oid_ntoa(&value->oid), column, row, type);
		return NULL;
	}

	return value;
}

static int mib_data_set(const oid_t *oid, data_t *data, int column, int row, int type, const void *arg);

static int mib_build_entry(const oid_t *prefix, int column, int row, int type, const void *arg)
{
	value_t *value;

	value = mib_alloc_entry(prefix, column, row, type);
	if (!value)
		return -1;

	return mib_data_set(&value->oid, &value->data, column, row, type, arg);
}

static int mib_data_set(const oid_t *oid, data_t *data, int column, int row, int type, const void *arg)
{
	int ret;
	const char *msg = "Failed assigning value to OID";

	ret = data_set(data, type, arg);
	if (ret) {
		if (ret == 1)
			logit(LOG_ERR, 0, "%s '%s.%d.%d': unsupported type %d", msg, oid_ntoa(oid), column, row, type);
		else if (ret == 2)
			logit(LOG_ERR, 0, "%s '%s.%d.%d': invalid default value", msg, oid_ntoa(oid), column, row);

		return -1;
	}

	return 0;
}


/* Create OID from the given prefix, column, and row */
static int oid_build(oid_t *oid, const oid_t *prefix, int column, int row)
{
	memcpy(oid, prefix, sizeof(*oid));

	if (oid->subid_list_length >= MAX_NR_SUBIDS)
		return -1;

	oid->subid_list[oid->subid_list_length++] = column;

	if (oid->subid_list_length >= MAX_NR_SUBIDS)
		return -1;

	oid->subid_list[oid->subid_list_length++] = row;

	return 0;
}

/*
 * Calculate the encoded length of the created OID (note: first the length
 * of the subid list, then the length of the length/type header!)
 */
static int encode_oid_len(oid_t *oid)
{
	uint32_t len = 1;
	size_t i;

	for (i = 2; i < oid->subid_list_length; i++) {
		if (oid->subid_list[i] >= (1 << 28))
			len += 5;
		else if (oid->subid_list[i] >= (1 << 21))
			len += 4;
		else if (oid->subid_list[i] >= (1 << 14))
			len += 3;
		else if (oid->subid_list[i] >= (1 << 7))
			len += 2;
		else
			len += 1;
	}

	if (len > 0xFFFF) {
		logit(LOG_ERR, 0, "Failed encoding '%s': OID overflow", oid_ntoa(oid));
		oid->encoded_length = -1;
		return -1;
	}

	if (len > 0xFF)
		len += 4;
	else if (len > 0x7F)
		len += 3;
	else
		len += 2;

	oid->encoded_length = (short)len;

	return 0;
}

/* Create a data buffer for the value depending on the type:
 *
 * - strings and oids are assumed to be static or have the maximum allowed length
 * - integers are assumed to be dynamic and don't have more than 32 bits
 */
static int data_alloc(data_t *data, int type)
{
	switch (type) {
	case BER_TYPE_INTEGER:
		data->max_length = sizeof(int) + 2;
		data->encoded_length = 0;
		data->buffer = allocate(data->max_length);
		break;

	case BER_TYPE_IP_ADDRESS:
		data->max_length = sizeof(uint32_t) + 2;
		data->encoded_length = 0;
		data->buffer = allocate(data->max_length);
		break;

	case BER_TYPE_OCTET_STRING:
		data->max_length = 4;
		data->encoded_length = 0;
		data->buffer = allocate(data->max_length);
		break;

	case BER_TYPE_OID:
		data->max_length = MAX_NR_SUBIDS * 5 + 4;
		data->encoded_length = 0;
		data->buffer = allocate(data->max_length);
		break;

	case BER_TYPE_COUNTER64:
		data->max_length = sizeof(uint64_t) + 2;
		data->encoded_length = 0;
		data->buffer = allocate(data->max_length);
		break;

	case BER_TYPE_COUNTER:
	case BER_TYPE_GAUGE:
	case BER_TYPE_TIME_TICKS:
		data->max_length = sizeof(unsigned int) + 3;
		data->encoded_length = 0;
		data->buffer = allocate(data->max_length);
		break;

	case BER_TYPE_NULL:
		data->max_length = 0 + 3;
		data->encoded_length = 0;
		data->buffer = allocate(data->max_length);
		break;

	default:
		return -1;
	}

	if (!data->buffer)
		return -1;

	data->buffer[0] = type;
	data->buffer[1] = 0;
	data->buffer[2] = 0;
	data->encoded_length = 3;

	return 0;
}

/*
 * Set data buffer to its new value, depending on the type.
 *
 * Note: we assume the buffer was allocated to hold the maximum possible
 *       value when the MIB was built.
 */
static int data_set(data_t *data, int type, const void *arg)
{
	/* Make sure to always initialize the buffer, in case of error below. */
	memset(data->buffer, 0, data->max_length);

	switch (type) {
	case BER_TYPE_INTEGER:
		return encode_integer(data, (intptr_t)arg);

	case BER_TYPE_IP_ADDRESS:
		return encode_ipaddress(data, (uintptr_t)arg);

	case BER_TYPE_OCTET_STRING:
		return encode_string(data, (const char *)arg);

	case BER_TYPE_OID:
		return encode_oid(data, oid_aton((const char *)arg));

	case BER_TYPE_COUNTER64:
		return encode_unsigned64(data, type, *((uint64_t *)arg));

	case BER_TYPE_COUNTER:
	case BER_TYPE_GAUGE:
	case BER_TYPE_TIME_TICKS:
		return encode_unsigned(data, type, (uintptr_t)arg);

	case BER_TYPE_NULL:
		return 0;

	default:
		break;	/* Fall through */
	}

	return 1;
}

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

static int build_ip_mib(oid_t *oid, int type, unsigned int in_addr[], unsigned int value[])
{
	size_t i;

	for (i = 0; i < g_interface_list_length; i++) {
		unsigned int ip;
		int pos;
		int j;

		/* Find position in in_addr[] and value[] */
		pos = sorted_interface_list[i].pos;

		if (!in_addr[pos])
			continue;

		ip = htonl(in_addr[pos]);
		for (j = 0; j < 4; ++j)
			oid->subid_list[10 + j] = ((ip & (0xFF << (j * 8))) >> (j * 8));

		if (mib_build_ip_entry(oid, type, (const void *)(intptr_t)value[pos]) == -1)
			return -1;
	}

	return 0;
}

static int build_int(const oid_t *oid, int col, int row, unsigned int val)
{
	return mib_build_entry(oid, col, row, BER_TYPE_INTEGER, (const void *)(intptr_t)val);
}

static int build_str(const oid_t *oid, int col, int row, char *str)
{
	return mib_build_entry(oid, col, row, BER_TYPE_OCTET_STRING, str);
}

static int build_gge(const oid_t *oid, int col, int row, unsigned int val)
{
	return mib_build_entry(oid, col, row, BER_TYPE_GAUGE, (const void *)(intptr_t)val);
}

static int build_tm(const oid_t *oid, int col, int row, unsigned int tm)
{
	return mib_build_entry(oid, col, row, BER_TYPE_TIME_TICKS, (const void *)(intptr_t)tm);
}

static int mib_build_entries(const oid_t *prefix, int column, int row_from, int row_to, int type)
{
	int row;

	for (row = row_from; row <= row_to; row++) {
		if (!mib_alloc_entry(prefix, column, row, type))
			return -1;
	}

	return 0;
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
	netinfo_t netinfo;
	char hostname[MAX_STRING_SIZE];
	char name[16];
	size_t i;
	int sysServices;

	sysServices = ((1 << 0) +	/* Physical layer */
		       (1 << 1) +	/* L2 Datalink layer */
		       (1 << 2) +	/* L3 IP Layer */
		       (1 << 3) +	/* L4 TCP/UDP Layer */
		       (1 << 6));	/* Applications layer */

	/* Determine some static values that are not known at compile-time */
	if (gethostname(hostname, sizeof(hostname)) == -1)
		hostname[0] = '\0';
	else
		hostname[sizeof(hostname) - 1] = '\0';

	/* get_netinfo(&netinfo); */

	/*
	 * The system MIB: basic info about the host (SNMPv2-MIB.txt)
	 * Caution: on changes, adapt the corresponding mib_update() section too!
	 */
	if (mib_build_entry(&m_system_oid, 1, 0, BER_TYPE_OCTET_STRING, g_description) == -1 ||
	    mib_build_entry(&m_system_oid, 2, 0, BER_TYPE_OID,          g_vendor)      == -1 ||
	    !mib_alloc_entry(&m_system_oid, 3, 0, BER_TYPE_TIME_TICKS)                        ||
	    mib_build_entry(&m_system_oid, 4, 0, BER_TYPE_OCTET_STRING, g_contact)     == -1 ||
	    mib_build_entry(&m_system_oid, 5, 0, BER_TYPE_OCTET_STRING, hostname)      == -1 ||
	    mib_build_entry(&m_system_oid, 6, 0, BER_TYPE_OCTET_STRING, g_location)    == -1 ||
	    mib_build_entry(&m_system_oid, 7, 0, BER_TYPE_INTEGER, (const void *)(intptr_t)sysServices) == -1)
		return -1;

	/*
	 * The interface MIB: network interfaces (IF-MIB.txt)
	 * Caution: on changes, adapt the corresponding mib_update() section too!
	 */
	if (g_interface_list_length > 0) {
		if (build_int(&m_if_1_oid, 1, 0, g_interface_list_length) == -1)
			return -1;

		/* ifIndex */
		for (i = 0; i < g_interface_list_length; i++) {
			if (build_int(&m_if_2_oid, 1, i + 1, netinfo.ifindex[i]) == -1)
				return -1;
		}

		/* ifDescription */
		for (i = 0; i < g_interface_list_length; i++) {
			if (build_str(&m_if_2_oid, 2, i + 1, g_interface_list[i]) == -1)
				return -1;
		}

		/* ifType: ENUM, ethernetCsmacd(6) <-- recommended for all types of Ethernets */
		for (i = 0; i < g_interface_list_length; i++) {
			if (build_int(&m_if_2_oid, 3, i + 1, 6) == -1)
				return -1;
		}

		/* ifMtu */
		for (i = 0; i < g_interface_list_length; i++) {
			if (build_int(&m_if_2_oid, 4, i + 1, 1500) == -1)
				return -1;
		}

		/* ifSpeed (in bps) */
		for (i = 0; i < g_interface_list_length; i++) {
			if (build_gge(&m_if_2_oid, 5, i + 1, 1000000000) == -1)
				return -1;
		}

		/* ifPhysAddress */
		for (i = 1; i <= g_interface_list_length; i++) {
			if (build_str(&m_if_2_oid, 6, i, "") == -1)
				return -1;
		}

		/* ifAdminStatus: up(1), down(2), testing(3) */
		for (i = 0; i < g_interface_list_length; i++) {
			if (build_int(&m_if_2_oid, 7, i + 1, 1) == -1)
				return -1;
		}

		/* ifOperStatus: up(1), down(2), testing(3), unknown(4), dormant(5), notPresent(6), lowerLayerDown(7) */
		for (i = 0; i < g_interface_list_length; i++) {
			if (build_int(&m_if_2_oid, 8, i + 1, 1) == -1)
				return -1;
		}

		/* ifLastChange */
		for (i = 0; i < g_interface_list_length; i++) {
			if (build_tm(&m_if_2_oid, 9, i + 1, 0) == -1)
				return -1;
		}

		if (mib_build_entries(&m_if_2_oid, 10, 1, g_interface_list_length, BER_TYPE_COUNTER) == -1 ||
		    mib_build_entries(&m_if_2_oid, 11, 1, g_interface_list_length, BER_TYPE_COUNTER) == -1 ||
		    mib_build_entries(&m_if_2_oid, 13, 1, g_interface_list_length, BER_TYPE_COUNTER) == -1 ||
		    mib_build_entries(&m_if_2_oid, 14, 1, g_interface_list_length, BER_TYPE_COUNTER) == -1 ||
		    mib_build_entries(&m_if_2_oid, 16, 1, g_interface_list_length, BER_TYPE_COUNTER) == -1 ||
		    mib_build_entries(&m_if_2_oid, 17, 1, g_interface_list_length, BER_TYPE_COUNTER) == -1 ||
		    mib_build_entries(&m_if_2_oid, 19, 1, g_interface_list_length, BER_TYPE_COUNTER) == -1 ||
		    mib_build_entries(&m_if_2_oid, 20, 1, g_interface_list_length, BER_TYPE_COUNTER) == -1)
			return -1;
	}

	/*
	 * The IP-MIB.
	 */
	if (!mib_alloc_entry(&m_ip_oid,  1, 0, BER_TYPE_INTEGER) ||
	    !mib_alloc_entry(&m_ip_oid,  2, 0, BER_TYPE_INTEGER) ||
	    !mib_alloc_entry(&m_ip_oid, 13, 0, BER_TYPE_INTEGER) )
		return -1;

	{
		oid_t m_ip_adentryaddr_oid   = { { 1, 3, 6, 1, 2, 1, 4, 20, 1, 1, 0, 0, 0, 0 },  14, 15  };
		oid_t m_ip_adentryifidx_oid  = { { 1, 3, 6, 1, 2, 1, 4, 20, 1, 2, 0, 0, 0, 0 },  14, 15  };
		oid_t m_ip_adentrymask_oid   = { { 1, 3, 6, 1, 2, 1, 4, 20, 1, 3, 0, 0, 0, 0 },  14, 15  };
		oid_t m_ip_adentrybcaddr_oid = { { 1, 3, 6, 1, 2, 1, 4, 20, 1, 4, 0, 0, 0, 0 },  14, 15  };

		sort_addr(&netinfo);

		build_ip_mib(&m_ip_adentryaddr_oid,   BER_TYPE_IP_ADDRESS, netinfo.in_addr, netinfo.in_addr);
		build_ip_mib(&m_ip_adentryifidx_oid,  BER_TYPE_INTEGER,    netinfo.in_addr, netinfo.ifindex);
		build_ip_mib(&m_ip_adentrymask_oid,   BER_TYPE_IP_ADDRESS, netinfo.in_addr, netinfo.in_mask);
		build_ip_mib(&m_ip_adentrybcaddr_oid, BER_TYPE_INTEGER,    netinfo.in_addr, netinfo.in_bcent);
	}

	/*
	 * The TCP-MIB.
	 */
	if (!mib_alloc_entry(&m_tcp_oid,  1, 0, BER_TYPE_INTEGER) ||
	    !mib_alloc_entry(&m_tcp_oid,  2, 0, BER_TYPE_INTEGER) ||
	    !mib_alloc_entry(&m_tcp_oid,  3, 0, BER_TYPE_INTEGER) ||
	    !mib_alloc_entry(&m_tcp_oid,  4, 0, BER_TYPE_INTEGER) ||
	    !mib_alloc_entry(&m_tcp_oid,  5, 0, BER_TYPE_COUNTER) ||
	    !mib_alloc_entry(&m_tcp_oid,  6, 0, BER_TYPE_COUNTER) ||
	    !mib_alloc_entry(&m_tcp_oid,  7, 0, BER_TYPE_COUNTER) ||
	    !mib_alloc_entry(&m_tcp_oid,  8, 0, BER_TYPE_COUNTER) ||
	    !mib_alloc_entry(&m_tcp_oid,  9, 0, BER_TYPE_GAUGE)   ||
	    !mib_alloc_entry(&m_tcp_oid, 10, 0, BER_TYPE_COUNTER) ||
	    !mib_alloc_entry(&m_tcp_oid, 11, 0, BER_TYPE_COUNTER) ||
	    !mib_alloc_entry(&m_tcp_oid, 12, 0, BER_TYPE_COUNTER) ||
	    !mib_alloc_entry(&m_tcp_oid, 14, 0, BER_TYPE_COUNTER) ||
	    !mib_alloc_entry(&m_tcp_oid, 15, 0, BER_TYPE_COUNTER))
		return -1;

	/*
	 * The UDP-MIB.
	 */
	if (!mib_alloc_entry(&m_udp_oid,  1, 0, BER_TYPE_COUNTER)   ||
	    !mib_alloc_entry(&m_udp_oid,  2, 0, BER_TYPE_COUNTER)   ||
	    !mib_alloc_entry(&m_udp_oid,  3, 0, BER_TYPE_COUNTER)   ||
	    !mib_alloc_entry(&m_udp_oid,  4, 0, BER_TYPE_COUNTER)   ||
	    !mib_alloc_entry(&m_udp_oid,  8, 0, BER_TYPE_COUNTER64) ||
	    !mib_alloc_entry(&m_udp_oid,  9, 0, BER_TYPE_COUNTER64)) {
		return -1;
	}

	/*
	 * The host MIB: additional host info (HOST-RESOURCES-MIB.txt)
	 * Caution: on changes, adapt the corresponding mib_update() section too!
	 */
	if (!mib_alloc_entry(&m_host_oid, 1, 0, BER_TYPE_TIME_TICKS))
		return -1;

	/*
	 * IF-MIB continuation
	 * ifXTable
	 */
	if (g_interface_list_length > 0) {

		/* ifName */
		for (i = 0; i < g_interface_list_length; i++) {
			if (build_str(&m_ifxtable_oid, 1, i + 1, g_interface_list[i]) == -1)
				return -1;
		}

		/* Counters */
		if (mib_build_entries(&m_ifxtable_oid,  2, 1, g_interface_list_length, BER_TYPE_COUNTER)   == -1 ||
		    mib_build_entries(&m_ifxtable_oid,  3, 1, g_interface_list_length, BER_TYPE_COUNTER)   == -1 ||
		    mib_build_entries(&m_ifxtable_oid,  4, 1, g_interface_list_length, BER_TYPE_COUNTER)   == -1 ||
		    mib_build_entries(&m_ifxtable_oid,  5, 1, g_interface_list_length, BER_TYPE_COUNTER)   == -1 ||
		    mib_build_entries(&m_ifxtable_oid,  6, 1, g_interface_list_length, BER_TYPE_COUNTER64) == -1 ||
		    mib_build_entries(&m_ifxtable_oid,  7, 1, g_interface_list_length, BER_TYPE_COUNTER64) == -1 ||
		    mib_build_entries(&m_ifxtable_oid,  8, 1, g_interface_list_length, BER_TYPE_COUNTER64) == -1 ||
		    mib_build_entries(&m_ifxtable_oid,  9, 1, g_interface_list_length, BER_TYPE_COUNTER64) == -1 ||
		    mib_build_entries(&m_ifxtable_oid, 10, 1, g_interface_list_length, BER_TYPE_COUNTER64) == -1 ||
		    mib_build_entries(&m_ifxtable_oid, 11, 1, g_interface_list_length, BER_TYPE_COUNTER64) == -1 ||
		    mib_build_entries(&m_ifxtable_oid, 12, 1, g_interface_list_length, BER_TYPE_COUNTER64) == -1 ||
		    mib_build_entries(&m_ifxtable_oid, 13, 1, g_interface_list_length, BER_TYPE_COUNTER64) == -1)
			return -1;

		/* ifLinkUpDownTrapEnable */
		for (i = 0; i < g_interface_list_length; i++) {
			if (build_int(&m_ifxtable_oid, 14, i + 1, 2 /* disabled */) == -1)
				return -1;
		}

		/* ifHighSpeed */
		for (i = 0; i < g_interface_list_length; i++) {
			if (build_gge(&m_ifxtable_oid, 15, i + 1, 0) == -1)
				return -1;
		}

		/* ifPromiscuousMode */
		for (i = 0; i < g_interface_list_length; i++) {
			if (build_int(&m_ifxtable_oid, 16, i + 1, 2 /* false */) == -1)
				return -1;
		}

		/* ifConnectorPresent */
		for (i = 0; i < g_interface_list_length; i++) {
			if (build_int(&m_ifxtable_oid, 17, i + 1, 1 /* true */) == -1)
				return -1;
		}

		/* ifAlias */
		for (i = 0; i < g_interface_list_length; i++) {
			if (build_str(&m_ifxtable_oid, 18, i + 1, g_interface_list[i]) == -1)
				return -1;
		}

		/* ifCounterDiscontinuityTime */
		for (i = 0; i < g_interface_list_length; i++) {
			if (build_tm(&m_ifxtable_oid, 19, i + 1, 0) == -1)
				return -1;
		}
	}

	/*
	 * The memory MIB: total/free memory (UCD-SNMP-MIB.txt)
	 * Caution: on changes, adapt the corresponding mib_update() section too!
	 */
	if (!mib_alloc_entry(&m_memory_oid,  5, 0, BER_TYPE_INTEGER) ||
	    !mib_alloc_entry(&m_memory_oid,  6, 0, BER_TYPE_INTEGER) ||
	    !mib_alloc_entry(&m_memory_oid, 13, 0, BER_TYPE_INTEGER) ||
	    !mib_alloc_entry(&m_memory_oid, 14, 0, BER_TYPE_INTEGER) ||
	    !mib_alloc_entry(&m_memory_oid, 15, 0, BER_TYPE_INTEGER))
		return -1;

	/*
	 * The disk MIB: mounted partitions (UCD-SNMP-MIB.txt)
	 * Caution: on changes, adapt the corresponding mib_update() section too!
	 */
	if (g_disk_list_length > 0) {
		for (i = 0; i < g_disk_list_length; i++) {
			if (build_int(&m_disk_oid, 1, i + 1, i + 1) == -1)
				return -1;
		}

		for (i = 0; i < g_disk_list_length; i++) {
			if (build_str(&m_disk_oid, 2, i + 1, g_disk_list[i]) == -1)
				return -1;
		}

		if (mib_build_entries(&m_disk_oid,  6, 1, g_disk_list_length, BER_TYPE_INTEGER) == -1 ||
		    mib_build_entries(&m_disk_oid,  7, 1, g_disk_list_length, BER_TYPE_INTEGER) == -1 ||
		    mib_build_entries(&m_disk_oid,  8, 1, g_disk_list_length, BER_TYPE_INTEGER) == -1 ||
		    mib_build_entries(&m_disk_oid,  9, 1, g_disk_list_length, BER_TYPE_INTEGER) == -1 ||
		    mib_build_entries(&m_disk_oid, 10, 1, g_disk_list_length, BER_TYPE_INTEGER) == -1)
			return -1;
	}

	/*
	 * The load MIB: CPU load averages (UCD-SNMP-MIB.txt)
	 * Caution: on changes, adapt the corresponding mib_update() section too!
	 */
	for (i = 0; i < 3; i++) {
		if (build_int(&m_load_oid, 1, i + 1, i + 1) == -1)
			return -1;
	}

	for (i = 0; i < 3; i++) {
		snprintf(name, sizeof(name), "Load-%d", m_load_avg_times[i]);
		if (build_str(&m_load_oid, 2, i + 1, name) == -1)
			return -1;
	}

	if (mib_build_entries(&m_load_oid, 3, 1, 3, BER_TYPE_OCTET_STRING) == -1)
		return -1;

	for (i = 0; i < 3; i++) {
		snprintf(name, sizeof(name), "%d", m_load_avg_times[i]);
		if (build_str(&m_load_oid, 4, i + 1, name) == -1)
			return -1;
	}

	if (mib_build_entries(&m_load_oid, 5, 1, 3, BER_TYPE_INTEGER) == -1)
		return -1;

	/* The CPU MIB: CPU statistics (UCD-SNMP-MIB.txt)
	 * Caution: on changes, adapt the corresponding mib_update() section too!
	 */
	if (!mib_alloc_entry(&m_cpu_oid, 50, 0, BER_TYPE_COUNTER) ||
	    !mib_alloc_entry(&m_cpu_oid, 51, 0, BER_TYPE_COUNTER) ||
	    !mib_alloc_entry(&m_cpu_oid, 52, 0, BER_TYPE_COUNTER) ||
	    !mib_alloc_entry(&m_cpu_oid, 53, 0, BER_TYPE_COUNTER) ||
	    !mib_alloc_entry(&m_cpu_oid, 59, 0, BER_TYPE_COUNTER) ||
	    !mib_alloc_entry(&m_cpu_oid, 60, 0, BER_TYPE_COUNTER))
		return -1;

	/* The demo MIB: two random integers
	 * Caution: on changes, adapt the corresponding mib_update() section too!
	 */
#ifdef CONFIG_ENABLE_DEMO
	if (!mib_alloc_entry(&m_demo_oid, 1, 0, BER_TYPE_INTEGER) ||
	    !mib_alloc_entry(&m_demo_oid, 2, 0, BER_TYPE_INTEGER))
		return -1;
#endif

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
