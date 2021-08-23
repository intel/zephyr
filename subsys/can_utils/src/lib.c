/*
 * INTEL CONFIDENTIAL
 *
 * Copyright 2021 Intel Corporation
 *
 * This software and the related documents are Intel copyrighted materials,
 * and your use of them is governed by the express license under which they
 * were provided to you (License). Unless the License provides otherwise, you
 * may not use, modify, copy, publish, distribute, disclose or transmit this
 * software or the related documents without Intel's prior written permission.
 *
 * This software and the related documents are provided as is, with no express
 * or implied warranties, other than those that are expressly stated in the
 * License.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>


#include <zephyr.h>
#include <sedi.h>
#include <net/socket.h>
#include <net/socket_can.h>
#include "lib.h"
#include <logging/log.h>
LOG_MODULE_REGISTER(can_utils_lib, LOG_LEVEL_DBG);


#define CANID_DELIM '#'
#define DATA_DELIMITOR '.'
#define INVALID_CAN_ID (0xffffffff)/* 32 bit CAN id */
#define INVALID_ASCII_NIBBLE (16)

typedef enum can_frame_type {
	CAN_STD_FR,
	CAN_EXT_FR,
	CAN_FD_FR
} e_can_fr_type;

/*
 * Fill frame MSB first.
 * For STD frame id is 11 bit ie 3 fields
 * For EXT frame id is 29 bit ie 8 fields
 * This is the value in the base_offset
 */
static uint32_t fill_can_frame(
	e_can_fr_type ct,
	unsigned char nibble,
	uint32_t index)
{
	uint8_t base_offset;
	uint32_t ret_u32 = 0;

	base_offset = (ct == CAN_STD_FR) ? 2 : 7;
	if (base_offset >= index) {
		ret_u32 = nibble << ((base_offset - index) * 4);
	} else {
		ret_u32 = INVALID_CAN_ID;
	}
	return ret_u32;
}

/* Convert ascii code to hex nibble */
unsigned char asc2nibble(
	char c
	)
{
	if ((c >= '0') && (c <= '9')) {
		return c - '0';
	}
	if ((c >= 'A') && (c <= 'F')) {
		return c - 'A' + 10;
	}
	if ((c >= 'a') && (c <= 'f')) {
		return c - 'a' + 10;
	}
	/* error */
	return INVALID_ASCII_NIBBLE;
}

/* Parse the user arguments and fill the can frame accordingly */
int parse_canframe(
	char *cs,
	struct can_frame *cf
	)
{
	int i, idx, dlen, len;
	/* Init to 8 byte std can */
	int maxdlen = CAN_2_0_MAX_DLEN;
	int ret = CAN_MTU_BYTES;
	unsigned char tmp;
	canid_t can_id_fill = 0;

	PRINT("%s: enter %s", __func__, cs);
	len = strlen(cs);
	/* init CAN FD frame, e.g. LEN = 0 */
	memset(cf, 0, sizeof(*cf));

	if (len < 4) {
		return 0;
	}

	/* delimiter at 3rd position */
	if (cs[3] == CANID_DELIM) {
		idx = 4;
		for (i = 0; i < 3; i++) {
			tmp = asc2nibble(cs[i]);
			/* Nibble cant be > 0xf */
			if (tmp > 0x0F) {
				return 0;
			}
			can_id_fill |= fill_can_frame(CAN_STD_FR, tmp, i);
			PRINT("Pass %d CAN id is %x", i, can_id_fill);
		}
		cf->can_id = can_id_fill;
		PRINT("%s: STD Frame: ID 0x%x", __func__, cf->can_id);
	} else if (cs[8] == CANID_DELIM) {
		/* delimiter at 8th position */
		idx = 9;
		for (i = 0; i < 8; i++) {
			tmp = asc2nibble(cs[i]);
			if (tmp > 0x0F) {
				return 0;
			}
			can_id_fill |= fill_can_frame(CAN_EXT_FR, tmp, i);
			PRINT("Pass %d CAN id is %x", i, can_id_fill);
		}
		cf->can_id = can_id_fill;
		PRINT("%s: EXT Frame ID 0x%x", __func__, cf->can_id);
		if (!(cf->can_id & CAN_ERR_FLAG)) {
			cf->can_id |= CAN_EFF_FLAG;
		}
		/* 8 digits but no errorframe
		 * then it is an extended frame
		 */
	} else {
		return 0;
	}
	/* RTR frame */
	if ((cs[idx] == 'R') || (cs[idx] == 'r')) {
		cf->can_id |= CAN_RTR_FLAG;
		PRINT("%s: RTR Frame", __func__);
		/* check for DLC value for CAN frames */
		if (cs[++idx]) {
			tmp = asc2nibble(cs[idx]);
			if (tmp <= CAN_2_0_MAX_DLC && tmp != 0) {
				cf->can_dlc = tmp;
			} else {
				/* DLC cannot be 0 */
				return 0;
			}
		}
		return ret;
	}
	/* ID for FD frame would be already parsed above */
	if (cs[idx] == CANID_DELIM) {
		/* CAN FD frame escape char '##' */
		PRINT("%s: FD Frame", __func__);
		maxdlen = CANFD_MAX_DLEN;
		ret = CANFD_MTU_BYTES;

		/* CAN FD frame <canid>##<flags><data> */
		tmp = asc2nibble(cs[idx + 1]);
		if (tmp > 0x0F) {
			return 0;
		}
		idx += 2;
	}

	for (i = 0, dlen = 0; i < maxdlen; i++) {
		if (cs[idx] == DATA_DELIMITOR) {
			/* skip (optional) separator */
			idx++;
		}
		if (idx >= len) {
			/* end of string => end of data */
			break;
		}
		/* Fill data into frame MSB first*/
		tmp = asc2nibble(cs[idx++]);
		if (tmp > 0x0F) {
			return 0;
		}
		cf->data[i] = (tmp << 4);
		tmp = asc2nibble(cs[idx++]);
		if (tmp > 0x0F) {
			return 0;
		}
		cf->data[i] |= tmp;
		dlen++;
	}
	cf->can_dlc = dlen;
	return ret;
}

#if !defined(CONFIG_LIB_DEBUG_ENABLE) || !defined(CONFIG_CAN_UTILS_DEBUG)
/*
 * Dummy print when debug is disabled
 */
void dummy_print(char *ch, ...)
{
}
/* CONFIG_LIB_DEBUG_ENABLE || CONFIG_CAN_UTILS_DEBUG */
#endif

